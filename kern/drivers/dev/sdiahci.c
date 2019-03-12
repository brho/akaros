/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * ahci serial ata driver
 * copyright © 2007-8 coraid, inc.
 */

#include <assert.h>
#include <cpio.h>
#include <error.h>
#include <kmalloc.h>
#include <kref.h>
#include <net/ip.h>
#include <pmap.h>
#include <sd.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>

#include <ahci.h>

enum { Vatiamd = 0x1002,
       Vintel = 0x8086,
       Vmarvell = 0x1b4b,
};

#define iprintd(...)                                                           \
	do {                                                                   \
		if (prid)                                                      \
			printd(__VA_ARGS__);                                   \
	} while (0)
#define aprintd(...)                                                           \
	do {                                                                   \
		if (datapi)                                                    \
			printd(__VA_ARGS__);                                   \
	} while (0)
#define Tname(c) tname[(c)->type]
#define Intel(x) ((x)->pci->ven_id == Vintel)

enum { NCtlr = 16,
       NCtlrdrv = 32,
       NDrive = NCtlr * NCtlrdrv,

       Read = 0,
       Write,

       Nms = 256, /* ms. between drive checks */
       Mphywait = 2 * 1024 / Nms - 1,
       Midwait = 16 * 1024 / Nms - 1,
       Mcomrwait = 64 * 1024 / Nms - 1,

       Obs = 0xa0, /* obsolete device bits */

       /*
	* if we get more than this many interrupts per tick for a drive,
	* either the hardware is broken or we've got a bug in this driver.
	*/
       Maxintrspertick = 2000, /* was 1000 */
};

/* pci space configuration */
enum { Pmap = 0x90,
       Ppcs = 0x91,
       Prev = 0xa8,
};

enum { Tesb,
       Tich,
       Tsb600,
       Tunk,
};

static char *tname[] = {
    "63xxesb",
    "ich",
    "sb600",
    "unknown",
};

enum { Dnull,
       Dmissing,
       Dnew,
       Dready,
       Derror,
       Dreset,
       Doffline,
       Dportreset,
       Dlast,
};

static char *diskstates[Dlast] = {
    "null", "missing", "new", "ready", "error", "reset", "offline", "portreset",
};

enum { DMautoneg,
       DMsatai,
       DMsataii,
       DMsata3,
};

static char *modename[] = {
    /* used in control messages */
    "auto",
    "satai",
    "sataii",
    "sata3",
};
static char *descmode[] = {
    /*  only printed */
    "auto",
    "sata 1",
    "sata 2",
    "sata 3",
};

static char *flagname[] = {
    "llba", "smart", "power", "nop", "atapi", "atapi16",
};

struct drive {
	spinlock_t Lock;

	struct ctlr *ctlr;
	struct sdunit *unit;
	char name[10];
	void *port;
	struct aportm portm;
	struct aportc portc; /* redundant ptr to port and portm */

	unsigned char mediachange;
	unsigned char state;
	unsigned char smartrs;

	uint64_t sectors;
	uint32_t secsize;
	uint32_t intick; /* start tick of current transfer */
	uint32_t lastseen;
	int wait;
	unsigned char mode; /* DMautoneg, satai or sataii */
	unsigned char active;

	char serial[20 + 1];
	char firmware[8 + 1];
	char model[40 + 1];

	int infosz;
	uint16_t *info;
	uint16_t tinyinfo[2]; /* used iff malloc fails */

	int driveno; /* ctlr*NCtlrdrv + unit */
	/* controller port # != driveno when not all ports are enabled */
	int portno;

	uint32_t lastintr0;
	uint32_t intrs;
};

struct ctlr {
	spinlock_t Lock;

	int type;
	int enabled;
	struct sdev *sdev;
	struct pci_device *pci;
	void *vector;

	/* virtual register addresses */
	void *mmio;
	void *hba;

	/* phyical register address */
	uintptr_t physio;

	struct drive *rawdrive;
	struct drive *drive[NCtlrdrv];
	int ndrive;
	int mport; /* highest drive # (0-origin) on ich9 at least */

	uint32_t lastintr0;
	uint32_t intrs; /* not attributable to any drive */
};

struct Asleep {
	void *p;
	int i;
};

struct sdifc sdiahciifc; // TODO(afergs): make static???

static struct ctlr iactlr[NCtlr];
static struct sdev sdevs[NCtlr];
static int niactlr;

static struct drive *iadrive[NDrive];
static int niadrive;

/* these are fiddled in iawtopctl() */
static int debug;
static int prid = 1;
static int datapi;

// TODO: does this get initialized correctly?
static char stab[] = {[0] = 'i',  'm', [8] = 't', 'c', 'p', 'e',
                      [16] = 'N', 'I', 'W',       'B', 'D', 'C',
                      'H',        'S', 'T',       'F', 'X'};

// AHCI register access helper functions
// TODO(afergs): Figure out what the datatype for reg/offset should really be!

static inline uint32_t ahci_hba_read32(void *base, uint32_t reg)
{
	return read_mmreg32((uintptr_t)base + reg);
}

static inline void ahci_hba_write32(void *base, uint32_t reg, uint32_t val)
{
	write_mmreg32((uintptr_t)base + reg, val);
}

static inline uint32_t ahci_port_read32(void *aport, uint32_t reg)
{
	return read_mmreg32((uintptr_t)aport + reg);
}

static inline void ahci_port_write32(void *aport, uint32_t reg, uint32_t val)
{
	write_mmreg32((uintptr_t)aport + reg, val);
}

static inline uint32_t ahci_list_read32(void *alist, uint32_t reg)
{
	return read_mmreg32((uintptr_t)alist + reg);
}

static inline void ahci_list_write32(void *alist, uint32_t reg, uint32_t val)
{
	write_mmreg32((uintptr_t)alist + reg, val);
}

static inline uint32_t ahci_prdt_read32(void *aprdt, uint32_t reg)
{
	return read_mmreg32((uintptr_t)aprdt + reg);
}

static inline void ahci_prdt_write32(void *aprdt, uint32_t reg, uint32_t val)
{
	write_mmreg32((uintptr_t)aprdt + reg, val);
}

static inline uint8_t ahci_cfis_read8(void *acfis, uint8_t offset)
{
	return read_mmreg8((uintptr_t)acfis + offset);
}

static inline void ahci_cfis_write8(void *acfis, uint8_t offset, uint8_t val)
{
	write_mmreg8((uintptr_t)acfis + offset, val);
}

static uint32_t set_bit32(void *base, uint32_t offset, uint32_t mask)
{
	uint32_t value = read_mmreg32((uintptr_t)base + offset);

	value |= mask;
	write_mmreg32((uintptr_t)base + offset, value);
	return value;
}

static uint32_t clear_bit32(void *base, uint32_t offset, uint32_t mask)
{
	uint32_t value = read_mmreg32((uintptr_t)base + offset);

	value &= ~mask;
	write_mmreg32((uintptr_t)base + offset, value);
	return value;
}

static uint8_t set_bit8(void *base, uint32_t offset, uint8_t mask)
{
	uint8_t value = read_mmreg8((uintptr_t)base + offset);

	value |= mask;
	write_mmreg8((uintptr_t)base + offset, value);
	return value;
}

static uint8_t clear_bit8(void *base, uint32_t offset, uint8_t mask)
{
	uint8_t value = read_mmreg8((uintptr_t)base + offset);

	value &= ~mask;
	write_mmreg8((uintptr_t)base + offset, value);
	return value;
}

/* ALL time units in this file are in milliseconds. */
static uint32_t ms(void)
{
	return (uint32_t)(epoch_nsec() / 1048576);
}

/* TODO: if we like this, make it useable elsewhere. */
static void sdierror(struct cmdbuf *cb, char *fmt, ...)
{
	char *c = kzmalloc(512, MEM_WAIT);
	va_list ap;

	assert(fmt);
	va_start(ap, fmt);
	vsnprintf(c, 512, fmt, ap);
	va_end(ap);
	cmderror(cb, c);
	kfree(c);
}

static void serrstr(uint32_t r, char *s, char *e)
{
	int i;

	e -= 3;
	for (i = 0; i < ARRAY_SIZE(stab) && s < e; i++)
		if (r & (1 << i) && stab[i]) {
			*s++ = stab[i];
			if (SerrBad & (1 << i))
				*s++ = '*';
		}
	*s = 0;
}

static char ntab[] = "0123456789abcdef";

static void preg(volatile unsigned char *reg, int n)
{
	int i;
	char buf[25 * 3 + 1], *e;

	e = buf;
	for (i = 0; i < n; i++) {
		*e++ = ntab[reg[i] >> 4];
		*e++ = ntab[reg[i] & 0xf];
		*e++ = ' ';
	}
	*e++ = '\n';
	*e = 0;
	printd(buf);
}

static void dreg(char *s, void *p)
{
	printd("ahci: %stask=%#lx; cmd=%#lx; ci=%#lx; is=%#lx\n", s,
	       ahci_port_read32(p, PORT_TFD), ahci_port_read32(p, PORT_CMD),
	       ahci_port_read32(p, PORT_CI), ahci_port_read32(p, PORT_IS));
}

static void esleep(int ms)
{
	ERRSTACK(1);

	if (waserror()) {
		poperror();
		return;
	}
	kthread_usleep(ms * 1000);
	poperror();
}

static int ahciclear(void *v)
{
	struct Asleep *s;

	s = v;
	return (ahci_port_read32(s->p, PORT_CI) & s->i) == 0;
}

static void aesleep(struct aportm *pm, struct Asleep *a, int ms)
{
	ERRSTACK(1);

	if (waserror()) {
		poperror();
		return;
	}
	rendez_sleep_timeout(&pm->Rendez, ahciclear, a, ms * 1000);
	poperror();
}

static int ahciwait(struct aportc *c, int ms)
{
	struct Asleep as;
	void *port;
	uint32_t cmd, tfd;

	port = c->p;
	cmd = ahci_port_read32(port, PORT_CMD);
	printd("ahci: %s: CMD=0x%08x\n", __func__, cmd);

	// TODO: Set the correct CI bit for the slot, not always slot 0!
	ahci_port_write32(port, PORT_CI, 1);
	as.p = port;
	as.i = 1;
	aesleep(c->pm, &as, ms);
	tfd = ahci_port_read32(port, PORT_TFD);
	if (((tfd & 0x81) == 0) && // Not busy and not error
	    (ahci_port_read32(port, PORT_CI) == 0))
		return 0;
	dreg("ahci: ahciwait: timeout ", c->p);
	return -1;
}

/* fill in cfis boilerplate */
static void *cfissetup(struct aportc *pc)
{
	void *cfis;

	cfis = pc->pm->ctab; // cfis is the first thing in ctab, same location
	memset((void *)cfis, 0, 0x20);
	ahci_cfis_write8(cfis, 0, 0x27); // H2D
	ahci_cfis_write8(cfis, 1,
	                 0x80); // Transfer due to update to CMD register
	ahci_cfis_write8(cfis, 7, Obs);
	return cfis;
}

/* initialize pc's list */
static void listsetup(struct aportc *pc, int flags)
{
	void *list;

	list = pc->pm->list;
	ahci_list_write32(list, ALIST_FLAGS, flags | 5);
	ahci_list_write32(list, ALIST_LEN, 0);
	ahci_list_write32(list, ALIST_CTAB, paddr_low32(pc->pm->ctab));
	ahci_list_write32(list, ALIST_CTABHI, paddr_high32(pc->pm->ctab));
	printd("ahci: %s: CTAB physical address=0x%08x:0x%08x\n", __func__,
	       paddr_high32(pc->pm->ctab), paddr_low32(pc->pm->ctab));
}

static int nop(struct aportc *pc)
{
	void *cfis;

	if ((pc->pm->feat & Dnop) == 0)
		return -1;
	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, 0);
	listsetup(pc, Lwrite);
	return ahciwait(pc, 3 * 1000);
}

static int setfeatures(struct aportc *pc, unsigned char f)
{
	void *cfis;

	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, 0xef);
	ahci_cfis_write8(cfis, 3, f);
	listsetup(pc, Lwrite);
	return ahciwait(pc, 3 * 1000);
}

static int setudmamode(struct aportc *pc, unsigned char f)
{
	void *cfis;

	printd("ahci: %s: PORT_SIG=0x%08x\n", __func__,
	       ahci_port_read32(pc->p, PORT_SIG));
	/* hack */
	if ((ahci_port_read32(pc->p, PORT_SIG) >> 16) == 0xeb14)
		return 0;
	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, 0xef);
	ahci_cfis_write8(cfis, 3, 3);         /* set transfer mode */
	ahci_cfis_write8(cfis, 12, 0x40 | f); /* sector count */
	listsetup(pc, Lwrite);
	return ahciwait(pc, 3 * 1000);
}

static void asleep(int ms)
{
	udelay(ms * 1000);
}

static int ahciportreset(struct aportc *c)
{
	uint32_t cmd, i, scr_ctl;
	void *port;

	port = c->p;
	clear_bit32(port, PORT_CMD, Afre | Ast);
	for (i = 0; i < 500; i += 25) {
		if ((ahci_port_read32(port, PORT_CMD) & Acr) == 0)
			break;
		asleep(25);
	}

	scr_ctl = ahci_port_read32(port, PORT_SCTL);
	scr_ctl = 1 | (scr_ctl & ~7);
	ahci_port_write32(port, PORT_SCTL, scr_ctl);
	printk("Sleeping one second\n");
	udelay(1000 * 1000);
	clear_bit32(port, PORT_SCTL, 7); // TODO: Make sure PxCMD.ST == 0 first?
	return 0;
}

static int smart(struct aportc *pc, int n)
{
	void *cfis;

	if ((pc->pm->feat & Dsmart) == 0)
		return -1;
	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, 0xb0);
	ahci_cfis_write8(cfis, 3, 0xd8 + n); /* able smart */
	ahci_cfis_write8(cfis, 5, 0x4f);
	ahci_cfis_write8(cfis, 6, 0xc2);
	listsetup(pc, Lwrite);
	// TODO(afergs): Replace 1|32 with constants
	if (ahciwait(pc, 1000) == -1 ||
	    ahci_port_read32(pc->p, PORT_TFD) & (1 | 32)) {
		printd("ahci: smart fail %#lx\n",
		       ahci_port_read32(pc->p, PORT_TFD));
		return -1;
	}
	if (n)
		return 0;
	return 1;
}

static int smartrs(struct aportc *pc)
{
	void *cfis;
	volatile unsigned char *rfis;
	uint32_t tfd;

	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, 0xb0);
	ahci_cfis_write8(cfis, 3, 0xda); /* return smart status */
	ahci_cfis_write8(cfis, 5, 0x4f);
	ahci_cfis_write8(cfis, 6, 0xc2);
	listsetup(pc, Lwrite);

	rfis = pc->pm->fis.r;
	tfd = ahci_port_read32(pc->p, PORT_TFD);
	// TODO(afergs): Replace 1|32 with constants
	if (ahciwait(pc, 1000) == -1 || tfd & (1 | 32)) {
		printd("ahci: smart fail %#lx\n", tfd);
		preg(rfis, 20);
		return -1;
	}
	if (rfis[5] == 0x4f && rfis[6] == 0xc2)
		return 1;
	return 0;
}

static int ahciflushcache(struct aportc *pc)
{
	void *cfis;

	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, pc->pm->feat & Dllba ? 0xea : 0xe7);
	listsetup(pc, Lwrite);
	if (ahciwait(pc, 60000) == -1 ||
	    ahci_port_read32(pc->p, PORT_TFD) & (1 | 32)) {
		printd("ahciflushcache: fail %#lx\n",
		       ahci_port_read32(pc->p, PORT_TFD));
		// preg(pc->m->fis.r, 20);
		return -1;
	}
	return 0;
}

static uint16_t gbit16(void *a)
{
	unsigned char *i;

	i = a;
	return i[1] << 8 | i[0];
}

static uint32_t gbit32(void *a)
{
	uint32_t j;
	unsigned char *i;

	i = a;
	j = i[3] << 24;
	j |= i[2] << 16;
	j |= i[1] << 8;
	j |= i[0];
	return j;
}

static uint64_t gbit64(void *a)
{
	unsigned char *i;

	i = a;
	return (uint64_t)gbit32(i + 4) << 32 | gbit32(a);
}

static int ahciidentify0(struct aportc *pc, void *id, int atapi)
{
	void *cfis, *prdt;
	static unsigned char tab[] = {
	    0xec,
	    0xa1,
	};

	cfis = cfissetup(pc);
	ahci_cfis_write8(cfis, 2, tab[atapi]);
	printd("ahci: %s: ATAPI=%d, cfis[2]: 0x%02x\n", __func__, atapi,
	       ahci_cfis_read8(cfis, 2));
	printd("ahci: %s: CFIS physical address=0x%08x:0x%08x\n", __func__,
	       paddr_high32(cfis), paddr_low32(cfis));

	listsetup(pc, 1 << 16);

	memset(id, 0, 0x200); /* Fill 512 bytes with 0's */
	prdt = pc->pm->ctab + ACTAB_PRDT;
	ahci_prdt_write32(prdt, APRDT_DBA, paddr_low32(id));
	ahci_prdt_write32(prdt, APRDT_DBAHI, paddr_high32(id));
	ahci_prdt_write32(prdt, APRDT_COUNT, 1 << 31 | (0x200 - 2) | 1);
	printd("ahci: %s: DBA  physical address=0x%08x:0x%08x\n", __func__,
	       paddr_high32(id), paddr_low32(id));

	return ahciwait(pc, 3 * 1000);
}

static int64_t ahciidentify(struct aportc *pc, uint16_t *id)
{
	int i, sig;
	int64_t s;
	struct aportm *pm;
	int cnt;

	pm = pc->pm;
	pm->feat = 0;
	pm->smart = 0;
	i = 0;
	sig = ahci_port_read32(pc->p, PORT_SIG) >> 16;
	if (sig == 0xeb14) {
		pm->feat |= Datapi;
		i = 1;
	}
	if (ahciidentify0(pc, id, i) == -1)
		return -1;

#ifdef DEBUG
	printd("ahci: %s: ahciidentify0 return dump=\n\t", __func__);
	for (cnt = 0; cnt < 64; cnt++) {
		printd("0x%08x  ", id[cnt]);
		if (cnt % 4 == 3 && cnt != 63)
			printd("\n\t");
	}
	printd("\n");
#endif

	i = gbit16(id + 83) | gbit16(id + 86);
	if (i & (1 << 10)) {
		pm->feat |= Dllba;
		s = gbit64(id + 100);
	} else
		s = gbit32(id + 60);

	if (pm->feat & Datapi) {
		i = gbit16(id + 0);
		if (i & 1)
			pm->feat |= Datapi16;
	}

	i = gbit16(id + 83);
	if ((i >> 14) == 1) {
		if (i & (1 << 3))
			pm->feat |= Dpower;
		i = gbit16(id + 82);
		if (i & 1)
			pm->feat |= Dsmart;
		if (i & (1 << 14))
			pm->feat |= Dnop;
	}
	return s;
}

#if 0
static int
ahciquiet(struct aport *a)
{
	uint32_t *p, i;

	p = &a->cmd;
	*p &= ~Ast;
	for(i = 0; i < 500; i += 50){
		if((*p & Acr) == 0)
			goto stop;
		asleep(50);
	}
	return -1;
stop:
	if((a->task & (ASdrq|ASbsy)) == 0){
		*p |= Ast;
		return 0;
	}

	*p |= Aclo;
	for(i = 0; i < 500; i += 50){
		if((*p & Aclo) == 0)
			goto stop1;
		asleep(50);
	}
	return -1;
stop1:
	/* extra check */
	printd("ahci: clo clear %#lx\n", a->task);
	if(a->task & ASbsy)
		return -1;
	*p |= Ast;
	return 0;
}
#endif

#if 0
static int
ahcicomreset(struct aportc *pc)
{
	unsigned char *c;

	printd("ahcicomreset\n");
	dreg("ahci: comreset ", pc->p);
	if(ahciquiet(pc->p) == -1){
		printd("ahciquiet failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);

	c = cfissetup(pc);
	ahci_cfis_write8(cfis, 1, 0);
	ahci_cfis_write8(cfis, 15, 1<<2);	/* srst */
	listsetup(pc, Lclear | Lreset);
	if(ahciwait(pc, 500) == -1){
		printd("ahcicomreset: first command failed\n");
		return -1;
	}
	microdelay(250);
	dreg("comreset ", pc->p);

	c = cfissetup(pc);
	ahci_cfis_write8(cfis, 1, 0);
	listsetup(pc, Lwrite);
	if (ahciwait(pc, 150) == -1) {
		printd("ahcicomreset: second command failed\n");
		return -1;
	}
	dreg("comreset ", pc->p);
	return 0;
}
#endif

static int ahciidle(void *port)
{
	uint32_t i, r, cmd;

	cmd = ahci_port_read32(port, PORT_CMD);
	if ((cmd & Arun) == 0)
		return 0;
	cmd &= ~Ast;
	ahci_port_write32(port, PORT_CMD, cmd);

	r = 0;
	for (i = 0; i < 500; i += 25) {
		if ((ahci_port_read32(port, PORT_CMD) & Acr) == 0)
			goto stop;
		asleep(25);
	}
	r = -1;
stop:
	if ((ahci_port_read32(port, PORT_CMD) & Afre) == 0)
		return r;

	clear_bit32(port, PORT_CMD, Afre);
	for (i = 0; i < 500; i += 25) {
		if ((ahci_port_read32(port, PORT_CMD) & Afre) == 0)
			return 0;
		asleep(25);
	}
	return -1;
}

/*
 * § 6.2.2.1  first part; comreset handled by reset disk.
 *	- remainder is handled by configdisk.
 *	- ahcirecover is a quick recovery from a failed command.
 */
static int ahciswreset(struct aportc *pc)
{
	int i;

	i = ahciidle(pc->p);

	set_bit32(pc->p, PORT_CMD, Afre);
	if (i == -1)
		return -1;
	if (ahci_port_read32(pc->p, PORT_TFD) & (ASdrq | ASbsy))
		return -1;
	return 0;
}

static int ahcirecover(struct aportc *pc)
{
	ahciswreset(pc);

	set_bit32(pc->p, PORT_CMD, Ast);
	if (setudmamode(pc, 5) == -1)
		return -1;
	return 0;
}

static void *malign(int size, int align)
{
	return kmalloc_align(size, MEM_WAIT, align);
}

static void setupfis(struct afis *f)
{
	f->base = malign(0x100, 0x100); /* magic */
	f->d = f->base + 0;
	f->p = f->base + 0x20;
	f->r = f->base + 0x40;
	f->u = f->base + 0x60;
	f->devicebits = (uint32_t *)(f->base + 0x58);
}

static void ahciwakeup(void *port)
{
	uint16_t s;
	uint32_t sctl;

	s = ahci_port_read32(port, PORT_SSTS);
	if ((s & Intpm) != Intslumber && (s & Intpm) != Intpartpwr)
		return;
	if ((s & Devdet) != Devpresent) { /* not (device, no phy) */
		iprint("ahci: slumbering drive unwakable %#x\n", s);
		return;
	}
	ahci_port_write32(port, PORT_SCTL, 3 * Aipm | 0 * Aspd | Adet);
	udelay(1000 * 1000);
	clear_bit32(port, PORT_SCTL, 7);
	//	iprint("ahci: wake %#x -> %#x\n", s, p->sstatus);
}

static int ahciconfigdrive(struct drive *d)
{
	char *name;
	void *hba, *port;
	struct aportm *pm;
	uint32_t cap, sstatus;

	hba = d->ctlr->hba;
	port = d->portc.p;
	pm = d->portc.pm;
	if (pm->list == 0) {
		setupfis(&pm->fis);
		pm->list = malign(ALIST_SIZE, 1024);
		pm->ctab = malign(ACTAB_PRDT + APRDT_SIZE, 128);
	}

	if (d->unit)
		name = d->unit->sdperm.name;
	else
		name = NULL;
	sstatus = ahci_port_read32(port, PORT_SSTS);
	cap = ahci_hba_read32(hba, HBA_CAP);
	if (sstatus & (Devphycomm | Devpresent) && cap & Hsss) {
		/* device connected & staggered spin-up */
		printd("ahci: configdrive: %s: spinning up ... [%#lx]\n", name,
		       sstatus);
		set_bit32(port, PORT_CMD, Apod | Asud);
		asleep(1400);
	}

	ahci_port_write32(port, PORT_SERR, SerrAll);

	ahci_port_write32(port, PORT_CLB, paddr_low32(pm->list));
	ahci_port_write32(port, PORT_CLBU, paddr_high32(pm->list));
	printd("ahci: %s: PORT_CLB physical address=0x%08x:0x%08x\n", __func__,
	       paddr_high32(pm->list), paddr_low32(pm->list));
	ahci_port_write32(port, PORT_FB, paddr_low32(pm->fis.base));
	ahci_port_write32(port, PORT_FBU, paddr_high32(pm->fis.base));
	printd("ahci: %s: PORT_FB  physical address=0x%08x:0x%08x\n", __func__,
	       paddr_high32(pm->fis.base), paddr_low32(pm->fis.base));

	set_bit32(port, PORT_CMD, Afre | Ast);

	/* drive coming up in slumbering? */
	sstatus = ahci_port_read32(port, PORT_SSTS);
	if ((sstatus & Devdet) == Devpresent &&
	    ((sstatus & Intpm) == Intslumber ||
	     (sstatus & Intpm) == Intpartpwr))
		ahciwakeup(port);

	/* "disable power management" sequence from book. */
	ahci_port_write32(port, PORT_SCTL,
	                  (3 * Aipm) | (d->mode * Aspd) | (0 * Adet));
	clear_bit32(port, PORT_CMD, Aalpe);
	ahci_port_write32(port, PORT_IE, IEM);

	return 0;
}

static void ahcienable(void *hba)
{
	set_bit32(hba, HBA_GHC, Hie);
}

static void ahcidisable(void *hba)
{
	clear_bit32(hba, HBA_GHC, Hie);
}

static int countbits(uint32_t u)
{
	int n;

	n = 0;
	for (; u != 0; u >>= 1)
		if (u & 1)
			n++;
	return n;
}

static int ahciconf(struct ctlr *ctlr)
{
	void *hba;
	uint32_t cap;

	hba = ctlr->hba = ctlr->mmio;
	cap = ahci_hba_read32(hba, HBA_CAP);

	if ((cap & Hsam) == 0)
		set_bit32(hba, HBA_GHC, Hae);

	printd("#S/sd%c: type %s port %#p: sss %ld ncs %ld coal %ld "
	       "%ld ports, led %ld clo %ld ems %ld\n",
	       ctlr->sdev->idno, tname[ctlr->type], hba, (cap >> 27) & 1,
	       (cap >> 8) & 0x1f, (cap >> 7) & 1, (cap & 0x1f) + 1,
	       (cap >> 25) & 1, (cap >> 24) & 1, (cap >> 6) & 1);
	return countbits(ahci_hba_read32(hba, HBA_PI));
}

#if 0
static int
ahcihbareset(void *hba)
{
	int wait;

	h->ghc |= 1;
	for(wait = 0; wait < 1000; wait += 100){
		if(h->ghc == 0)
			return 0;
		delay(100);
	}
	return -1;
}
#endif

static void idmove(char *p, uint16_t *a, int n)
{
	int i;
	char *op, *e;

	op = p;
	for (i = 0; i < n / 2; i++) {
		*p++ = a[i] >> 8;
		*p++ = a[i];
	}
	*p = 0;
	while (p > op && *--p == ' ')
		*p = 0;
	e = p;
	for (p = op; *p == ' '; p++)
		;
	memmove(op, p, n - (e - p));
}

static int identify(struct drive *d)
{
	uint16_t *id;
	int64_t osectors, s;
	unsigned char oserial[21];
	struct sdunit *u;

	if (d->info == NULL) {
		d->infosz = 512 * sizeof(uint16_t);
		d->info = kzmalloc(d->infosz, 0);
		printd("ahci: %s: HBA physical address (hack): 0x%08x:0x%08x\n",
		       __func__, paddr_high32(d->info), paddr_low32(d->info));
	}
	if (d->info == NULL) {
		d->info = d->tinyinfo;
		d->infosz = sizeof d->tinyinfo;
	}
	id = d->info;
	s = ahciidentify(&d->portc, id);
	if (s == -1) {
		d->state = Derror;
		return -1;
	}
	osectors = d->sectors;
	memmove(oserial, d->serial, sizeof d->serial);

	u = d->unit;
	d->sectors = s;
	d->secsize = u->secsize;
	if (d->secsize == 0)
		d->secsize = 512; /* default */
	d->smartrs = 0;

	idmove(d->serial, id + 10, 20);
	idmove(d->firmware, id + 23, 8);
	idmove(d->model, id + 27, 40);

	memset(u->inquiry, 0, sizeof u->inquiry);
	u->inquiry[2] = 2;
	u->inquiry[3] = 2;
	u->inquiry[4] = sizeof u->inquiry - 4;
	memmove(u->inquiry + 8, d->model, 40);

	if (osectors != s || memcmp(oserial, d->serial, sizeof oserial) != 0) {
		d->mediachange = 1;
		u->sectors = 0;
	}
	return 0;
}

static void clearci(void *p)
{
	uint32_t cmd;

	cmd = ahci_port_read32(p, PORT_CMD);
	if (cmd & Ast) {
		clear_bit32(p, PORT_CMD, Ast);
		set_bit32(p, PORT_CMD, Ast);
	}
}

static void updatedrive(struct drive *d)
{
	uint32_t cause, serr, task, sstatus, ie, s0, pr, ewake;
	char *name;
	void *port;
	static uint32_t last;

	pr = 1;
	ewake = 0;
	port = d->port;
	cause = ahci_port_read32(port, PORT_IS);
	serr = ahci_port_read32(port, PORT_SERR);
	ahci_port_write32(port, PORT_IS, cause);
	name = "??";
	if (d->unit && d->unit->sdperm.name)
		name = d->unit->sdperm.name;

	if (ahci_port_read32(port, PORT_CI) == 0) {
		d->portm.flag |= Fdone;
		rendez_wakeup(&d->portm.Rendez);
		pr = 0;
	} else if (cause & Adps)
		pr = 0;
	if (cause & Ifatal) {
		ewake = 1;
		printd("ahci: updatedrive: %s: fatal\n", name);
	}
	task = ahci_port_read32(port, PORT_TFD);
	if (cause & Adhrs) {
		if (task & (1 << 5 | 1)) {
			printd(
			    "ahci: %s: Adhrs cause %#lx serr %#lx task %#lx\n",
			    name, cause, serr, task);
			d->portm.flag |= Ferror;
			ewake = 1;
		}
		pr = 0;
	}
	sstatus = ahci_port_read32(port, PORT_SSTS);
	if (task & 1 && last != cause)
		printd("%s: err ca %#lx serr %#lx task %#lx sstat %#lx\n", name,
		       cause, serr, task, sstatus);
	if (pr)
		printd("%s: upd %#lx ta %#lx\n", name, cause, task);

	if (cause & (Aprcs | Aifs)) {
		s0 = d->state;
		switch (sstatus & Devdet) {
		case 0: /* no device */
			d->state = Dmissing;
			break;
		case Devpresent: /* device but no phy comm. */
			if ((sstatus & Intpm) == Intslumber ||
			    (sstatus & Intpm) == Intpartpwr)
				d->state = Dnew; /* slumbering */
			else
				d->state = Derror;
			break;
		case Devpresent | Devphycomm:
			/* power mgnt crap for surprise removal */
			set_bit32(port, PORT_IE,
			          Aprcs | Apcs); /* is this required? */
			d->state = Dreset;
			break;
		case Devphyoffline:
			d->state = Doffline;
			break;
		}
		printd("%s: %s → %s [Apcrs] %#lx\n", name, diskstates[s0],
		       diskstates[d->state], sstatus);
		/* print pulled message here. */
		if (s0 == Dready && d->state != Dready)
			iprintd("%s: pulled\n", name); /* wtf? */
		if (d->state != Dready)
			d->portm.flag |= Ferror;
		ewake = 1;
	}
	ahci_port_write32(port, PORT_SERR, serr);
	if (ewake) {
		clearci(port);
		rendez_wakeup(&d->portm.Rendez);
	}
	last = cause;
}

static void pstatus(struct drive *d, uint32_t s)
{
	/*
	 * s is masked with Devdet.
	 *
	 * bogus code because the first interrupt is currently dropped.
	 * likely my fault.  serror may be cleared at the wrong time.
	 */
	switch (s) {
	case 0: /* no device */
		d->state = Dmissing;
		break;
	case Devpresent: /* device but no phy. comm. */
		break;
	case Devphycomm: /* should this be missing?  need testcase. */
		printd("ahci: pstatus 2\n");
	/* fallthrough */
	case Devpresent | Devphycomm:
		d->wait = 0;
		d->state = Dnew;
		break;
	case Devphyoffline:
		d->state = Doffline;
		break;
	case Devphyoffline | Devphycomm: /* does this make sense? */
		d->state = Dnew;
		break;
	}
}

static int configdrive(struct drive *d)
{
	if (ahciconfigdrive(d) == -1)
		return -1;
	spin_lock_irqsave(&d->Lock);
	pstatus(d, ahci_port_read32(d->port, PORT_SSTS) & Devdet);
	spin_unlock_irqsave(&d->Lock);
	return 0;
}

static void setstate(struct drive *d, int state)
{
	spin_lock_irqsave(&d->Lock);
	d->state = state;
	spin_unlock_irqsave(&d->Lock);
}

static void resetdisk(struct drive *d)
{
	unsigned int state, det, cmd, stat;
	void *port;

	port = d->port;
	det = ahci_port_read32(port, PORT_SCTL) & 7;
	stat = ahci_port_read32(port, PORT_SSTS) & Devdet;
	cmd = ahci_port_read32(port, PORT_CMD);
	state = (cmd >> 28) & 0xf;
	printd("ahci: resetdisk: icc %#x  det %d sdet %d\n", state, det, stat);

	spin_lock_irqsave(&d->Lock);
	state = d->state;
	if (d->state != Dready || d->state != Dnew)
		d->portm.flag |= Ferror;
	clearci(port); /* satisfy sleep condition. */
	rendez_wakeup(&d->portm.Rendez);
	if (stat != (Devpresent | Devphycomm)) {
		/* device absent or phy not communicating */
		d->state = Dportreset;
		spin_unlock_irqsave(&d->Lock);
		return;
	}
	d->state = Derror;
	spin_unlock_irqsave(&d->Lock);

	qlock(&d->portm.ql);
	if (cmd & Ast && ahciswreset(&d->portc) == -1)
		setstate(d, Dportreset); /* get a bigger stick. */
	else {
		setstate(d, Dmissing);
		configdrive(d);
	}
	printd("ahci: %s: resetdisk: %s → %s\n",
	       (d->unit ? d->unit->sdperm.name : NULL), diskstates[state],
	       diskstates[d->state]);
	qunlock(&d->portm.ql);
}

static int newdrive(struct drive *d)
{
	char *name;
	struct aportc *pc;
	struct aportm *pm;

	pc = &d->portc;
	pm = &d->portm;

	name = d->unit->sdperm.name;
	if (name == 0)
		name = "??";

	if (ahci_port_read32(d->port, PORT_TFD) == 0x80)
		return -1;
	qlock(&pc->pm->ql);
	if (setudmamode(pc, 5) == -1) {
		printd("%s: can't set udma mode\n", name);
		goto lose;
	}
	if (identify(d) == -1) {
		printd("%s: identify failure\n", name);
		goto lose;
	}
	if (pm->feat & Dpower && setfeatures(pc, 0x85) == -1) {
		pm->feat &= ~Dpower;
		if (ahcirecover(pc) == -1)
			goto lose;
	}
	setstate(d, Dready);
	qunlock(&pc->pm->ql);

	iprintd("%s: %sLBA %llu sectors: %s %s %s %s\n", d->unit->sdperm.name,
	        (pm->feat & Dllba ? "L" : ""), d->sectors, d->model,
	        d->firmware, d->serial, d->mediachange ? "[mediachange]" : "");
	return 0;

lose:
	iprintd("%s: can't be initialized\n", d->unit->sdperm.name);
	setstate(d, Dnull);
	qunlock(&pc->pm->ql);
	return -1;
}

static void westerndigitalhung(struct drive *d)
{
	uint32_t task, ci;

	task = ahci_port_read32(d->port, PORT_TFD);
	ci = ahci_port_read32(d->port, PORT_CI);

	if ((d->portm.feat & Datapi) == 0 && d->active &&
	    (ms() - d->intick) > 5000) {
		printd("%s: drive hung; resetting [%#lx] ci %#lx\n",
		       d->unit->sdperm.name, task, ci);
		d->state = Dreset;
	}
}

static uint16_t olds[NCtlr * NCtlrdrv];

static int doportreset(struct drive *d)
{
	int i;

	i = -1;
	qlock(&d->portm.ql);
	if (ahciportreset(&d->portc) == -1)
		printd("ahci: doportreset: fails\n") else i = 0;
	qunlock(&d->portm.ql);
	printd("ahci: doportreset: portreset → %s  [task %#lx]\n",
	       diskstates[d->state], ahci_port_read32(d->port, PORT_TFD));
	return i;
}

/* drive must be locked */
static void statechange(struct drive *d)
{
	switch (d->state) {
	case Dnull:
	case Doffline:
		if (d->unit->sectors != 0) {
			d->sectors = 0;
			d->mediachange = 1;
		}
	/* fallthrough */
	case Dready:
		d->wait = 0;
		break;
	}
}

static void checkdrive(struct drive *d, int i)
{
	uint16_t s;
	uint32_t task;
	char *name;

	if (d == NULL) {
		printd("checkdrive: NULL d\n");
		return;
	}
	spin_lock_irqsave(&d->Lock);
	if (d->unit == NULL || d->port == NULL) {
		if (0)
			printk("checkdrive: nil d->%s\n",
			       d->unit == NULL ? "unit" : "port");
		spin_unlock_irqsave(&d->Lock);
		return;
	}
	name = d->unit->sdperm.name;
	s = ahci_port_read32(d->port, PORT_SSTS);
	if (s)
		d->lastseen = ms();
	if (s != olds[i]) {
		printd("%s: status: %06#x -> %06#x: %s\n", name, olds[i], s,
		       diskstates[d->state]);
		olds[i] = s;
		d->wait = 0;
	}
	westerndigitalhung(d);

	switch (d->state) {
	case Dnull:
	case Dready:
		break;
	case Dmissing:
	case Dnew:
		switch (s & (Intactive | Devdet)) {
		case Devpresent: /* no device (pm), device but no phy. comm. */
			ahciwakeup(d->port);
		/* fall through */
		case 0: /* no device */
			break;
		default:
			printd("%s: unknown status %06#x\n", name, s);
		/* fall through */
		case Intactive: /* active, no device */
			if (++d->wait & Mphywait)
				break;
		reset:
			if (++d->mode > DMsataii)
				d->mode = 0;
			if (d->mode == DMsatai) { /* we tried everything */
				d->state = Dportreset;
				goto portreset;
			}
			printd("%s: reset; new mode %s\n", name,
			       modename[d->mode]);
			spin_unlock_irqsave(&d->Lock);
			resetdisk(d);
			spin_lock_irqsave(&d->Lock);
			break;
		case Intactive | Devphycomm | Devpresent:
			task = ahci_port_read32(d->port, PORT_TFD);
			if ((++d->wait & Midwait) == 0) {
				printd("%s: slow reset %06#x task=%#lx; %d\n",
				       name, s, task, d->wait);
				goto reset;
			}
			s = (unsigned char)task;
			if (s == 0x7f ||
			    ((ahci_port_read32(d->port, PORT_SIG) >> 16) !=
			         0xeb14 &&
			     (s & ~0x17) != (1 << 6)))
				break;
			spin_unlock_irqsave(&d->Lock);
			newdrive(d);
			spin_lock_irqsave(&d->Lock);
			break;
		}
		break;
	case Doffline:
		if (d->wait++ & Mcomrwait)
			break;
	/* fallthrough */
	case Derror:
	case Dreset:
		printd("%s: reset [%s]: mode %d; status %06#x\n", name,
		       diskstates[d->state], d->mode, s);
		spin_unlock_irqsave(&d->Lock);
		resetdisk(d);
		spin_lock_irqsave(&d->Lock);
		break;
	case Dportreset:
	portreset:
		if (d->wait++ & 0xff && (s & Intactive) == 0)
			break;
		/* device is active */
		printd("%s: portreset [%s]: mode %d; status %06#x\n", name,
		       diskstates[d->state], d->mode, s);
		d->portm.flag |= Ferror;
		clearci(d->port);
		rendez_wakeup(&d->portm.Rendez);
		if ((s & Devdet) == 0) { /* no device */
			d->state = Dmissing;
			break;
		}
		spin_unlock_irqsave(&d->Lock);
		doportreset(d);
		spin_lock_irqsave(&d->Lock);
		break;
	}
	statechange(d);
	spin_unlock_irqsave(&d->Lock);
}

static void satakproc(void *v)
{
	int i;
	for (;;) {
		kthread_usleep(Nms * 1000);
		for (i = 0; i < niadrive; i++)
			if (iadrive[i] != NULL)
				checkdrive(iadrive[i], i);
	}
}

static void isctlrjabbering(struct ctlr *c, uint32_t cause)
{
	uint32_t now;

	now = ms();
	if (now > c->lastintr0) {
		c->intrs = 0;
		c->lastintr0 = now;
	}
	if (++c->intrs > Maxintrspertick) {
		iprint("sdiahci: %lu intrs per tick for no serviced "
		       "drive; cause %#lx mport %d\n",
		       c->intrs, cause, c->mport);
		c->intrs = 0;
	}
}

static void isdrivejabbering(struct drive *d)
{
	uint32_t now = ms();

	if (now > d->lastintr0) {
		d->intrs = 0;
		d->lastintr0 = now;
	}
	if (++d->intrs > Maxintrspertick) {
		iprint("sdiahci: %lu interrupts per tick for %s\n", d->intrs,
		       d->unit->sdperm.name);
		d->intrs = 0;
	}
}

static void iainterrupt(struct hw_trapframe *unused_hw_trapframe, void *a)
{
	int i;
	uint32_t cause, mask, p_is, h_is, pi;
	struct ctlr *c;
	struct drive *d;

	c = a;
	spin_lock_irqsave(&c->Lock);
	cause = ahci_hba_read32(c->hba, HBA_ISR);
	if (cause == 0) {
		isctlrjabbering(c, cause);
		// iprint("sdiahci: interrupt for no drive\n");
		spin_unlock_irqsave(&c->Lock);
		return;
	}
	for (i = 0; cause && i <= c->mport; i++) {
		mask = 1 << i;
		if ((cause & mask) == 0)
			continue;
		d = c->rawdrive + i;
		spin_lock_irqsave(&d->Lock);
		isdrivejabbering(d);
		p_is = ahci_port_read32(d->port, PORT_IS);
		pi = ahci_hba_read32(c->hba, HBA_PI);
		if (p_is && pi & mask)
			updatedrive(d);
		ahci_hba_write32(c->hba, HBA_ISR, mask);
		spin_unlock_irqsave(&d->Lock);

		cause &= ~mask;
	}
	if (cause) {
		isctlrjabbering(c, cause);
		iprint("sdiahci: intr cause unserviced: %#lx\n", cause);
	}
	spin_unlock_irqsave(&c->Lock);
}

/* checkdrive, called from satakproc, will prod the drive while we wait */
static void awaitspinup(struct drive *d)
{
	int ms;
	uint16_t s;
	char *name;

	spin_lock_irqsave(&d->Lock);
	if (d->unit == NULL || d->port == NULL) {
		panic("awaitspinup: NULL d->unit or d->port");
		spin_unlock_irqsave(&d->Lock);
		return;
	}
	name = (d->unit ? d->unit->sdperm.name : NULL);
	s = ahci_port_read32(d->port, PORT_SSTS);
	if (!(s & Devpresent)) { /* never going to be ready */
		printd("awaitspinup: %s absent, not waiting\n", name);
		spin_unlock_irqsave(&d->Lock);
		return;
	}

	for (ms = 20000; ms > 0; ms -= 50)
		switch (d->state) {
		case Dnull:
			/* absent; done */
			spin_unlock_irqsave(&d->Lock);
			printd("awaitspinup: %s in null state\n", name);
			return;
		case Dready:
		case Dnew:
			if (d->sectors || d->mediachange) {
				/* ready to use; done */
				spin_unlock_irqsave(&d->Lock);
				printd("awaitspinup: %s ready!\n", name);
				return;
			}
		/* fall through */
		default:
		case Dmissing: /* normal waiting states */
		case Dreset:
		case Doffline: /* transitional states */
		case Derror:
		case Dportreset:
			spin_unlock_irqsave(&d->Lock);
			asleep(50);
			spin_lock_irqsave(&d->Lock);
			break;
		}
	printd("awaitspinup: %s didn't spin up after 20 seconds\n", name);
	spin_unlock_irqsave(&d->Lock);
}

static int iaverify(struct sdunit *u)
{
	struct ctlr *c;
	struct drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	spin_lock_irqsave(&c->Lock);
	spin_lock_irqsave(&d->Lock);
	d->unit = u;
	spin_unlock_irqsave(&d->Lock);
	spin_unlock_irqsave(&c->Lock);
	checkdrive(d, d->driveno); /* c->d0 + d->driveno */

	/*
	 * hang around until disks are spun up and thus available as
	 * nvram, dos file systems, etc.  you wouldn't expect it, but
	 * the intel 330 ssd takes a while to `spin up'.
	 */
	awaitspinup(d);
	return 1;
}

static int iaenable(struct sdev *s)
{
	char name[32];
	struct ctlr *c;
	static int once;

	c = s->ctlr;
	spin_lock_irqsave(&c->Lock);
	if (!c->enabled) {
		if (once == 0) {
			once = 1;
			ktask("ahci", satakproc, 0);
		}
		if (c->ndrive == 0)
			panic("iaenable: zero s->ctlr->ndrive");
		pci_set_bus_master(c->pci);
		snprintf(name, sizeof(name), "%s (%s)", s->name, s->ifc->name);
		/*c->vector = intrenable(c->pci->intl, iainterrupt, c,
		 *c->pci->tbdf, name);*/
		/* what do we do about the arg? */
		register_irq(c->pci->irqline, iainterrupt, c,
		             pci_to_tbdf(c->pci));
		/* supposed to squelch leftover interrupts here. */
		ahcienable(c->hba);
		c->enabled = 1;
	}
	spin_unlock_irqsave(&c->Lock);
	return 1;
}

static int iadisable(struct sdev *s)
{
	char name[32];
	struct ctlr *c;

	c = s->ctlr;
	spin_lock_irqsave(&c->Lock);
	ahcidisable(c->hba);
	snprintf(name, sizeof(name), "%s (%s)", s->name, s->ifc->name);
	// TODO: what to do here?
	// intrdisable(c->vector);
	c->enabled = 0;
	spin_unlock_irqsave(&c->Lock);
	return 1;
}

static int iaonline(struct sdunit *unit)
{
	int r;
	struct ctlr *c;
	struct drive *d;

	c = unit->dev->ctlr;
	d = c->drive[unit->subno];
	r = 0;

	if ((d->portm.feat & Datapi) && d->mediachange) {
		r = scsionline(unit);
		if (r > 0)
			d->mediachange = 0;
		return r;
	}

	spin_lock_irqsave(&d->Lock);
	if (d->mediachange) {
		r = 2;
		d->mediachange = 0;
		/* devsd resets this after online is called; why? */
		unit->sectors = d->sectors;
		unit->secsize = 512; /* default size */
	} else if (d->state == Dready)
		r = 1;
	spin_unlock_irqsave(&d->Lock);
	return r;
}

/* returns locked list! */
static void *ahcibuild(struct drive *d, unsigned char *cmd, void *data, int n,
                       int64_t lba)
{
	void *cfis, *list, *prdt, *ctab;
	unsigned char acmd, dir, llba, c7;
	struct aportm *pm;
	uint32_t flags;
	static unsigned char tab[2][2] = {
	    {0xc8, 0x25},
	    {0xca, 0x35},
	};

	pm = &d->portm;
	dir = *cmd != 0x28;
	llba = pm->feat & Dllba ? 1 : 0;
	acmd = tab[dir][llba];
	qlock(&pm->ql);
	list = pm->list;
	ctab = pm->ctab;
	cfis = ctab;

	ahci_cfis_write8(cfis, 0, 0x27);
	ahci_cfis_write8(cfis, 1, 0x80);
	ahci_cfis_write8(cfis, 2, acmd);
	ahci_cfis_write8(cfis, 3, 0);

	ahci_cfis_write8(cfis, 4, lba);       /* sector			lba low
	                                         7:0 */
	ahci_cfis_write8(cfis, 5, lba >> 8);  /* cylinder low	lba mid	15:8 */
	ahci_cfis_write8(cfis, 6, lba >> 16); /* cylinder hi	lba hi	23:16 */
	c7 = Obs | 0x40;                      /* 0x40 == lba */
	if (llba == 0)
		c7 |= (lba >> 24) & 7;
	ahci_cfis_write8(cfis, 7, c7);

	ahci_cfis_write8(
	    cfis, 8,
	    lba >> 24); /* sector (exp)			lba	31:24 */
	ahci_cfis_write8(cfis, 9,
	                 lba >> 32); /* cylinder low (exp)	lba	39:32 */
	ahci_cfis_write8(
	    cfis, 10, lba >> 48);      /* cylinder hi (exp)		lba	48:40 */
	ahci_cfis_write8(cfis, 11, 0); /* features (exp); */

	ahci_cfis_write8(cfis, 12, n);      /* sector count */
	ahci_cfis_write8(cfis, 13, n >> 8); /* sector count (exp) */
	ahci_cfis_write8(cfis, 14, 0);      /* r */
	ahci_cfis_write8(cfis, 15, 0);      /* control */

	ahci_cfis_write8(cfis, 16, 0);
	ahci_cfis_write8(cfis, 17, 0);
	ahci_cfis_write8(cfis, 18, 0);
	ahci_cfis_write8(cfis, 19, 0);

	flags = 1 << 16 | Lpref | 0x5; /* Lpref ?? */
	if (dir == Write)
		flags |= Lwrite;
	ahci_list_write32(list, ALIST_FLAGS, flags);
	ahci_list_write32(list, ALIST_LEN, 0);
	ahci_list_write32(list, ALIST_CTAB, paddr_low32(ctab));
	ahci_list_write32(list, ALIST_CTABHI, paddr_high32(ctab));

	prdt = ctab + ACTAB_PRDT;
	ahci_prdt_write32(prdt, APRDT_DBA, paddr_low32(data));
	ahci_prdt_write32(prdt, APRDT_DBAHI, paddr_high32(data));
	if (d->unit == NULL)
		panic("ahcibuild: NULL d->unit");
	ahci_prdt_write32(prdt, APRDT_COUNT,
	                  1 << 31 | (d->unit->secsize * n - 2) | 1);

	return list;
}

static void *ahcibuildpkt(struct aportm *pm, struct sdreq *r, void *data, int n)
{
	int fill, len, i;
	void *cfis, *list, *ctab, *prdt;
	uint32_t flags;

	qlock(&pm->ql);
	list = pm->list;
	ctab = pm->ctab;
	cfis = ctab;

	fill = pm->feat & Datapi16 ? 16 : 12;
	if ((len = r->clen) > fill)
		len = fill;
	memmove(ctab + ACTAB_ATAPI, r->cmd, len);
	memset(ctab + ACTAB_ATAPI + len, 0, fill - len);

	ahci_cfis_write8(cfis, 0, 0x27);
	ahci_cfis_write8(cfis, 1, 0x80);
	ahci_cfis_write8(cfis, 2, 0xa0);
	if (n != 0)
		ahci_cfis_write8(cfis, 3, 1); /* dma */
	else
		ahci_cfis_write8(cfis, 3, 0); /* features (exp); */

	ahci_cfis_write8(cfis, 4, 0); /* sector			lba low	7:0 */
	ahci_cfis_write8(cfis, 5, n); /* cylinder low		lba mid	15:8 */
	ahci_cfis_write8(cfis, 6,
	                 n >> 8); /* cylinder hi		lba hi	23:16 */
	ahci_cfis_write8(cfis, 7, Obs);

	for (i = 0; i < 12; i++)
		ahci_cfis_write8(cfis, 8 + i, 0);

	flags = 1 << 16 | Lpref | Latapi | 0x5;
	if (r->write != 0 && data)
		flags |= Lwrite;
	ahci_list_write32(list, ALIST_FLAGS, flags);
	ahci_list_write32(list, ALIST_LEN, 0);
	ahci_list_write32(list, ALIST_CTAB, paddr_low32(ctab));
	ahci_list_write32(list, ALIST_CTABHI, paddr_high32(ctab));
	printd("ahci: %s: LIST->CTAB physical address=0x%08x:0x%08x\n",
	       __func__, paddr_high32(ctab), paddr_low32(ctab));

	if (data == 0)
		return list;

	prdt = ctab + ACTAB_PRDT;
	ahci_prdt_write32(prdt, APRDT_DBA, paddr_low32(data));
	ahci_prdt_write32(prdt, APRDT_DBAHI, paddr_high32(data));
	ahci_prdt_write32(prdt, APRDT_COUNT, 1 << 31 | (n - 2) | 1);
	printd("ahci: %s: PRDT->DBA  physical address=0x%08x:0x%08x\n",
	       __func__, paddr_high32(data), paddr_low32(data));

	return list;
}

static int waitready(struct drive *d)
{
	uint32_t s, i, delta;

	for (i = 0; i < 15000; i += 250) {
		if (d->state == Dreset || d->state == Dportreset ||
		    d->state == Dnew)
			return 1;
		delta = ms() - d->lastseen;
		if (d->state == Dnull || delta > 10 * 1000)
			return -1;
		spin_lock_irqsave(&d->Lock);
		s = ahci_port_read32(d->port, PORT_SSTS);
		spin_unlock_irqsave(&d->Lock);
		if ((s & Intpm) == 0 && delta > 1500)
			return -1; /* no detect */
		if (d->state == Dready &&
		    (s & Devdet) == (Devphycomm | Devpresent))
			return 0; /* ready, present & phy. comm. */
		esleep(250);
	}
	printd("%s: not responding; offline\n", d->unit->sdperm.name);
	setstate(d, Doffline);
	return -1;
}

static int lockready(struct drive *d)
{
	int i;

	qlock(&d->portm.ql);
	while ((i = waitready(d)) == 1) { /* could wait forever? */
		qunlock(&d->portm.ql);
		esleep(1);
		qlock(&d->portm.ql);
	}
	return i;
}

static int flushcache(struct drive *d)
{
	int i;

	i = -1;
	if (lockready(d) == 0)
		i = ahciflushcache(&d->portc);
	qunlock(&d->portm.ql);
	return i;
}

static int iariopkt(struct sdreq *r, struct drive *d)
{
	ERRSTACK(2);
	int n, count, try
		, max, flag, task, wormwrite;
	char *name;
	unsigned char *cmd, *data;
	void *port;
	struct Asleep as;

	cmd = r->cmd;
	name = d->unit->sdperm.name;
	port = d->port;

	aprintd("ahci: iariopkt: %04#x %04#x %c %d %p\n", cmd[0], cmd[2],
	        "rw"[r->write], r -> dlen, r -> data);
	if (cmd[0] == 0x5a && (cmd[2] & 0x3f) == 0x3f)
		return sdmodesense(r, cmd, d->info, d->infosz);
	r->rlen = 0;
	count = r->dlen;
	max = 65536;

	try
		= 0;
retry:
	data = r->data;
	n = count;
	if (n > max)
		n = max;
	ahcibuildpkt(&d->portm, r, data, n);
	switch (waitready(d)) {
	case -1:
		qunlock(&d->portm.ql);
		return SDeio;
	case 1:
		qunlock(&d->portm.ql);
		esleep(1);
		goto retry;
	}
	/* d->portm qlock held here */

	spin_lock_irqsave(&d->Lock);
	d->portm.flag = 0;
	spin_unlock_irqsave(&d->Lock);
	ahci_port_write32(port, PORT_CI, 1);

	as.p = port;
	as.i = 1;
	d->intick = ms();
	d->active++;

	while (waserror())
		poperror();
	/* don't sleep here forever */
	rendez_sleep_timeout(&d->portm.Rendez, ahciclear, &as,
	                     (3 * 1000) * 1000);
	poperror();
	if (!ahciclear(&as)) {
		qunlock(&d->portm.ql);
		printd("%s: ahciclear not true after 3 seconds\n", name);
		r->status = SDcheck;
		return SDcheck;
	}

	d->active--;
	spin_lock_irqsave(&d->Lock);
	flag = d->portm.flag;
	task = ahci_port_read32(port, PORT_TFD);
	spin_unlock_irqsave(&d->Lock);

	if ((task & (Efatal << 8)) ||
	    ((task & (ASbsy | ASdrq)) && (d->state == Dready))) {
		ahci_port_write32(port, PORT_CI, 0);
		ahcirecover(&d->portc);
		task = ahci_port_read32(port, PORT_TFD);
		flag &= ~Fdone; /* either an error or do-over */
	}
	qunlock(&d->portm.ql);
	if (flag == 0) {
		if (++try == 10) {
			printd("%s: bad disk\n", name);
			r->status = SDcheck;
			return SDcheck;
		}
		/*
		 * write retries cannot succeed on write-once media,
		 * so just accept any failure.
		 */
		wormwrite = 0;
		switch (d->unit->inquiry[0] & SDinq0periphtype) {
		case SDperworm:
		case SDpercd:
			switch (cmd[0]) {
			case 0x0a: /* write (6?) */
			case 0x2a: /* write (10) */
			case 0x8a: /* int32_t write (16) */
			case 0x2e: /* write and verify (10) */
				wormwrite = 1;
				break;
			}
			break;
		}
		if (!wormwrite) {
			printd("%s: retry\n", name);
			goto retry;
		}
	}
	if (flag & Ferror) {
		if ((task & Eidnf) == 0)
			printd("%s: i/o error task=%#x\n", name, task);
		r->status = SDcheck;
		return SDcheck;
	}

	data += n;

	r->rlen = data - (unsigned char *)r->data;
	r->status = SDok;
	return SDok;
}

static int iario(struct sdreq *r)
{
	ERRSTACK(1);
	int i, n, count, try
		, max, flag, task;
	uint64_t lba;
	char *name;
	unsigned char *cmd, *data;
	void *port;
	struct Asleep as;
	struct ctlr *c;
	struct drive *d;
	struct sdunit *unit;

	unit = r->unit;
	c = unit->dev->ctlr;
	d = c->drive[unit->subno];
	if (d->portm.feat & Datapi)
		return iariopkt(r, d);
	cmd = r->cmd;
	name = d->unit->sdperm.name;
	port = d->port;

	if (r->cmd[0] == 0x35 || r->cmd[0] == 0x91) {
		if (flushcache(d) == 0)
			return sdsetsense(r, SDok, 0, 0, 0);
		return sdsetsense(r, SDcheck, 3, 0xc, 2);
	}

	if ((i = sdfakescsi(r, d->info, d->infosz)) != SDnostatus) {
		r->status = i;
		return i;
	}

	if (*cmd != 0x28 && *cmd != 0x2a) {
		printd("%s: bad cmd %.2#x\n", name, cmd[0]);
		r->status = SDcheck;
		return SDcheck;
	}

	// TODO: make cmd bigger to support drives with >= 2 TiB capacity,
	// with 32 bits and 512 B blocks only 2^(9+32) = 2 TiB addressable
	lba = (uint32_t)(cmd[2] << 24) | cmd[3] << 16 | cmd[4] << 8 | cmd[5];
	count = cmd[7] << 8 | cmd[8];
	if (r->data == NULL)
		return SDok;
	if (r->dlen < count * unit->secsize)
		count = r->dlen / unit->secsize;
	max = 128;

	try
		= 0;
retry:
	data = r->data;
	while (count > 0) {
		n = count;
		if (n > max)
			n = max;
		ahcibuild(d, cmd, data, n, lba);
		switch (waitready(d)) {
		case -1:
			qunlock(&d->portm.ql);
			return SDeio;
		case 1:
			qunlock(&d->portm.ql);
			esleep(1);
			goto retry;
		}
		/* d->portm qlock held here */
		spin_lock_irqsave(&d->Lock);
		d->portm.flag = 0;
		spin_unlock_irqsave(&d->Lock);
		ahci_port_write32(port, PORT_CI, 1);

		as.p = port;
		as.i = 1;
		d->intick = ms();
		d->active++;

		while (waserror())
			poperror();
		/* don't sleep here forever */
		rendez_sleep_timeout(&d->portm.Rendez, ahciclear, &as,
		                     (3 * 1000) * 1000);
		poperror();
		if (!ahciclear(&as)) {
			qunlock(&d->portm.ql);
			printd("%s: ahciclear not true after 3 seconds\n",
			       name);
			r->status = SDcheck;
			return SDcheck;
		}

		d->active--;
		spin_lock_irqsave(&d->Lock);
		flag = d->portm.flag;
		task = ahci_port_read32(port, PORT_TFD);
		spin_unlock_irqsave(&d->Lock);

		if ((task & (Efatal << 8)) ||
		    ((task & (ASbsy | ASdrq)) && d->state == Dready)) {
			ahci_port_write32(port, PORT_CI, 0);
			ahcirecover(&d->portc);
			task = ahci_port_read32(port, PORT_TFD);
		}
		qunlock(&d->portm.ql);
		if (flag == 0) {
			if (++try == 10) {
				printd("%s: bad disk\n", name);
				r->status = SDeio;
				return SDeio;
			}
			printd("%s: retry blk %lld\n", name, lba);
			goto retry;
		}
		if (flag & Ferror) {
			printk("%s: i/o error task=%#x @%lld\n", name, task,
			       lba);
			r->status = SDeio;
			return SDeio;
		}

		count -= n;
		lba += n;
		data += n * unit->secsize;
	}
	r->rlen = data - (unsigned char *)r->data;
	r->status = SDok;
	return SDok;
}

/*
 * configure drives 0-5 as ahci sata (c.f. errata).
 * what about 6 & 7, as claimed by marvell 0x9123?
 */
static int iaahcimode(struct pci_device *p)
{
	printd("iaahcimode: %#x %#x %#x\n", pcidev_read8(p, 0x91),
	       pcidev_read8(p, 92), pcidev_read8(p, 93));
	pcidev_write16(p, 0x92, pcidev_read16(p, 0x92) | 0x3f); /* ports 0-5 */
	return 0;
}

static void iasetupahci(struct ctlr *c)
{
	void *p = c->mmio; /* This is actually a pointer to HBA */
	/* disable cmd block decoding. */
	pcidev_write16(c->pci, 0x40, pcidev_read16(c->pci, 0x40) & ~(1 << 15));
	pcidev_write16(c->pci, 0x42, pcidev_read16(c->pci, 0x42) & ~(1 << 15));

	ahci_hba_write32(p, HBA_GHC,
	                 1 << 31); /* enable ahci mode (ghc register) */
	ahci_hba_write32(p, HBA_PI,
	                 (1 << 6) - 1); /* 5 ports. (supposedly ro pi reg.) */

	/* enable ahci mode and 6 ports; from ich9 datasheet */
	pcidev_write16(c->pci, 0x90, 1 << 6 | 1 << 5);
}

static int didtype(struct pci_device *p)
{
	switch (p->ven_id) {
	case Vintel:
		if ((p->dev_id & 0xfffc) == 0x2680)
			return Tesb;
		/*
		 * 0x27c4 is the intel 82801 in compatibility (not sata) mode.
		 */
		if (p->dev_id == 0x1e02 ||            /* c210 */
		    p->dev_id == 0x24d1 ||            /* 82801eb/er */
		    (p->dev_id & 0xfffb) == 0x27c1 || /* 82801g[bh]m ich7 */
		    p->dev_id == 0x2821 ||            /* 82801h[roh] */
		    (p->dev_id & 0xfffe) == 0x2824 || /* 82801h[b] */
		    (p->dev_id & 0xfeff) == 0x2829 || /* ich8/9m */
		    (p->dev_id & 0xfffe) == 0x2922 || /* ich9 */
		    p->dev_id == 0x3a02 ||            /* 82801jd/do */
		    (p->dev_id & 0xfefe) == 0x3a22 || /* ich10, pch */
		    (p->dev_id & 0xfff8) == 0x3b28 || /* pchm */
		    p->dev_id == 0x1d02)              /* c600/x79 pch */
			return Tich;
		break;
	case Vatiamd:
		if (p->dev_id == 0x4380 || p->dev_id == 0x4390 ||
		    p->dev_id == 0x4391) {
			printd("detected sb600 vid %#x did %#x\n", p->ven_id,
			       p->dev_id);
			return Tsb600;
		}
		break;
	case Vmarvell:
		if (p->dev_id == 0x9123)
			printk("ahci: marvell sata 3 controller has delusions "
			       "of something on unit 7\n");
		break;
	}
	if (p->class == Pcibcstore && p->subclass == Pciscsata &&
	    p->progif == 1) {
		printd("ahci: Tunk: vid %#4.4x did %#4.4x\n", p->ven_id,
		       p->dev_id);
		return Tunk;
	}
	return -1;
}

static int newctlr(struct ctlr *ctlr, struct sdev *sdev, int nunit)
{
	int i, n;
	struct drive *drive;
	uint32_t h_cap, pi;

	ctlr->ndrive = sdev->nunit = nunit;
	h_cap = ahci_hba_read32(ctlr->hba, HBA_CAP);
	printd("ahci: %s: HBA_CAP=0x%08x\n", __func__, h_cap);
	ctlr->mport = h_cap & ((1 << 5) - 1);

	i = (h_cap >> 20) & ((1 << 4) - 1); /* iss */
	printk("#S/sd%c: %s: %#p %s, %d ports, irq %d\n", sdev->idno,
	       Tname(ctlr), ctlr->physio, descmode[i], nunit,
	       ctlr->pci->irqline);
	/* map the drives -- they don't all need to be enabled. */
	n = 0;
	ctlr->rawdrive = kzmalloc(NCtlrdrv * sizeof(struct drive), 0);
	if (ctlr->rawdrive == NULL) {
		printd("ahci: out of memory\n");
		return -1;
	}
	pi = ahci_hba_read32(ctlr->hba, HBA_PI);
	for (i = 0; i < NCtlrdrv; i++) {
		drive = ctlr->rawdrive + i;
		spinlock_init_irqsave(&drive->Lock);
		drive->portno = i;
		drive->driveno = -1;
		drive->sectors = 0;
		drive->serial[0] = ' ';
		drive->ctlr = ctlr;
		if ((pi & (1 << i)) == 0)
			continue;
		drive->port = ctlr->mmio + 0x80 * i + 0x100;
		drive->portc.p = drive->port;
		drive->portc.pm = &drive->portm;
		qlock_init(&drive->portm.ql);
		rendez_init(&drive->portm.Rendez);
		drive->driveno = n++;
		ctlr->drive[drive->driveno] = drive;
		iadrive[niadrive + drive->driveno] = drive;
	}
	for (i = 0; i < n; i++)
		if (ahciidle(ctlr->drive[i]->port) == -1) {
			printd("ahci: %s: port %d wedged; abort\n", Tname(ctlr),
			       i);
			return -1;
		}
	for (i = 0; i < n; i++) {
		ctlr->drive[i]->mode = DMsatai;
		configdrive(ctlr->drive[i]);
	}
	return n;
}

static void releasedrive(struct kref *kref)
{
	printk("release drive called, but we don't do that yet\n");
}

static struct sdev *iapnp(void)
{
	int n, nunit, type;
	struct ctlr *c;
	struct pci_device *p;
	struct sdev *head, *tail, *s;

	// TODO: ensure we're only called once.

	memset(olds, 0xff, sizeof olds);
	p = NULL;
	head = tail = NULL;
	STAILQ_FOREACH (p, &pci_devices, all_dev) {
		type = didtype(p);
		if (type == -1)
			continue;
		printd("ahci: %s: ven_id=0x%04x, dev_id=0x%04x, didtype=%d\n",
		       __func__, p->ven_id, p->dev_id, type);
		if (p->bar[Abar].mmio_base32 == 0)
			continue;
		if (niactlr == NCtlr) {
			printk("ahci: iapnp: %s: too many controllers\n",
			       tname[type]);
			break;
		}
		c = iactlr + niactlr;
		s = sdevs + niactlr;
		memset(c, 0, sizeof *c);
		memset(s, 0, sizeof *s);
		kref_init(&s->r, releasedrive, 1);
		qlock_init(&s->ql);
		qlock_init(&s->unitlock);
		c->physio = p->bar[Abar].mmio_base32 & ~0xf;
		c->mmio =
		    (void *)vmap_pmem_nocache(c->physio, p->bar[Abar].mmio_sz);
		spinlock_init_irqsave(&c->Lock);
		if (c->mmio == 0) {
			printk("ahci: %s: address %#lX in use did=%#x\n",
			       Tname(c), c->physio, p->dev_id);
			continue;
		}
		printk("sdiahci %s: Mapped %p/%d to %p\n", tname[type],
		       c->physio, p->bar[Abar].mmio_sz, c->mmio);
		c->pci = p;
		c->type = type;

		s->ifc = &sdiahciifc;
		s->idno = 'E' + niactlr;
		s->ctlr = c;
		c->sdev = s;

		if (Intel(c) && p->dev_id != 0x2681)
			iasetupahci(c);
		nunit = ahciconf(c);
		// ahcihbareset((void *)c->mmio);
		if (Intel(c) && iaahcimode(p) == -1)
			break;
		if (nunit < 1) {
			vunmap_vmem((uintptr_t)c->mmio, p->bar[Abar].mmio_sz);
			continue;
		}
		n = newctlr(c, s, nunit);
		if (n < 0)
			continue;
		niadrive += n;
		niactlr++;
		if (head)
			tail->next = s;
		else
			head = s;
		tail = s;
	}
	return head;
}

static char *smarttab[] = {"unset", "error", "threshold exceeded", "normal"};

static char *pflag(char *s, char *e, unsigned char f)
{
	unsigned char i;

	for (i = 0; i < 8; i++)
		if (f & (1 << i))
			s = seprintf(s, e, "%s ", flagname[i]);
	return seprintf(s, e, "\n");
}

static int iarctl(struct sdunit *u, char *p, int l)
{
	char buf[32];
	char *e, *op;
	void *port;
	struct ctlr *c;
	struct drive *d;
	uint32_t serror, task, cmd, ci, is, sig, sstatus;

	c = u->dev->ctlr;
	if (c == NULL) {
		printk("iarctl: nil u->dev->ctlr\n");
		return 0;
	}
	d = c->drive[u->subno];
	port = d->port;

	e = p + l;
	op = p;
	if (d->state == Dready) {
		p = seprintf(p, e, "model\t%s\n", d->model);
		p = seprintf(p, e, "serial\t%s\n", d->serial);
		p = seprintf(p, e, "firm\t%s\n", d->firmware);
		if (d->smartrs == 0xff)
			p = seprintf(p, e, "smart\tenable error\n");
		else if (d->smartrs == 0)
			p = seprintf(p, e, "smart\tdisabled\n");
		else
			p = seprintf(p, e, "smart\t%s\n",
			             smarttab[d->portm.smart]);
		p = seprintf(p, e, "flag\t");
		p = pflag(p, e, d->portm.feat);
	} else
		p = seprintf(p, e, "no disk present [%s]\n",
		             diskstates[d->state]);
	serror = ahci_port_read32(port, PORT_SERR);
	task = ahci_port_read32(port, PORT_TFD);
	cmd = ahci_port_read32(port, PORT_CMD);
	ci = ahci_port_read32(port, PORT_CI);
	is = ahci_port_read32(port, PORT_IS);
	sig = ahci_port_read32(port, PORT_SIG);
	sstatus = ahci_port_read32(port, PORT_SSTS);
	serrstr(serror, buf, buf + sizeof(buf) - 1);
	p = seprintf(p, e,
	             "reg\ttask %#lx cmd %#lx serr %#lx %s ci %#lx is %#lx; "
	             "sig %#lx sstatus %06#lx\n",
	             task, cmd, serror, buf, ci, is, sig, sstatus);
	if (d->unit == NULL)
		panic("iarctl: nil d->unit");
	p = seprintf(p, e, "geometry %llu %lu\n", d->sectors, d->unit->secsize);
	return p - op;
}

static void runflushcache(struct drive *d)
{
	int32_t t0;

	t0 = ms();
	if (flushcache(d) != 0)
		error(EIO, "Flush cache failed");
	printd("ahci: flush in %ld ms\n", ms() - t0);
}

static void forcemode(struct drive *d, char *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(modename); i++)
		if (strcmp(mode, modename[i]) == 0)
			break;
	if (i == ARRAY_SIZE(modename))
		i = 0;
	spin_lock_irqsave(&d->Lock);
	d->mode = i;
	spin_unlock_irqsave(&d->Lock);
}

static void runsmartable(struct drive *d, int i)
{
	ERRSTACK(1);

	if (waserror()) {
		qunlock(&d->portm.ql);
		d->smartrs = 0;
		nexterror();
	}
	if (lockready(d) == -1)
		error(EIO, "runsmartable: lockready returned -1");
	d->smartrs = smart(&d->portc, i);
	d->portm.smart = 0;
	qunlock(&d->portm.ql);
	poperror();
}

static void forcestate(struct drive *d, char *state)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(diskstates); i++)
		if (strcmp(state, diskstates[i]) == 0)
			break;
	if (i == ARRAY_SIZE(diskstates))
		error(EINVAL, "Can't set state to invalid value '%s'", state);
	setstate(d, i);
}

/*
 * force this driver to notice a change of medium if the hardware doesn't
 * report it.
 */
static void changemedia(struct sdunit *u)
{
	struct ctlr *c;
	struct drive *d;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	spin_lock_irqsave(&d->Lock);
	d->mediachange = 1;
	u->sectors = 0;
	spin_unlock_irqsave(&d->Lock);
}

static int iawctl(struct sdunit *u, struct cmdbuf *cmd)
{
	ERRSTACK(1);
	char **f;
	struct ctlr *c;
	struct drive *d;
	unsigned int i;

	c = u->dev->ctlr;
	d = c->drive[u->subno];
	f = cmd->f;

	if (strcmp(f[0], "change") == 0)
		changemedia(u);
	else if (strcmp(f[0], "flushcache") == 0)
		runflushcache(d);
	else if (strcmp(f[0], "identify") == 0) {
		i = strtoul(f[1] ? f[1] : "0", 0, 0);
		if (i > 0xff)
			i = 0;
		printd("ahci: %04d %#x\n", i, d->info[i]);
	} else if (strcmp(f[0], "mode") == 0)
		forcemode(d, f[1] ? f[1] : "satai");
	else if (strcmp(f[0], "nop") == 0) {
		if ((d->portm.feat & Dnop) == 0) {
			sdierror(cmd, "no drive support");
			return -1;
		}
		if (waserror()) {
			qunlock(&d->portm.ql);
			nexterror();
		}
		if (lockready(d) == -1)
			error(EIO, "%s: lockready returned -1", __func__);
		nop(&d->portc);
		qunlock(&d->portm.ql);
		poperror();
	} else if (strcmp(f[0], "reset") == 0)
		forcestate(d, "reset");
	else if (strcmp(f[0], "smart") == 0) {
		if (d->smartrs == 0)
			sdierror(cmd, "smart not enabled");
		if (waserror()) {
			qunlock(&d->portm.ql);
			d->smartrs = 0;
			nexterror();
		}
		if (lockready(d) == -1)
			error(EIO, "%s: lockready returned -1", __func__);
		d->portm.smart = 2 + smartrs(&d->portc);
		qunlock(&d->portm.ql);
		poperror();
	} else if (strcmp(f[0], "smartdisable") == 0)
		runsmartable(d, 1);
	else if (strcmp(f[0], "smartenable") == 0)
		runsmartable(d, 0);
	else if (strcmp(f[0], "state") == 0)
		forcestate(d, f[1] ? f[1] : "null");
	else {
		sdierror(cmd, "%s: unknown control '%s'", __func__, f[0]);
		return -1;
	}
	return 0;
}

static char *portr(char *p, char *e, unsigned int x)
{
	int i, a;

	p[0] = 0;
	a = -1;
	for (i = 0; i < 32; i++) {
		if ((x & (1 << i)) == 0) {
			if (a != -1 && i - 1 != a)
				p = seprintf(p, e, "-%d", i - 1);
			a = -1;
			continue;
		}
		if (a == -1) {
			if (i > 0)
				p = seprintf(p, e, ", ");
			p = seprintf(p, e, "%d", a = i);
		}
	}
	if (a != -1 && i - 1 != a)
		p = seprintf(p, e, "-%d", i - 1);
	return p;
}

/* must emit exactly one line per controller (sd(3)) */
static char *iartopctl(struct sdev *sdev, char *p, char *e)
{
	uint32_t cap, ghc, isr, pi, ver;
	char pr[25];
	void *hba;
	struct ctlr *ctlr;

#define has(x, str)                                                            \
	do {                                                                   \
		if (cap & (x))                                                 \
			p = seprintf(p, e, "%s ", (str));                      \
	} while (0)

	ctlr = sdev->ctlr;
	hba = ctlr->hba;
	p = seprintf(p, e, "sd%c ahci port %#p: ", sdev->idno, ctlr->physio);
	cap = ahci_hba_read32(hba, HBA_CAP);
	has(Hs64a, "64a");
	has(Hsalp, "alp");
	has(Hsam, "am");
	has(Hsclo, "clo");
	has(Hcccs, "coal");
	has(Hems, "ems");
	has(Hsal, "led");
	has(Hsmps, "mps");
	has(Hsncq, "ncq");
	has(Hssntf, "ntf");
	has(Hspm, "pm");
	has(Hpsc, "pslum");
	has(Hssc, "slum");
	has(Hsss, "ss");
	has(Hsxs, "sxs");
	pi = ahci_hba_read32(hba, HBA_PI);
	portr(pr, pr + sizeof(pr), pi);

	ghc = ahci_hba_read32(hba, HBA_GHC);
	isr = ahci_hba_read32(hba, HBA_ISR);
	ver = ahci_hba_read32(hba, HBA_VS);
	return seprintf(
	    p, e,
	    "iss %ld ncs %ld np %ld; ghc %#lx isr %#lx pi %#lx %s ver %#lx\n",
	    (cap >> 20) & 0xf, (cap >> 8) & 0x1f, 1 + (cap & 0x1f), ghc, isr,
	    pi, ver);
#undef has
}

static int iawtopctl(struct sdev *sdev, struct cmdbuf *cmd)
{
	int *v;
	char **f;

	f = cmd->f;
	v = 0;

	if (f[0] == NULL)
		return 0;
	if (strcmp(f[0], "debug") == 0)
		v = &debug;
	else if (strcmp(f[0], "iprintd") == 0)
		v = &prid;
	else if (strcmp(f[0], "aprint") == 0)
		v = &datapi;
	else
		sdierror(cmd, "%s: bad control '%s'", __func__, f[0]);

	switch (cmd->nf) {
	default:
		sdierror(cmd, "%s: %d args, only 1 or 2 allowed", __func__,
		         cmd->nf);
	case 1:
		*v ^= 1;
		break;
	case 2:
		if (f[1])
			*v = strcmp(f[1], "on") == 0;
		else
			*v ^= 1;
		break;
	}
	return 0;
}

struct sdifc sdiahciifc = {
    "iahci",

    iapnp,     NULL, /* legacy */
    iaenable,  iadisable,

    iaverify,  iaonline,  iario, iarctl, iawctl,

    scsibio,   NULL, /* probe */
    NULL,            /* clear */
    iartopctl, iawtopctl,
};
