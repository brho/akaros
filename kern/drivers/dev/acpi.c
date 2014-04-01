/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>
#include <acpi.h>
#ifdef CONFIG_X86
#include <arch/pci.h>
#endif

/*
 * ACPI 4.0 Support.
 * Still WIP.
 *
 * This driver locates tables and parses only the FADT
 * and the XSDT. All other tables are mapped and kept there
 * for the user-level interpreter.
 */

#define l16get(p)	(((p)[1]<<8)|(p)[0])
#define l32get(p)	(((uint32_t)l16get(p+2)<<16)|l16get(p))
static struct Atable *acpifadt(uint8_t *, int);
static struct Atable *acpitable(uint8_t *, int);
static struct Atable *acpimadt(uint8_t *, int);
static struct Atable *acpimsct(uint8_t *, int);
static struct Atable *acpisrat(uint8_t *, int);
static struct Atable *acpislit(uint8_t *, int);

static struct cmdtab ctls[] = {
	{CMregion, "region", 6},
	{CMgpe, "gpe", 3},
};

static struct dirtab acpidir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"acpictl", {Qctl}, 0, 0666},
	{"acpitbl", {Qtbl}, 0, 0444},
	{"acpiregio", {Qio}, 0, 0666},
	{"acpipretty", {Qpretty}, 0, 0444},
	{"ioapic", {Qioapic}, 0, 0444},
	{"apic", {Qapic}, 0, 0444},
};

/*
 * The DSDT is always given to the user interpreter.
 * Tables listed here are also loaded from the XSDT:
 * MSCT, MADT, and FADT are processed by us, because they are
 * required to do early initialization before we have user processes.
 * Other tables are given to the user level interpreter for
 * execution.
 */
static struct Parse ptables[] = {
	{"FACP", acpifadt},
	{"APIC", acpimadt,},
	{"SRAT", acpisrat,},
	{"SLIT", acpislit,},
	{"MSCT", acpimsct,},
	{"SSDT", acpitable,},
};

static struct Facs *facs;		/* Firmware ACPI control structure */
static struct Fadt fadt;		/* Fixed ACPI description. To reach ACPI registers */
static struct Xsdt *xsdt;		/* XSDT table */
static struct Atable *tfirst;	/* loaded DSDT/SSDT/... tables */
static struct Atable *tlast;	/* pointer to last table */
struct Madt *apics;				/* APIC info */
static struct Srat *srat;		/* System resource affinity, used by physalloc */
static struct Slit *slit;		/* System locality information table used by the scheduler */
static struct Msct *msct;		/* Maximum system characteristics table */
static struct Reg *reg;			/* region used for I/O */
static struct Gpe *gpes;		/* General purpose events */
static int ngpes;

static char *regnames[] = {
	"mem", "io", "pcicfg", "embed",
	"smb", "cmos", "pcibar",
};

static char *dumpGas(char *start, char *end, char *prefix, struct Gas *g);

static char *acpiregstr(int id)
{
	static char buf[20];		/* BUG */

	if (id >= 0 && id < ARRAY_SIZE(regnames)) {
		return regnames[id];
	}
	seprintf(buf, buf + sizeof(buf), "spc:%#x", id);
	return buf;
}

static int acpiregid(char *s)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(regnames); i++)
		if (strcmp(regnames[i], s) == 0) {
			return i;
		}
	return -1;
}

static uint8_t mget8(uintptr_t p, void *unused)
{
	uint8_t *cp = (uint8_t *) p;
	return *cp;
}

static void mset8(uintptr_t p, uint8_t v, void *unused)
{
	uint8_t *cp = (uint8_t *) p;
	*cp = v;
}

static uint16_t mget16(uintptr_t p, void *unused)
{
	uint16_t *cp = (uint16_t *) p;
	return *cp;
}

static void mset16(uintptr_t p, uint16_t v, void *unused)
{
	uint16_t *cp = (uint16_t *) p;
	*cp = v;
}

static uint32_t mget32(uintptr_t p, void *unused)
{
	uint32_t *cp = (uint32_t *) p;
	return *cp;
}

static void mset32(uintptr_t p, uint32_t v, void *unused)
{
	uint32_t *cp = (uint32_t *) p;
	*cp = v;
}

static uint64_t mget64(uintptr_t p, void *unused)
{
	uint64_t *cp = (uint64_t *) p;
	return *cp;
}

static void mset64(uintptr_t p, uint64_t v, void *unused)
{
	uint64_t *cp = (uint64_t *) p;
	*cp = v;
}

static uint8_t ioget8(uintptr_t p, void *unused)
{
	return inb(p);
}

static void ioset8(uintptr_t p, uint8_t v, void *unused)
{
	outb(p, v);
}

static uint16_t ioget16(uintptr_t p, void *unused)
{
	return inw(p);
}

static void ioset16(uintptr_t p, uint16_t v, void *unused)
{
	outw(p, v);
}

static uint32_t ioget32(uintptr_t p, void *unused)
{
	return inl(p);
}

static void ioset32(uintptr_t p, uint32_t v, void *unused)
{
	outl(p, v);
}

/* TODO: these cfgs are hacky.	maybe all the struct Reg should have struct
 * pci_device or something? */
static uint8_t cfgget8(uintptr_t p, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read8(&pcidev, p);
}

static void cfgset8(uintptr_t p, uint8_t v, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write8(&pcidev, p, v);
}

static uint16_t cfgget16(uintptr_t p, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read16(&pcidev, p);
}

static void cfgset16(uintptr_t p, uint16_t v, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write16(&pcidev, p, v);
}

static uint32_t cfgget32(uintptr_t p, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read32(&pcidev, p);
}

static void cfgset32(uintptr_t p, uint32_t v, void *r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write32(&pcidev, p, v);
}

static struct Regio memio = {
	NULL,
	mget8, mset8, mget16, mset16,
	mget32, mset32, mget64, mset64
};

static struct Regio ioio = {
	NULL,
	ioget8, ioset8, ioget16, ioset16,
	ioget32, ioset32, NULL, NULL
};

static struct Regio cfgio = {
	NULL,
	cfgget8, cfgset8, cfgget16, cfgset16,
	cfgget32, cfgset32, NULL, NULL
};

/*
 * Copy memory, 1/2/4/8-bytes at a time, to/from a region.
 */
static long
regcpy(struct Regio *dio, uintptr_t da, struct Regio *sio,
	   uintptr_t sa, long len, int align)
{
	int n, i;

	printd("regcpy %#p %#p %#p %#p\n", da, sa, len, align);
	if ((len % align) != 0)
		printd("regcpy: bug: copy not aligned. truncated\n");
	n = len / align;
	for (i = 0; i < n; i++) {
		switch (align) {
			case 1:
				printd("cpy8 %#p %#p\n", da, sa);
				dio->set8(da, sio->get8(sa, sio->arg), dio->arg);
				break;
			case 2:
				printd("cpy16 %#p %#p\n", da, sa);
				dio->set16(da, sio->get16(sa, sio->arg), dio->arg);
				break;
			case 4:
				printd("cpy32 %#p %#p\n", da, sa);
				dio->set32(da, sio->get32(sa, sio->arg), dio->arg);
				break;
			case 8:
				printd("cpy64 %#p %#p\n", da, sa);
				warn("Not doing set64 for some reason, fix me!");
				//  dio->set64(da, sio->get64(sa, sio->arg), dio->arg);
				break;
			default:
				panic("regcpy: align bug");
		}
		da += align;
		sa += align;
	}
	return n * align;
}

// until we know.  TODO: spatch/find_replace this shit, same with mp.c
//#define vmap(x,y) (void *)vmap_pmem((x),(y))
//#define vunmap(x,y) vunmap_vmem((uintptr_t)(x),(y))
#define vmap(x,y) ((void*)(x + KERNBASE))
#define vunmap(x,y)
/*
 * Perform I/O within region in access units of accsz bytes.
 * All units in bytes.
 */
static long regio(struct Reg *r, void *p, uint32_t len, uintptr_t off, int iswr)
{
	struct Regio rio;
	uintptr_t rp;

	printd("reg%s %s %#p %#p %#lx sz=%d\n",
		   iswr ? "out" : "in", r->name, p, off, len, r->accsz);
	rp = 0;
	if (off + len > r->len) {
		printd("regio: access outside limits");
		len = r->len - off;
	}
	if (len <= 0) {
		printd("regio: zero len\n");
		return 0;
	}
	switch (r->spc) {
		case Rsysmem:
			if (r->p == NULL)
				r->p = vmap(r->base, len);
			if (r->p == NULL)
				error("regio: vmap failed");
			rp = (uintptr_t) r->p + off;
			rio = memio;
			break;
		case Rsysio:
			rp = r->base + off;
			rio = ioio;
			break;
		case Rpcicfg:
			rp = r->base + off;
			rio = cfgio;
			rio.arg = r;
			break;
		case Rpcibar:
		case Rembed:
		case Rsmbus:
		case Rcmos:
		case Ripmi:
		case Rfixedhw:
			printd("regio: reg %s not supported\n", acpiregstr(r->spc));
			error("region not supported");
	}
	if (iswr)
		regcpy(&rio, rp, &memio, (uintptr_t) p, len, r->accsz);
	else
		regcpy(&memio, (uintptr_t) p, &rio, rp, len, r->accsz);
	return len;
}

static struct Atable *newtable(uint8_t * p)
{
	struct Atable *t;
	struct Sdthdr *h;

	t = kzmalloc(sizeof(struct Atable), 0);
	if (t == NULL)
		panic("no memory for more aml tables");
	t->tbl = p;
	h = (struct Sdthdr *)t->tbl;
	t->is64 = h->rev >= 2;
	t->dlen = l32get(h->length) - Sdthdrsz;
	memmove(t->sig, h->sig, sizeof(h->sig));
	t->sig[sizeof(t->sig) - 1] = 0;
	memmove(t->oemid, h->oemid, sizeof(h->oemid));
	t->oemtblid[sizeof(t->oemtblid) - 1] = 0;
	memmove(t->oemtblid, h->oemtblid, sizeof(h->oemtblid));
	t->oemtblid[sizeof(t->oemtblid) - 1] = 0;
	t->next = NULL;
	if (tfirst == NULL)
		tfirst = tlast = t;
	else {
		tlast->next = t;
		tlast = t;
	}
	return t;
}

static void *sdtchecksum(void *addr, int len)
{
	uint8_t *p, sum;

	sum = 0;
	for (p = addr; len-- > 0; p++)
		sum += *p;
	if (sum == 0) {
		return addr;
	}

	return NULL;
}

static void *sdtmap(uintptr_t pa, int *n, int cksum)
{
	struct Sdthdr *sdt;

	if (!pa) {
		printk("sdtmap: NULL pa\n");
		return NULL;
	}
	sdt = vmap(pa, sizeof(*sdt));
	if (sdt == NULL) {
		printk("acpi: vmap1: NULL\n");
		return NULL;
	}
	*n = l32get(sdt->length);
	vunmap(sdt, sizeof(*sdt));
	if (!*n) {
		printk("sdt has zero length!\n");
		return NULL;
	}
	if ((sdt = vmap(pa, *n)) == NULL) {
		printk("acpi: NULL vmap\n");
		return NULL;
	}
	if (cksum != 0 && sdtchecksum(sdt, *n) == NULL) {
		printk("acpi: SDT: bad checksum\n");
		vunmap(sdt, sizeof(*sdt));
		return NULL;
	}
	return sdt;
}

static int loadfacs(uintptr_t pa)
{
	int n;

	facs = sdtmap(pa, &n, 0);
	if (facs == NULL) {
		return -1;
	}
	if (memcmp(facs, "FACS", 4) != 0) {
		//vunmap(facs, n);
		facs = NULL;
		return -1;
	}
	/* no unmap */

	printd("acpi: facs: hwsig: %#p\n", facs->hwsig);
	printd("acpi: facs: wakingv: %#p\n", facs->wakingv);
	printd("acpi: facs: flags: %#p\n", facs->flags);
	printd("acpi: facs: glock: %#p\n", facs->glock);
	printd("acpi: facs: xwakingv: %#p\n", facs->xwakingv);
	printd("acpi: facs: vers: %#p\n", facs->vers);
	printd("acpi: facs: ospmflags: %#p\n", facs->ospmflags);
	return 0;
}

static void loaddsdt(uintptr_t pa)
{
	int n;
	uint8_t *dsdtp;

	dsdtp = sdtmap(pa, &n, 1);
	if (dsdtp == NULL) {
		return;
	}
	if (acpitable(dsdtp, n) == NULL) ;	//vunmap(dsdtp, n);
}

static void gasget(struct Gas *gas, uint8_t * p)
{
	gas->spc = p[0];
	gas->len = p[1];
	gas->off = p[2];
	gas->accsz = p[3];
	gas->addr = l64get(p + 4);
}

static char *dumpfadt(char *start, char *end, struct Fadt *fp)
{
	if (2 == 0) {
		return NULL;
	}

	start = seprintf(start, end, "acpi: fadt: facs: $%p\n", fp->facs);
	start = seprintf(start, end, "acpi: fadt: dsdt: $%p\n", fp->dsdt);
	start = seprintf(start, end, "acpi: fadt: pmprofile: $%p\n", fp->pmprofile);
	start = seprintf(start, end, "acpi: fadt: sciint: $%p\n", fp->sciint);
	start = seprintf(start, end, "acpi: fadt: smicmd: $%p\n", fp->smicmd);
	start =
		seprintf(start, end, "acpi: fadt: acpienable: $%p\n", fp->acpienable);
	start =
		seprintf(start, end, "acpi: fadt: acpidisable: $%p\n", fp->acpidisable);
	start = seprintf(start, end, "acpi: fadt: s4biosreq: $%p\n", fp->s4biosreq);
	start = seprintf(start, end, "acpi: fadt: pstatecnt: $%p\n", fp->pstatecnt);
	start =
		seprintf(start, end, "acpi: fadt: pm1aevtblk: $%p\n", fp->pm1aevtblk);
	start =
		seprintf(start, end, "acpi: fadt: pm1bevtblk: $%p\n", fp->pm1bevtblk);
	start =
		seprintf(start, end, "acpi: fadt: pm1acntblk: $%p\n", fp->pm1acntblk);
	start =
		seprintf(start, end, "acpi: fadt: pm1bcntblk: $%p\n", fp->pm1bcntblk);
	start = seprintf(start, end, "acpi: fadt: pm2cntblk: $%p\n", fp->pm2cntblk);
	start = seprintf(start, end, "acpi: fadt: pmtmrblk: $%p\n", fp->pmtmrblk);
	start = seprintf(start, end, "acpi: fadt: gpe0blk: $%p\n", fp->gpe0blk);
	start = seprintf(start, end, "acpi: fadt: gpe1blk: $%p\n", fp->gpe1blk);
	start = seprintf(start, end, "acpi: fadt: pm1evtlen: $%p\n", fp->pm1evtlen);
	start = seprintf(start, end, "acpi: fadt: pm1cntlen: $%p\n", fp->pm1cntlen);
	start = seprintf(start, end, "acpi: fadt: pm2cntlen: $%p\n", fp->pm2cntlen);
	start = seprintf(start, end, "acpi: fadt: pmtmrlen: $%p\n", fp->pmtmrlen);
	start =
		seprintf(start, end, "acpi: fadt: gpe0blklen: $%p\n", fp->gpe0blklen);
	start =
		seprintf(start, end, "acpi: fadt: gpe1blklen: $%p\n", fp->gpe1blklen);
	start = seprintf(start, end, "acpi: fadt: gp1base: $%p\n", fp->gp1base);
	start = seprintf(start, end, "acpi: fadt: cstcnt: $%p\n", fp->cstcnt);
	start = seprintf(start, end, "acpi: fadt: plvl2lat: $%p\n", fp->plvl2lat);
	start = seprintf(start, end, "acpi: fadt: plvl3lat: $%p\n", fp->plvl3lat);
	start = seprintf(start, end, "acpi: fadt: flushsz: $%p\n", fp->flushsz);
	start =
		seprintf(start, end, "acpi: fadt: flushstride: $%p\n", fp->flushstride);
	start = seprintf(start, end, "acpi: fadt: dutyoff: $%p\n", fp->dutyoff);
	start = seprintf(start, end, "acpi: fadt: dutywidth: $%p\n", fp->dutywidth);
	start = seprintf(start, end, "acpi: fadt: dayalrm: $%p\n", fp->dayalrm);
	start = seprintf(start, end, "acpi: fadt: monalrm: $%p\n", fp->monalrm);
	start = seprintf(start, end, "acpi: fadt: century: $%p\n", fp->century);
	start =
		seprintf(start, end, "acpi: fadt: iapcbootarch: $%p\n",
				 fp->iapcbootarch);
	start = seprintf(start, end, "acpi: fadt: flags: $%p\n", fp->flags);
	start = dumpGas(start, end, "acpi: fadt: resetreg: ", &fp->resetreg);
	start = seprintf(start, end, "acpi: fadt: resetval: $%p\n", fp->resetval);
	start = seprintf(start, end, "acpi: fadt: xfacs: %p\n", fp->xfacs);
	start = seprintf(start, end, "acpi: fadt: xdsdt: %p\n", fp->xdsdt);
	start = dumpGas(start, end, "acpi: fadt: xpm1aevtblk:", &fp->xpm1aevtblk);
	start = dumpGas(start, end, "acpi: fadt: xpm1bevtblk:", &fp->xpm1bevtblk);
	start = dumpGas(start, end, "acpi: fadt: xpm1acntblk:", &fp->xpm1acntblk);
	start = dumpGas(start, end, "acpi: fadt: xpm1bcntblk:", &fp->xpm1bcntblk);
	start = dumpGas(start, end, "acpi: fadt: xpm2cntblk:", &fp->xpm2cntblk);
	start = dumpGas(start, end, "acpi: fadt: xpmtmrblk:", &fp->xpmtmrblk);
	start = dumpGas(start, end, "acpi: fadt: xgpe0blk:", &fp->xgpe0blk);
	start = dumpGas(start, end, "acpi: fadt: xgpe1blk:", &fp->xgpe1blk);
	return start;
}

static struct Atable *acpifadt(uint8_t * p, int len)
{
	struct Fadt *fp;

	if (len < 116) {
		printk("ACPI: unusually short FADT, aborting!\n");
		return 0;
	}
	fp = &fadt;
	fp->facs = l32get(p + 36);
	fp->dsdt = l32get(p + 40);
	fp->pmprofile = p[45];
	fp->sciint = l16get(p + 46);
	fp->smicmd = l32get(p + 48);
	fp->acpienable = p[52];
	fp->acpidisable = p[53];
	fp->s4biosreq = p[54];
	fp->pstatecnt = p[55];
	fp->pm1aevtblk = l32get(p + 56);
	fp->pm1bevtblk = l32get(p + 60);
	fp->pm1acntblk = l32get(p + 64);
	fp->pm1bcntblk = l32get(p + 68);
	fp->pm2cntblk = l32get(p + 72);
	fp->pmtmrblk = l32get(p + 76);
	fp->gpe0blk = l32get(p + 80);
	fp->gpe1blk = l32get(p + 84);
	fp->pm1evtlen = p[88];
	fp->pm1cntlen = p[89];
	fp->pm2cntlen = p[90];
	fp->pmtmrlen = p[91];
	fp->gpe0blklen = p[92];
	fp->gpe1blklen = p[93];
	fp->gp1base = p[94];
	fp->cstcnt = p[95];
	fp->plvl2lat = l16get(p + 96);
	fp->plvl3lat = l16get(p + 98);
	fp->flushsz = l16get(p + 100);
	fp->flushstride = l16get(p + 102);
	fp->dutyoff = p[104];
	fp->dutywidth = p[105];
	fp->dayalrm = p[106];
	fp->monalrm = p[107];
	fp->century = p[108];
	fp->iapcbootarch = l16get(p + 109);
	fp->flags = l32get(p + 112);

	/* qemu gives us a 116 byte fadt, though i haven't seen any HW do that. */
	if (len < 244)
		return 0;

	gasget(&fp->resetreg, p + 116);
	fp->resetval = p[128];
	fp->xfacs = l64get(p + 132);
	fp->xdsdt = l64get(p + 140);
	gasget(&fp->xpm1aevtblk, p + 148);
	gasget(&fp->xpm1bevtblk, p + 160);
	gasget(&fp->xpm1acntblk, p + 172);
	gasget(&fp->xpm1bcntblk, p + 184);
	gasget(&fp->xpm2cntblk, p + 196);
	gasget(&fp->xpmtmrblk, p + 208);
	gasget(&fp->xgpe0blk, p + 220);
	gasget(&fp->xgpe1blk, p + 232);

	if (fp->xfacs != 0)
		loadfacs(fp->xfacs);
	else
		loadfacs(fp->facs);

	if (fp->xdsdt == ((uint64_t) fp->dsdt))	/* acpica */
		loaddsdt(fp->xdsdt);
	else
		loaddsdt(fp->dsdt);

	return NULL;	/* can be unmapped once parsed */
}

static char *dumpmsct(char *start, char *end, struct Msct *msct)
{
	struct Mdom *st;

	if (!msct)
		return start;
	start = seprintf(start, end, "acpi: msct: %d doms %d clkdoms %#p maxpa\n",
					 msct->ndoms, msct->nclkdoms, msct->maxpa);
	for (st = msct->dom; st != NULL; st = st->next)
		start = seprintf(start, end, "\t[%d:%d] %d maxproc %#p maxmmem\n",
						 st->start, st->end, st->maxproc, st->maxmem);
	start = seprintf(start, end, "\n");
	return start;
}

/*
 * XXX: should perhaps update our idea of available memory.
 * Else we should remove this code.
 */
static struct Atable *acpimsct(uint8_t * p, int len)
{
	uint8_t *pe;
	struct Mdom **stl, *st;
	int off;

	msct = kzmalloc(sizeof(struct Msct), 0);
	msct->ndoms = l32get(p + 40) + 1;
	msct->nclkdoms = l32get(p + 44) + 1;
	msct->maxpa = l64get(p + 48);
	msct->dom = NULL;
	stl = &msct->dom;
	pe = p + len;
	off = l32get(p + 36);
	for (p += off; p < pe; p += 22) {
		st = kzmalloc(sizeof(struct Mdom), 0);
		st->next = NULL;
		st->start = l32get(p + 2);
		st->end = l32get(p + 6);
		st->maxproc = l32get(p + 10);
		st->maxmem = l64get(p + 14);
		*stl = st;
		stl = &st->next;
	}
	return NULL;	/* can be unmapped once parsed */
}

static char *dumpsrat(char *start, char *end, struct Srat *st)
{
	start = seprintf(start, end, "acpi: srat:\n");
	for (; st != NULL; st = st->next)
		switch (st->type) {
			case SRlapic:
				start =
					seprintf(start, end,
							 "\tlapic: dom %d apic %d sapic %d clk %d\n",
							 st->lapic.dom, st->lapic.apic, st->lapic.sapic,
							 st->lapic.clkdom);
				break;
			case SRmem:
				start = seprintf(start, end, "\tmem: dom %d %#p %#p %c%c\n",
								 st->mem.dom, st->mem.addr, st->mem.len,
								 st->mem.hplug ? 'h' : '-',
								 st->mem.nvram ? 'n' : '-');
				break;
			case SRlx2apic:
				start =
					seprintf(start, end, "\tlx2apic: dom %d apic %d clk %d\n",
							 st->lx2apic.dom, st->lx2apic.apic,
							 st->lx2apic.clkdom);
				break;
			default:
				start = seprintf(start, end, "\t<unknown srat entry>\n");
		}
	start = seprintf(start, end, "\n");
	return start;
}

static struct Atable *acpisrat(uint8_t * p, int len)
{

	struct Srat **stl, *st;
	uint8_t *pe;
	int stlen, flags;

	if (srat != NULL) {
		printd("acpi: two SRATs?\n");
		return NULL;
	}

	stl = &srat;
	pe = p + len;
	for (p += 48; p < pe; p += stlen) {
		st = kzmalloc(sizeof(struct Srat), 1);
		st->type = p[0];
		st->next = NULL;
		stlen = p[1];
		switch (st->type) {
			case SRlapic:
				st->lapic.dom = p[2] | p[9] << 24 | p[10] << 16 | p[11] << 8;
				st->lapic.apic = p[3];
				st->lapic.sapic = p[8];
				st->lapic.clkdom = l32get(p + 12);
				if (l32get(p + 4) == 0) {
					kfree(st);
					st = NULL;
				}
				break;
			case SRmem:
				st->mem.dom = l32get(p + 2);
				st->mem.addr = l64get(p + 8);
				st->mem.len = l64get(p + 16);
				flags = l32get(p + 28);
				if ((flags & 1) == 0) {	/* not enabled */
					kfree(st);
					st = NULL;
				} else {
					st->mem.hplug = flags & 2;
					st->mem.nvram = flags & 4;
				}
				break;
			case SRlx2apic:
				st->lx2apic.dom = l32get(p + 4);
				st->lx2apic.apic = l32get(p + 8);
				st->lx2apic.clkdom = l32get(p + 16);
				if (l32get(p + 12) == 0) {
					kfree(st);
					st = NULL;
				}
				break;
			default:
				printd("unknown SRAT structure\n");
				kfree(st);
				st = NULL;
		}
		if (st != NULL) {
			*stl = st;
			stl = &st->next;
		}
	}
	return NULL;	/* can be unmapped once parsed */
}

static char *dumpslit(char *start, char *end, struct Slit *sl)
{
	int i;

	if (!sl)
		return start;
	start = seprintf(start, end, "acpi slit:\n");
	for (i = 0; i < sl->rowlen * sl->rowlen; i++) {
		start = seprintf(start, end,
						 "slit: %ux\n",
						 sl->e[i / sl->rowlen][i % sl->rowlen].dist);
	}
	start = seprintf(start, end, "\n");
	return start;
}

static int cmpslitent(void *v1, void *v2)
{
	struct SlEntry *se1, *se2;

	se1 = v1;
	se2 = v2;
	return se1->dist - se2->dist;
}

static struct Atable *acpislit(uint8_t * p, int len)
{

	uint8_t *pe;
	int i, j, k;
	struct SlEntry *se;

	pe = p + len;
	slit = kzmalloc(sizeof(*slit), 0);
	slit->rowlen = l64get(p + 36);
	slit->e = kzmalloc(slit->rowlen * sizeof(struct SlEntry *), 0);
	for (i = 0; i < slit->rowlen; i++)
		slit->e[i] = kzmalloc(sizeof(struct SlEntry) * slit->rowlen, 0);

	i = 0;
	for (p += 44; p < pe; p++, i++) {
		j = i / slit->rowlen;
		k = i % slit->rowlen;
		se = &slit->e[j][k];
		se->dom = k;
		se->dist = *p;
	}
#if 0
	/* TODO: might need to sort this shit */
	for (i = 0; i < slit->rowlen; i++)
		qsort(slit->e[i], slit->rowlen, sizeof(slit->e[0][0]), cmpslitent);
#endif
	return NULL;	/* can be unmapped once parsed */
}

uintptr_t acpimblocksize(uintptr_t addr, int *dom)
{
	struct Srat *sl;

	for (sl = srat; sl != NULL; sl = sl->next)
		if (sl->type == SRmem)
			if (sl->mem.addr <= addr && sl->mem.addr + sl->mem.len > addr) {
				*dom = sl->mem.dom;
				return sl->mem.len - (addr - sl->mem.addr);
			}
	return 0;
}

int pickcore(int mycolor, int index)
{
	int color;
	int ncorepercol;

	if (slit == NULL) {
		return 0;
	}
	ncorepercol = num_cpus / slit->rowlen;
	color = slit->e[mycolor][index / ncorepercol].dom;
	return color * ncorepercol + index % ncorepercol;
}

static char *polarity[4] = {
	"polarity/trigger like in ISA",
	"active high",
	"BOGUS POLARITY",
	"active low"
};

static char *trigger[] = {
	"BOGUS TRIGGER",
	"edge",
	"BOGUS TRIGGER",
	"level"
};

static char *printiflags(char *start, char *end, int flags)
{

	return seprintf(start, end, "[%s,%s]",
					polarity[flags & AFpmask], trigger[(flags & AFtmask) >> 2]);
}

static char *dumpmadt(char *start, char *end, struct Madt *apics)
{
	struct Apicst *st;

	start =
		seprintf(start, end, "acpi: madt lapic paddr %llux pcat %d:\n",
				 apics->lapicpa, apics->pcat);
	for (st = apics->st; st != NULL; st = st->next)

		switch (st->type) {
			case ASlapic:
				start =
					seprintf(start, end, "\tlapic pid %d id %d\n",
							 st->lapic.pid, st->lapic.id);
				break;
			case ASioapic:
			case ASiosapic:
				start =
					seprintf(start, end,
							 "\tioapic id %d addr %#llux ibase %d\n",
							 st->ioapic.id, st->ioapic.addr, st->ioapic.ibase);
				break;
			case ASintovr:
				start =
					seprintf(start, end, "\tintovr irq %d intr %d flags $%p",
							 st->intovr.irq, st->intovr.intr, st->intovr.flags);
				start = printiflags(start, end, st->intovr.flags);
				start = seprintf(start, end, "\n");
				break;
			case ASnmi:
				start = seprintf(start, end, "\tnmi intr %d flags $%p\n",
								 st->nmi.intr, st->nmi.flags);
				break;
			case ASlnmi:
				start =
					seprintf(start, end, "\tlnmi pid %d lint %d flags $%p\n",
							 st->lnmi.pid, st->lnmi.lint, st->lnmi.flags);
				break;
			case ASlsapic:
				start =
					seprintf(start, end,
							 "\tlsapic pid %d id %d eid %d puid %d puids %s\n",
							 st->lsapic.pid, st->lsapic.id, st->lsapic.eid,
							 st->lsapic.puid, st->lsapic.puids);
				break;
			case ASintsrc:
				start =
					seprintf(start, end,
							 "\tintr type %d pid %d peid %d iosv %d intr %d %#x\n",
							 st->type, st->intsrc.pid, st->intsrc.peid,
							 st->intsrc.iosv, st->intsrc.intr,
							 st->intsrc.flags);
				start = printiflags(start, end, st->intsrc.flags);
				start = seprintf(start, end, "\n");
				break;
			case ASlx2apic:
				start =
					seprintf(start, end, "\tlx2apic puid %d id %d\n",
							 st->lx2apic.puid, st->lx2apic.id);
				break;
			case ASlx2nmi:
				start =
					seprintf(start, end, "\tlx2nmi puid %d intr %d flags $%p\n",
							 st->lx2nmi.puid, st->lx2nmi.intr,
							 st->lx2nmi.flags);
				break;
			default:
				start = seprintf(start, end, "\t<unknown madt entry>\n");
		}
	start = seprintf(start, end, "\n");
	return start;
}

static struct Atable *acpimadt(uint8_t * p, int len)
{

	uint8_t *pe;
	struct Apicst *st, *l, **stl;
	int stlen, id;

	apics = kzmalloc(sizeof(struct Madt), 1);
	apics->lapicpa = l32get(p + 36);
	apics->pcat = l32get(p + 40);
	apics->st = NULL;
	stl = &apics->st;
	pe = p + len;
	for (p += 44; p < pe; p += stlen) {
		st = kzmalloc(sizeof(struct Apicst), 1);
		st->type = p[0];
		st->next = NULL;
		stlen = p[1];
		switch (st->type) {
			case ASlapic:
				st->lapic.pid = p[2];
				st->lapic.id = p[3];
				if (l32get(p + 4) == 0) {
					kfree(st);
					st = NULL;
				}
				break;
			case ASioapic:
				st->ioapic.id = id = p[2];
				st->ioapic.addr = l32get(p + 4);
				st->ioapic.ibase = l32get(p + 8);
				/* ioapic overrides any ioapic entry for the same id */
				for (l = apics->st; l != NULL; l = l->next)
					if (l->type == ASiosapic && l->iosapic.id == id) {
						st->ioapic = l->iosapic;
						/* we leave it linked; could be removed */
						break;
					}
				break;
			case ASintovr:
				st->intovr.irq = p[3];
				st->intovr.intr = l32get(p + 4);
				st->intovr.flags = l16get(p + 8);
				break;
			case ASnmi:
				st->nmi.flags = l16get(p + 2);
				st->nmi.intr = l32get(p + 4);
				break;
			case ASlnmi:
				st->lnmi.pid = p[2];
				st->lnmi.flags = l16get(p + 3);
				st->lnmi.lint = p[5];
				break;
			case ASladdr:
				/* This is for 64 bits, perhaps we should not
				 * honor it on 32 bits.
				 */
				apics->lapicpa = l64get(p + 8);
				break;
			case ASiosapic:
				id = st->iosapic.id = p[2];
				st->iosapic.ibase = l32get(p + 4);
				st->iosapic.addr = l64get(p + 8);
				/* iosapic overrides any ioapic entry for the same id */
				for (l = apics->st; l != NULL; l = l->next)
					if (l->type == ASioapic && l->ioapic.id == id) {
						l->ioapic = st->iosapic;
						kfree(st);
						st = NULL;
						break;
					}
				break;
			case ASlsapic:
				st->lsapic.pid = p[2];
				st->lsapic.id = p[3];
				st->lsapic.eid = p[4];
				st->lsapic.puid = l32get(p + 12);
				if (l32get(p + 8) == 0) {
					kfree(st);
					st = NULL;
				} else
					kstrdup(&st->lsapic.puids, (char *)p + 16);
				break;
			case ASintsrc:
				st->intsrc.flags = l16get(p + 2);
				st->type = p[4];
				st->intsrc.pid = p[5];
				st->intsrc.peid = p[6];
				st->intsrc.iosv = p[7];
				st->intsrc.intr = l32get(p + 8);
				st->intsrc.any = l32get(p + 12);
				break;
			case ASlx2apic:
				st->lx2apic.id = l32get(p + 4);
				st->lx2apic.puid = l32get(p + 12);
				if (l32get(p + 8) == 0) {
					kfree(st);
					st = NULL;
				}
				break;
			case ASlx2nmi:
				st->lx2nmi.flags = l16get(p + 2);
				st->lx2nmi.puid = l32get(p + 4);
				st->lx2nmi.intr = p[8];
				break;
			default:
				printd("unknown APIC structure\n");
				kfree(st);
				st = NULL;
		}
		if (st != NULL) {
			*stl = st;
			stl = &st->next;
		}
	}
	return NULL;	/* can be unmapped once parsed */
}

/*
 * Map the table and keep it there.
 */
static struct Atable *acpitable(uint8_t * p, int len)
{
	if (len < Sdthdrsz) {
		return NULL;
	}
	return newtable(p);
}

static char *dumptable(char *start, char *end, char *sig, uint8_t * p, int l)
{
	int n, i;

	if (2 > 1) {
		start = seprintf(start, end, "%s @ %#p\n", sig, p);
		if (2 > 2)
			n = l;
		else
			n = 256;
		for (i = 0; i < n; i++) {
			if ((i % 16) == 0)
				start = seprintf(start, end, "%x: ", i);
			start = seprintf(start, end, " %2.2ux", p[i]);
			if ((i % 16) == 15)
				start = seprintf(start, end, "\n");
		}
		start = seprintf(start, end, "\n");
		start = seprintf(start, end, "\n");
	}
	return start;
}

static char *seprinttable(char *s, char *e, struct Atable *t)
{
	uint8_t *p;
	int i, n;

	p = (uint8_t *) t->tbl;	/* include header */
	n = Sdthdrsz + t->dlen;
	s = seprintf(s, e, "%s @ %#p\n", t->sig, p);
	for (i = 0; i < n; i++) {
		if ((i % 16) == 0)
			s = seprintf(s, e, "%x: ", i);
		s = seprintf(s, e, " %2.2ux", p[i]);
		if ((i % 16) == 15)
			s = seprintf(s, e, "\n");
	}
	return seprintf(s, e, "\n\n");
}

/*
 * process xsdt table and load tables with sig, or all if NULL.
 * (XXX: should be able to search for sig, oemid, oemtblid)
 */
static int acpixsdtload(char *sig)
{
	int i, l, t, unmap, found;
	uintptr_t dhpa;
	uint8_t *sdt;
	char tsig[5];
	char table[128];

	found = 0;
	for (i = 0; i < xsdt->len; i += xsdt->asize) {
		if (xsdt->asize == 8)
			dhpa = l64get(xsdt->p + i);
		else
			dhpa = l32get(xsdt->p + i);
		if ((sdt = sdtmap(dhpa, &l, 1)) == NULL)
			continue;
		unmap = 1;
		memmove(tsig, sdt, 4);
		tsig[4] = 0;
		if (sig == NULL || strcmp(sig, tsig) == 0) {
			printd("acpi: %s addr %#p\n", tsig, sdt);
			for (t = 0; t < ARRAY_SIZE(ptables); t++)
				if (strcmp(tsig, ptables[t].sig) == 0) {
					//dumptable(table, &table[127], tsig, sdt, l);
					unmap = ptables[t].f(sdt, l) == NULL;
					found = 1;
					break;
				}
		}
//      if(unmap)
//          vunmap(sdt, l);
	}
	return found;
}

static void *rsdsearch(char *signature)
{
	uintptr_t p;
	uint8_t *bda;
	void *rsd;

	/*
	 * Search for the data structure signature:
	 * 1) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	return sigscan(KADDR(0xE0000), 0x20000, signature);
}

static void acpirsdptr(void)
{
	struct Rsdp *rsd;
	int asize;
	uintptr_t sdtpa;

	if ((rsd = rsdsearch("RSD PTR ")) == NULL) {
		return;
	}

	assert(sizeof(struct Sdthdr) == 36);

	printd("acpi: RSD PTR@ %#p, physaddr $%p length %ud %#llux rev %d\n",
		   rsd, l32get(rsd->raddr), l32get(rsd->length),
		   l64get(rsd->xaddr), rsd->revision);

	if (rsd->revision >= 2) {
		if (sdtchecksum(rsd, 36) == NULL) {
			printk("acpi: RSD: bad checksum\n");
			return;
		}
		sdtpa = l64get(rsd->xaddr);
		asize = 8;
	} else {
		if (sdtchecksum(rsd, 20) == NULL) {
			printk("acpi: RSD: bad checksum\n");
			return;
		}
		sdtpa = l32get(rsd->raddr);
		asize = 4;
	}

	/*
	 * process the RSDT or XSDT table.
	 */
	xsdt = kzmalloc(sizeof(struct Xsdt), 0);
	if (xsdt == NULL) {
		printk("acpi: malloc failed\n");
		return;
	}
	if ((xsdt->p = sdtmap(sdtpa, &xsdt->len, 1)) == NULL) {
		printk("acpi: sdtmap failed\n");
		return;
	}
	if ((xsdt->p[0] != 'R' && xsdt->p[0] != 'X')
		|| memcmp(xsdt->p + 1, "SDT", 3) != 0) {
		printd("acpi: xsdt sig: %c%c%c%c\n", xsdt->p[0], xsdt->p[1], xsdt->p[2],
			   xsdt->p[3]);
		kfree(xsdt);
		xsdt = NULL;
		//vunmap(xsdt, xsdt->len);
		return;
	}
	xsdt->p += sizeof(struct Sdthdr);
	xsdt->len -= sizeof(struct Sdthdr);
	xsdt->asize = asize;
	printd("acpi: XSDT %#p\n", xsdt);
	acpixsdtload(NULL);
	/* xsdt is kept and not unmapped */

}

static int
acpigen(struct chan *c, char *unused_char_p_t, struct dirtab *tab, int ntab,
		int i, struct dir *dp)
{
	struct qid qid;

	if (i == DEVDOTDOT) {
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, ".", 0, eve, 0555, dp);
		return 1;
	}
	i++;	/* skip first element for . itself */
	if (tab == 0 || i >= ntab) {
		return -1;
	}
	tab += i;
	qid = tab->qid;
	qid.path &= ~Qdir;
	qid.vers = 0;
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static char *dumpGas(char *start, char *end, char *prefix, struct Gas *g)
{
	static char *rnames[] = {
		"mem", "io", "pcicfg", "embed",
		"smb", "cmos", "pcibar", "ipmi"
	};
	start = seprintf(start, end, "%s", prefix);

	switch (g->spc) {
		case Rsysmem:
		case Rsysio:
		case Rembed:
		case Rsmbus:
		case Rcmos:
		case Rpcibar:
		case Ripmi:
			start = seprintf(start, end, "[%s ", rnames[g->spc]);
			break;
		case Rpcicfg:
			start = seprintf(start, end, "[pci ");
			start =
				seprintf(start, end, "dev %#p ",
						 (uint32_t) (g->addr >> 32) & 0xFFFF);
			start =
				seprintf(start, end, "fn %#p ",
						 (uint32_t) (g->addr & 0xFFFF0000) >> 16);
			start =
				seprintf(start, end, "adr %#p ", (uint32_t) (g->addr & 0xFFFF));
			break;
		case Rfixedhw:
			start = seprintf(start, end, "[hw ");
			break;
		default:
			start = seprintf(start, end, "[spc=%#p ", g->spc);
	}
	start = seprintf(start, end, "off %d len %d addr %#p sz%d]",
					 g->off, g->len, g->addr, g->accsz);
	start = seprintf(start, end, "\n");
	return start;
}

static unsigned int getbanked(uintptr_t ra, uintptr_t rb, int sz)
{
	unsigned int r;

	r = 0;
	switch (sz) {
		case 1:
			if (ra != 0)
				r |= inb(ra);
			if (rb != 0)
				r |= inb(rb);
			break;
		case 2:
			if (ra != 0)
				r |= inw(ra);
			if (rb != 0)
				r |= inw(rb);
			break;
		case 4:
			if (ra != 0)
				r |= inl(ra);
			if (rb != 0)
				r |= inl(rb);
			break;
		default:
			printd("getbanked: wrong size\n");
	}
	return r;
}

static unsigned int setbanked(uintptr_t ra, uintptr_t rb, int sz, int v)
{
	unsigned int r;

	r = -1;
	switch (sz) {
		case 1:
			if (ra != 0)
				outb(ra, v);
			if (rb != 0)
				outb(rb, v);
			break;
		case 2:
			if (ra != 0)
				outw(ra, v);
			if (rb != 0)
				outw(rb, v);
			break;
		case 4:
			if (ra != 0)
				outl(ra, v);
			if (rb != 0)
				outl(rb, v);
			break;
		default:
			printd("setbanked: wrong size\n");
	}
	return r;
}

static unsigned int getpm1ctl(void)
{
	return getbanked(fadt.pm1acntblk, fadt.pm1bcntblk, fadt.pm1cntlen);
}

static void setpm1sts(unsigned int v)
{
	setbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen / 2, v);
}

static unsigned int getpm1sts(void)
{
	return getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen / 2);
}

static unsigned int getpm1en(void)
{
	int sz;

	sz = fadt.pm1evtlen / 2;
	return getbanked(fadt.pm1aevtblk + sz, fadt.pm1bevtblk + sz, sz);
}

static int getgpeen(int n)
{
	return inb(gpes[n].enio) & 1 << gpes[n].enbit;
}

static void setgpeen(int n, unsigned int v)
{
	int old;

	old = inb(gpes[n].enio);
	if (v)
		outb(gpes[n].enio, old | 1 << gpes[n].enbit);
	else
		outb(gpes[n].enio, old & ~(1 << gpes[n].enbit));
}

static void clrgpests(int n)
{
	outb(gpes[n].stsio, 1 << gpes[n].stsbit);
}

static unsigned int getgpests(int n)
{
	return inb(gpes[n].stsio) & 1 << gpes[n].stsbit;
}

#warning "no acpi interrupts yet"
#if 0
static void acpiintr(Ureg *, void *)
{
	int i;
	unsigned int sts, en;

	printd("acpi: intr\n");

	for (i = 0; i < ngpes; i++)
		if (getgpests(i)) {
			printd("gpe %d on\n", i);
			en = getgpeen(i);
			setgpeen(i, 0);
			clrgpests(i);
			if (en != 0)
				printd("acpiitr: calling gpe %d\n", i);
			//  queue gpe for calling gpe->ho in the
			//  aml process.
			//  enable it again when it returns.
		}
	sts = getpm1sts();
	en = getpm1en();
	printd("acpiitr: pm1sts %#p pm1en %#p\n", sts, en);
	if (sts & en)
		printd("have enabled events\n");
	if (sts & 1)
		printd("power button\n");
	// XXX serve other interrupts here.
	setpm1sts(sts);
}
#endif
static void initgpes(void)
{
	int i, n0, n1;

	n0 = fadt.gpe0blklen / 2;
	n1 = fadt.gpe1blklen / 2;
	ngpes = n0 + n1;
	gpes = kzmalloc(sizeof(struct Gpe) * ngpes, 1);
	for (i = 0; i < n0; i++) {
		gpes[i].nb = i;
		gpes[i].stsbit = i & 7;
		gpes[i].stsio = fadt.gpe0blk + (i >> 3);
		gpes[i].enbit = (n0 + i) & 7;
		gpes[i].enio = fadt.gpe0blk + ((n0 + i) >> 3);
	}
	for (i = 0; i + n0 < ngpes; i++) {
		gpes[i + n0].nb = fadt.gp1base + i;
		gpes[i + n0].stsbit = i & 7;
		gpes[i + n0].stsio = fadt.gpe1blk + (i >> 3);
		gpes[i + n0].enbit = (n1 + i) & 7;
		gpes[i + n0].enio = fadt.gpe1blk + ((n1 + i) >> 3);
	}
	for (i = 0; i < ngpes; i++) {
		setgpeen(i, 0);
		clrgpests(i);
	}
}

static void acpiioalloc(unsigned int addr, int len)
{
	if (addr != 0) {
		printk("Just TAKING port %016lx to %016lx\n", addr, addr + len);
		//ioalloc(addr, len, 0, "acpi");
	}
}

int acpiinit(void)
{
	/* this smicmd test implements 'run once' for now. */
	if (fadt.smicmd == 0) {
		//fmtinstall('G', Gfmt);
		acpirsdptr();
		if (fadt.smicmd == 0) {
			return -1;
		}
	}
	printk("ACPI initialized\n");
	return 0;
}

static struct chan *acpiattach(char *spec)
{
	int i;

	/*
	 * This was written for the stock kernel.
	 * This code must use 64 registers to be acpi ready in nix.
	 */
	if (acpiinit() < 0) {
		error("no acpi");
	}

	/*
	 * should use fadt->xpm* and fadt->xgpe* registers for 64 bits.
	 * We are not ready in this kernel for that.
	 */
	acpiioalloc(fadt.smicmd, 1);
	acpiioalloc(fadt.pm1aevtblk, fadt.pm1evtlen);
	acpiioalloc(fadt.pm1bevtblk, fadt.pm1evtlen);
	acpiioalloc(fadt.pm1acntblk, fadt.pm1cntlen);
	acpiioalloc(fadt.pm1bcntblk, fadt.pm1cntlen);
	acpiioalloc(fadt.pm2cntblk, fadt.pm2cntlen);
	acpiioalloc(fadt.pmtmrblk, fadt.pmtmrlen);
	acpiioalloc(fadt.gpe0blk, fadt.gpe0blklen);
	acpiioalloc(fadt.gpe1blk, fadt.gpe1blklen);

	initgpes();

	/*
	 * This starts ACPI, which may require we handle
	 * power mgmt events ourselves. Use with care.
	 */
	outb(fadt.smicmd, fadt.acpienable);
	for (i = 0; i < 10; i++)
		if (getpm1ctl() & Pm1SciEn)
			break;
	if (i == 10)
		error("acpi: failed to enable\n");
//  if(fadt.sciint != 0)
//      intrenable(fadt.sciint, acpiintr, 0, BUSUNKNOWN, "acpi");
	return devattach('a', spec);
}

static struct walkqid *acpiwalk(struct chan *c, struct chan *nc, char **name,
								int nname)
{
	return devwalk(c, nc, name, nname, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static int acpistat(struct chan *c, uint8_t * dp, int n)
{
	return devstat(c, dp, n, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static struct chan *acpiopen(struct chan *c, int omode)
{
	return devopen(c, omode, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static void acpiclose(struct chan *unused)
{
}

static char *ttext;
static int tlen;

static long acpiread(struct chan *c, void *a, long n, int64_t off)
{
	long q;
	struct Atable *t;
	char *ns, *s, *e, *ntext;

	if (ttext == NULL) {
		tlen = 32768;
		ttext = kzmalloc(tlen, 0);
	}
	if (ttext == NULL) {
		error("acpiread: no memory");
	}
	q = c->qid.path;
	switch (q) {
		case Qdir:
			return devdirread(c, a, n, acpidir, ARRAY_SIZE(acpidir), acpigen);
		case Qtbl:
			s = ttext;
			e = ttext + tlen;
			strncpy(s, "no tables\n", sizeof(s));
			for (t = tfirst; t != NULL; t = t->next) {
				ns = seprinttable(s, e, t);
				while (ns == e - 1) {
					ntext = krealloc(ttext, tlen * 2, 0);
					if (ntext == NULL)
						panic("acpi: no memory\n");
					s = ntext + (ttext - s);
					ttext = ntext;
					tlen *= 2;
					e = ttext + tlen;
					ns = seprinttable(s, e, t);
				}
				s = ns;
			}
			return readstr(off, a, n, ttext);
		case Qpretty:
			s = ttext;
			e = ttext + tlen;
			s = dumpfadt(s, e, &fadt);
			s = dumpmadt(s, e, apics);
			s = dumpslit(s, e, slit);
			s = dumpsrat(s, e, srat);
			dumpmsct(s, e, msct);
			return readstr(off, a, n, ttext);
		case Qioapic:
			s = ioapicdump(ttext, ttext + tlen);
			return readstr(off, a, n, ttext);
		case Qapic:
			s = apicdump(ttext, ttext + tlen);
			return readstr(off, a, n, ttext);
		case Qio:
			if (reg == NULL)
				error("region not configured");
			return regio(reg, a, n, off, 0);
	}
	error(Eperm);
	return -1;
}

static long acpiwrite(struct chan *c, void *a, long n, int64_t off)
{
	ERRSTACK(2);
	struct cmdtab *ct;
	struct cmdbuf *cb;
	struct Reg *r;
	unsigned int rno, fun, dev, bus, i;

	if (c->qid.path == Qio) {
		if (reg == NULL)
			error("region not configured");
		return regio(reg, a, n, off, 1);
	}
	if (c->qid.path != Qctl)
		error(Eperm);

	cb = parsecmd(a, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	ct = lookupcmd(cb, ctls, ARRAY_SIZE(ctls));
	switch (ct->index) {
		case CMregion:
			/* TODO: this block is racy on reg (global) */
			r = reg;
			if (r == NULL) {
				r = kzmalloc(sizeof(struct Reg), 0);
				r->name = NULL;
			}
			kstrdup(&r->name, cb->f[1]);
			r->spc = acpiregid(cb->f[2]);
			if (r->spc < 0) {
				kfree(r);
				reg = NULL;
				error("bad region type");
			}
			if (r->spc == Rpcicfg || r->spc == Rpcibar) {
				rno = r->base >> Rpciregshift & Rpciregmask;
				fun = r->base >> Rpcifunshift & Rpcifunmask;
				dev = r->base >> Rpcidevshift & Rpcidevmask;
				bus = r->base >> Rpcibusshift & Rpcibusmask;
				#ifdef CONFIG_X86
				r->tbdf = MKBUS(BusPCI, bus, dev, fun);
				#else
				r->tbdf = 0
				#endif
				r->base = rno;	/* register ~ our base addr */
			}
			r->base = strtoul(cb->f[3], NULL, 0);
			r->len = strtoul(cb->f[4], NULL, 0);
			r->accsz = strtoul(cb->f[5], NULL, 0);
			if (r->accsz < 1 || r->accsz > 4) {
				kfree(r);
				reg = NULL;
				error("bad region access size");
			}
			reg = r;
			printd("region %s %s %p %p sz%d",
				   r->name, acpiregstr(r->spc), r->base, r->len, r->accsz);
			break;
		case CMgpe:
			i = strtoul(cb->f[1], NULL, 0);
			if (i >= ngpes)
				error("gpe out of range");
			kstrdup(&gpes[i].obj, cb->f[2]);
			setgpeen(i, 1);
			break;
		default:
			panic("acpi: unknown ctl");
	}
	poperror();
	kfree(cb);
	return n;
}

struct dev acpidevtab __devtab = {
	'a',
	"acpi",

	devreset,
	devinit,
	devshutdown,
	acpiattach,
	acpiwalk,
	acpistat,
	acpiopen,
	devcreate,
	acpiclose,
	acpiread,
	devbread,
	acpiwrite,
	devbwrite,
	devremove,
	devwstat,
};
