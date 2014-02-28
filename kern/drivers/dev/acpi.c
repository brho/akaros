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
#include <arch/pci.h>
#include <acpi.h>

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
static struct Atable* acpifadt( uint8_t *unused_uint8_p_t, int);
static struct Atable* acpitable( uint8_t *unused_uint8_p_t, int);
static struct Atable* acpimadt( uint8_t *unused_uint8_p_t, int);
static struct Atable* acpimsct( uint8_t *unused_uint8_p_t, int);
static struct Atable* acpisrat( uint8_t *unused_uint8_p_t, int);
static struct Atable* acpislit( uint8_t *unused_uint8_p_t, int);


static struct cmdtab ctls[] =
{
	{CMregion,	"region",	6},
	{CMgpe,		"gpe",		3},
};

static struct dirtab acpidir[]={
	{".",		{Qdir, 0, QTDIR},	0,	DMDIR|0555},
	{"acpictl",	{Qctl},			0,	0666},
	{"acpitbl",	{Qtbl},			0,	0444},
	{"acpiregio",	{Qio},			0,	0666},
};

/*
 * The DSDT is always given to the user interpreter.
 * Tables listed here are also loaded from the XSDT:
 * MSCT, MADT, and FADT are processed by us, because they are
 * required to do early initialization before we have user processes.
 * Other tables are given to the user level interpreter for
 * execution.
 */
static struct Parse ptables[] =
{
	{"FACP", acpifadt},
	{"APIC",	acpimadt,},
	{"SRAT",	acpisrat,},
	{"SLIT",	acpislit,},
	{"MSCT",	acpimsct,},
	{"SSDT", acpitable,},
};

static struct Facs*	facs;	/* Firmware ACPI control structure */
static struct Fadt	fadt;	/* Fixed ACPI description. To reach ACPI registers */
static struct Xsdt*	xsdt;	/* XSDT table */
static struct Atable*	tfirst;	/* loaded DSDT/SSDT/... tables */
static struct Atable*	tlast;	/* pointer to last table */
static struct Madt*	apics;	/* APIC info */
static struct Srat*	srat;	/* System resource affinity, used by physalloc */
static struct Slit*	slit;	/* System locality information table used by the scheduler */
static struct Msct*	msct;	/* Maximum system characteristics table */
static struct Reg*	reg;	/* region used for I/O */
static struct Gpe*	gpes;	/* General purpose events */
static int	ngpes;

static char* regnames[] = {
	"mem", "io", "pcicfg", "embed",
	"smb", "cmos", "pcibar",
};

static char*
acpiregstr(int id)
{
	static char buf[20];	/* BUG */

	if(id >= 0 && id < ARRAY_SIZE(regnames)) {
		return regnames[id];
	}
	seprintf(buf, buf+sizeof(buf), "spc:%#x", id);
	return buf;
}

static int
acpiregid(char *s)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(regnames); i++)
		if(strcmp(regnames[i], s) == 0) {
			return i;
		}
	return -1;
}

static uint64_t
l64get(uint8_t* p)
{
	/*
	 * Doing this as a define
	 * #define l64get(p)	(((u64int)l32get(p+4)<<32)|l32get(p))
	 * causes 8c to abort with "out of fixed registers" in
	 * rsdlink() below.
	 */
	return (((uint64_t)l32get(p+4)<<32)|l32get(p));
}

static uint8_t
mget8(uintptr_t p, void*unused)
{
	uint8_t *cp = (uint8_t*)p;
	return *cp;
}

static void
mset8(uintptr_t p, uint8_t v, void*unused)
{
	uint8_t *cp = (uint8_t*)p;
	*cp = v;
}

static uint16_t
mget16(uintptr_t p, void*unused)
{
	uint16_t *cp = (uint16_t*)p;
	return *cp;
}

static void
mset16(uintptr_t p, uint16_t v, void*unused)
{
	uint16_t *cp = (uint16_t*)p;
	*cp = v;
}

static uint32_t
mget32(uintptr_t p, void*unused)
{
	uint32_t *cp = (uint32_t*)p;
	return *cp;
}

static void
mset32(uintptr_t p, uint32_t v, void*unused)
{
	uint32_t *cp = (uint32_t*)p;
	*cp = v;
}

static uint64_t
mget64(uintptr_t p, void*unused)
{
	uint64_t *cp = (uint64_t*)p;
	return *cp;
}

static void
mset64(uintptr_t p, uint64_t v, void*unused)
{
	uint64_t *cp = (uint64_t*)p;
	*cp = v;
}

static uint8_t
ioget8(uintptr_t p, void*unused)
{
	return inb(p);
}

static void
ioset8(uintptr_t p, uint8_t v, void*unused)
{
	outb(p, v);
}

static uint16_t
ioget16(uintptr_t p, void*unused)
{
	return inw(p);
}

static void
ioset16(uintptr_t p, uint16_t v, void*unused)
{
	outw(p, v);
}

static uint32_t
ioget32(uintptr_t p, void*unused)
{
	return inl(p);
}

static void
ioset32(uintptr_t p, uint32_t v, void*unused)
{
	outl(p, v);
}

#define explode_tbdf(tbdf) {pcidev.bus = tbdf >> 16;\
		pcidev.dev = (tbdf>>11)&0x1f;\
		pcidev.func = (tbdf>>8)&3;}


static uint8_t
cfgget8(uintptr_t p, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read8(&pcidev, p);
}

static void
cfgset8(uintptr_t p, uint8_t v, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write8(&pcidev, p, v);
}

static uint16_t
cfgget16(uintptr_t p, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read16(&pcidev, p);
}

static void
cfgset16(uintptr_t p, uint16_t v, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write16(&pcidev, p, v);
}

static uint32_t
cfgget32(uintptr_t p, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	return pcidev_read32(&pcidev, p);
}

static void
cfgset32(uintptr_t p, uint32_t v, void* r)
{
	struct Reg *ro = r;
	struct pci_device pcidev;

	explode_tbdf(ro->tbdf);
	pcidev_write32(&pcidev, p, v);
}

static struct Regio memio = 
{
	NULL,
	mget8, mset8, mget16, mset16,
	mget32, mset32, mget64, mset64
};

static struct Regio ioio = 
{
	NULL,
	ioget8, ioset8, ioget16, ioset16,
	ioget32, ioset32, NULL, NULL
};

static struct Regio cfgio = 
{
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

	printk("regcpy %#p %#p %#p %#p\n", da, sa, len, align);
	if((len%align) != 0)
		printd("regcpy: bug: copy not aligned. truncated\n");
	n = len/align;
	for(i = 0; i < n; i++){
		switch(align){
		case 1:
			printk("cpy8 %#p %#p\n", da, sa);
			dio->set8(da, sio->get8(sa, sio->arg), dio->arg);
			break;
		case 2:
			printk("cpy16 %#p %#p\n", da, sa);
			dio->set16(da, sio->get16(sa, sio->arg), dio->arg);
			break;
		case 4:
			printk("cpy32 %#p %#p\n", da, sa);
			dio->set32(da, sio->get32(sa, sio->arg), dio->arg);
			break;
		case 8:
			printk("cpy64 %#p %#p\n", da, sa);
		//	dio->set64(da, sio->get64(sa, sio->arg), dio->arg);
			break;
		default:
			panic("regcpy: align bug");
		}
		da += align;
		sa += align;
	}
	return n*align;
}

/*
 * Perform I/O within region in access units of accsz bytes.
 * All units in bytes.
 */
static long
regio(struct Reg *r, void *p, uint32_t len, uintptr_t off, int iswr)
{
	struct Regio rio;
	uintptr_t rp;

	printk("reg%s %s %#p %#ullx %#lx sz=%d\n",
		iswr ? "out" : "in", r->name, p, off, len, r->accsz);
	rp = 0;
	if(off + len > r->len){
		printd("regio: access outside limits");
		len = r->len - off;
	}
	if(len <= 0){
		printd("regio: zero len\n");
		return 0;
	}
	switch(r->spc){
	case Rsysmem:
		// XXX should map only what we are going to use
		// A region might be too large.
		// we don't have this nonsense in akaros, right? 
		if(r->p == NULL)
			//r->p = vmap(r->base, len);
			r->p = KADDR(r->base);
		if(r->p == NULL)
			error("regio: vmap failed");
		rp = (uintptr_t)r->p + off;
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
	if(iswr)
		regcpy(&rio, rp, &memio, (uintptr_t)p, len, r->accsz);
	else
		regcpy(&memio, (uintptr_t)p, &rio, rp, len, r->accsz);
	return len;
}

static struct Atable*
newtable(uint8_t *p)
{
	struct Atable *t;
	struct Sdthdr *h;

	t = kzmalloc(sizeof(struct Atable), 0);
	if(t == NULL)
		panic("no memory for more aml tables");
	t->tbl = p;
	h = (struct Sdthdr*)t->tbl;
	t->is64 = h->rev >= 2;
	t->dlen = l32get(h->length) - Sdthdrsz;
	memmove(t->sig, h->sig, sizeof(h->sig));
	t->sig[sizeof(t->sig)-1] = 0;
	memmove(t->oemid, h->oemid, sizeof(h->oemid));
	t->oemtblid[sizeof(t->oemtblid)-1] = 0;
	memmove(t->oemtblid, h->oemtblid, sizeof(h->oemtblid));
	t->oemtblid[sizeof(t->oemtblid)-1] = 0;
	t->next = NULL;
	if(tfirst == NULL)
		tfirst = tlast = t;
	else{
		tlast->next = t;
		tlast = t;
	}
	return t;
}

static void*
sdtchecksum(void* addr, int len)
{
	uint8_t *p, sum;

	sum = 0;
	for(p = addr; len-- > 0; p++)
		sum += *p;
	if(sum == 0) {
		return addr;
	}

	return NULL;
}

static void *
sdtmap(uintptr_t pa, int *n, int cksum)
{
	struct Sdthdr* sdt;

	sdt = KADDR(pa); //vmap(pa, sizeof(struct Sdthdr));
	if(sdt == NULL){
		printk("acpi: vmap1: NULL\n");
		return NULL;
	}
	*n = l32get(sdt->length);
	//vunmap(sdt, sizeof(Sdthdr));
	if((sdt = KADDR(pa) /*vmap(pa, *n)*/) == NULL){
		printk("acpi: NULL vmap\n");
		return NULL;
	}
	if(cksum != 0 && sdtchecksum(sdt, *n) == NULL){
		printk("acpi: SDT: bad checksum\n");
		//vunmap(sdt, sizeof(Sdthdr));
		return NULL;
	}
	return sdt;
}

static int
loadfacs(uintptr_t pa)
{
	int n;

	facs = sdtmap(pa, &n, 0);
	if(facs == NULL) {
		return -1;
	}
	if(memcmp(facs, "FACS", 4) != 0){
		//vunmap(facs, n);
		facs = NULL;
		return -1;
	}
	/* no unmap */

	printk("acpi: facs: hwsig: %#p\n", facs->hwsig);
	printk("acpi: facs: wakingv: %#p\n", facs->wakingv);
	printk("acpi: facs: flags: %#p\n", facs->flags);
	printk("acpi: facs: glock: %#p\n", facs->glock);
	printk("acpi: facs: xwakingv: %#p\n", facs->xwakingv);
	printk("acpi: facs: vers: %#p\n", facs->vers);
	printk("acpi: facs: ospmflags: %#p\n", facs->ospmflags);
	return 0;
}

static void
loaddsdt(uintptr_t pa)
{
	int n;
	uint8_t *dsdtp;

	dsdtp = sdtmap(pa, &n, 1);
	if(dsdtp == NULL) {
		return;
	}
	if(acpitable(dsdtp, n) == NULL)
		;//vunmap(dsdtp, n);
}

static void
gasget(struct Gas *gas, uint8_t *p)
{
	gas->spc = p[0];
	gas->len = p[1];
	gas->off = p[2];
	gas->accsz = p[3];
	gas->addr = l64get(p+4);
}

static void
dumpfadt(struct Fadt *fp)
{
	if(2 == 0) {
		return;
	}

	printk("acpi: fadt: facs: $%p\n", fp->facs);
	printk("acpi: fadt: dsdt: $%p\n", fp->dsdt);
	printk("acpi: fadt: pmprofile: $%p\n", fp->pmprofile);
	printk("acpi: fadt: sciint: $%p\n", fp->sciint);
	printk("acpi: fadt: smicmd: $%p\n", fp->smicmd);
	printk("acpi: fadt: acpienable: $%p\n", fp->acpienable);
	printk("acpi: fadt: acpidisable: $%p\n", fp->acpidisable);
	printk("acpi: fadt: s4biosreq: $%p\n", fp->s4biosreq);
	printk("acpi: fadt: pstatecnt: $%p\n", fp->pstatecnt);
	printk("acpi: fadt: pm1aevtblk: $%p\n", fp->pm1aevtblk);
	printk("acpi: fadt: pm1bevtblk: $%p\n", fp->pm1bevtblk);
	printk("acpi: fadt: pm1acntblk: $%p\n", fp->pm1acntblk);
	printk("acpi: fadt: pm1bcntblk: $%p\n", fp->pm1bcntblk);
	printk("acpi: fadt: pm2cntblk: $%p\n", fp->pm2cntblk);
	printk("acpi: fadt: pmtmrblk: $%p\n", fp->pmtmrblk);
	printk("acpi: fadt: gpe0blk: $%p\n", fp->gpe0blk);
	printk("acpi: fadt: gpe1blk: $%p\n", fp->gpe1blk);
	printk("acpi: fadt: pm1evtlen: $%p\n", fp->pm1evtlen);
	printk("acpi: fadt: pm1cntlen: $%p\n", fp->pm1cntlen);
	printk("acpi: fadt: pm2cntlen: $%p\n", fp->pm2cntlen);
	printk("acpi: fadt: pmtmrlen: $%p\n", fp->pmtmrlen);
	printk("acpi: fadt: gpe0blklen: $%p\n", fp->gpe0blklen);
	printk("acpi: fadt: gpe1blklen: $%p\n", fp->gpe1blklen);
	printk("acpi: fadt: gp1base: $%p\n", fp->gp1base);
	printk("acpi: fadt: cstcnt: $%p\n", fp->cstcnt);
	printk("acpi: fadt: plvl2lat: $%p\n", fp->plvl2lat);
	printk("acpi: fadt: plvl3lat: $%p\n", fp->plvl3lat);
	printk("acpi: fadt: flushsz: $%p\n", fp->flushsz);
	printk("acpi: fadt: flushstride: $%p\n", fp->flushstride);
	printk("acpi: fadt: dutyoff: $%p\n", fp->dutyoff);
	printk("acpi: fadt: dutywidth: $%p\n", fp->dutywidth);
	printk("acpi: fadt: dayalrm: $%p\n", fp->dayalrm);
	printk("acpi: fadt: monalrm: $%p\n", fp->monalrm);
	printk("acpi: fadt: century: $%p\n", fp->century);
	printk("acpi: fadt: iapcbootarch: $%p\n", fp->iapcbootarch);
	printk("acpi: fadt: flags: $%p\n", fp->flags);
	dumpGas("acpi: fadt: resetreg: ", &fp->resetreg);
	printk("acpi: fadt: resetval: $%p\n", fp->resetval);
	printk("acpi: fadt: xfacs: %#llux\n", fp->xfacs);
	printk("acpi: fadt: xdsdt: %#llux\n", fp->xdsdt);
	dumpGas("acpi: fadt: xpm1aevtblk:", &fp->xpm1aevtblk);
	dumpGas("acpi: fadt: xpm1bevtblk:", &fp->xpm1bevtblk);
	dumpGas("acpi: fadt: xpm1acntblk:", &fp->xpm1acntblk);
	dumpGas("acpi: fadt: xpm1bcntblk:", &fp->xpm1bcntblk);
	dumpGas("acpi: fadt: xpm2cntblk:", &fp->xpm2cntblk);
	dumpGas("acpi: fadt: xpmtmrblk:", &fp->xpmtmrblk);
	dumpGas("acpi: fadt: xgpe0blk:", &fp->xgpe0blk);
	dumpGas("acpi: fadt: xgpe1blk:", &fp->xgpe1blk);
}

static struct Atable*
acpifadt(uint8_t *p, int unused)
{
	struct Fadt *fp;

	fp = &fadt;
	fp->facs = l32get(p + 36);
	fp->dsdt = l32get(p + 40);
	fp->pmprofile = p[45];
	fp->sciint = l16get(p+46);
	fp->smicmd = l32get(p+48);
	fp->acpienable = p[52];
	fp->acpidisable = p[53];
	fp->s4biosreq = p[54];
	fp->pstatecnt = p[55];
	fp->pm1aevtblk = l32get(p+56);
	fp->pm1bevtblk = l32get(p+60);
	fp->pm1acntblk = l32get(p+64);
	fp->pm1bcntblk = l32get(p+68);
	fp->pm2cntblk = l32get(p+72);
	fp->pmtmrblk = l32get(p+76);
	fp->gpe0blk = l32get(p+80);
	fp->gpe1blk = l32get(p+84);
	fp->pm1evtlen = p[88];
	fp->pm1cntlen = p[89];
	fp->pm2cntlen = p[90];
	fp->pmtmrlen = p[91];
	fp->gpe0blklen = p[92];
	fp->gpe1blklen = p[93];
	fp->gp1base = p[94];
	fp->cstcnt = p[95];
	fp->plvl2lat = l16get(p+96);
	fp->plvl3lat = l16get(p+98);
	fp->flushsz = l16get(p+100);
	fp->flushstride = l16get(p+102);
	fp->dutyoff = p[104];
	fp->dutywidth = p[105];
	fp->dayalrm = p[106];
	fp->monalrm = p[107];
	fp->century = p[108];
	fp->iapcbootarch = l16get(p+109);
	fp->flags = l32get(p+112);
	gasget(&fp->resetreg, p+116);
	fp->resetval = p[128];
	fp->xfacs = l64get(p+132);
	fp->xdsdt = l64get(p+140);
	gasget(&fp->xpm1aevtblk, p+148);
	gasget(&fp->xpm1bevtblk, p+160);
	gasget(&fp->xpm1acntblk, p+172);
	gasget(&fp->xpm1bcntblk, p+184);
	gasget(&fp->xpm2cntblk, p+196);
	gasget(&fp->xpmtmrblk, p+208);
	gasget(&fp->xgpe0blk, p+220);
	gasget(&fp->xgpe1blk, p+232);

	dumpfadt(fp);
	if(fp->xfacs != 0)
		loadfacs(fp->xfacs);
	else
		loadfacs(fp->facs);

	if(fp->xdsdt == ((uint64_t)fp->dsdt)) /* acpica */
		loaddsdt(fp->xdsdt);
	else
		loaddsdt(fp->dsdt);

	return NULL;	/* can be unmapped once parsed */
}

static void
dumpmsct(struct Msct *msct)
{
	struct Mdom *st;

	printk("acpi: msct: %d doms %d clkdoms %#ullx maxpa\n",
		msct->ndoms, msct->nclkdoms, msct->maxpa);
	for(st = msct->dom; st != NULL; st = st->next)
		printk("\t[%d:%d] %d maxproc %#ullx maxmmem\n",
			st->start, st->end, st->maxproc, st->maxmem);
	printk("\n");
}

/*
 * XXX: should perhaps update our idea of available memory.
 * Else we should remove this code.
 */
static struct Atable*
acpimsct(uint8_t *p, int len)
{
	uint8_t *pe;
	struct Mdom **stl, *st;
	int off;

	msct = kzmalloc(sizeof(struct Msct), KMALLOC_WAIT);
	msct->ndoms = l32get(p+40) + 1;
	msct->nclkdoms = l32get(p+44) + 1;
	msct->maxpa = l64get(p+48);
	msct->dom = NULL;
	stl = &msct->dom;
	pe = p + len;
	off = l32get(p+36);
	for(p += off; p < pe; p += 22){
		st = kzmalloc(sizeof(struct Mdom), KMALLOC_WAIT);
		st->next = NULL;
		st->start = l32get(p+2);
		st->end = l32get(p+6);
		st->maxproc = l32get(p+10);
		st->maxmem = l64get(p+14);
		*stl = st;
		stl = &st->next;
	}

	dumpmsct(msct);
	return NULL;	/* can be unmapped once parsed */
}

static void
dumpsrat(struct Srat *st)
{
	printk("acpi: srat:\n");
	for(; st != NULL; st = st->next)
		switch(st->type){
		case SRlapic:
			printk("\tlapic: dom %d apic %d sapic %d clk %d\n",
				st->lapic.dom, st->lapic.apic,
				st->lapic.sapic, st->lapic.clkdom);
			break;
		case SRmem:
			printk("\tmem: dom %d %#ullx %#ullx %c%c\n",
				st->mem.dom, st->mem.addr, st->mem.len,
				st->mem.hplug?'h':'-',
				st->mem.nvram?'n':'-');
			break;
		case SRlx2apic:
			printk("\tlx2apic: dom %d apic %d clk %d\n",
				st->lx2apic.dom, st->lx2apic.apic,
				st->lx2apic.clkdom);
			break;
		default:
			printk("\t<unknown srat entry>\n");
		}
	printk("\n");
}

static struct Atable*
acpisrat(uint8_t *p, int len)
{
	struct Srat **stl, *st;
	uint8_t *pe;
	int stlen, flags;

	if(srat != NULL){
		printd("acpi: two SRATs?\n");
		return NULL;
	}

	stl = &srat;
	pe = p + len;
	for(p += 48; p < pe; p += stlen){
		st = kzmalloc(sizeof(struct Srat), 1);
		st->type = p[0];
		st->next = NULL;
		stlen = p[1];
		switch(st->type){
		case SRlapic:
			st->lapic.dom = p[2] | p[9]<<24| p[10]<<16 | p[11]<<8;
			st->lapic.apic = p[3];
			st->lapic.sapic = p[8];
			st->lapic.clkdom = l32get(p+12);
			if(l32get(p+4) == 0){
				kfree(st);
				st = NULL;
			}
			break;
		case SRmem:
			st->mem.dom = l32get(p+2);
			st->mem.addr = l64get(p+8);
			st->mem.len = l64get(p+16);
			flags = l32get(p+28);
			if((flags&1) == 0){	/* not enabled */
				kfree(st);
				st = NULL;
			}else{
				st->mem.hplug = flags & 2;
				st->mem.nvram = flags & 4;
			}
			break;
		case SRlx2apic:
			st->lx2apic.dom = l32get(p+4);
			st->lx2apic.apic = l32get(p+8);
			st->lx2apic.clkdom = l32get(p+16);
			if(l32get(p+12) == 0){
				kfree(st);
				st = NULL;
			}
			break;
		default:
			printd("unknown SRAT structure\n");
			kfree(st);
			st = NULL;
		}
		if(st != NULL){
			*stl = st;
			stl = &st->next;
		}
	}

	dumpsrat(srat);
	return NULL;	/* can be unmapped once parsed */
}

static void
dumpslit(struct Slit *sl)
{
	int i;
	
	printk("acpi slit:\n");
	for(i = 0; i < sl->rowlen*sl->rowlen; i++){
		printk("slit: %ux\n", sl->e[i/sl->rowlen][i%sl->rowlen].dist);
	}
}

static int
cmpslitent(void* v1, void* v2)
{
	struct SlEntry *se1, *se2;

	se1 = v1;
	se2 = v2;
	return se1->dist - se2->dist;
}

static struct Atable*
acpislit(uint8_t *p, int len)
{
	uint8_t *pe;
	int i, j, k;
	struct SlEntry *se;

	pe = p + len;
	slit = kzmalloc(sizeof(*slit), 0);
	slit->rowlen = l64get(p+36);
	slit->e = kzmalloc(slit->rowlen * sizeof(struct SlEntry *), 0);
	for(i = 0; i < slit->rowlen; i++)
		slit->e[i] = kzmalloc(sizeof(struct SlEntry) * slit->rowlen, 0);

	i = 0;
	for(p += 44; p < pe; p++, i++){
		j = i/slit->rowlen;
		k = i%slit->rowlen;
		se = &slit->e[j][k];
		se->dom = k;
		se->dist = *p;
	}
	dumpslit(slit);
#warning "no qsort"
#if 0
	for(i = 0; i < slit->rowlen; i++)
		qsort(slit->e[i], slit->rowlen, sizeof(slit->e[0][0]), cmpslitent);
	
	dumpslit(slit);
#endif
	return NULL;	/* can be unmapped once parsed */
}

uintptr_t
acpimblocksize(uintptr_t addr, int *dom)
{
	struct Srat *sl;

	for(sl = srat; sl != NULL; sl = sl->next)
		if(sl->type == SRmem)
		if(sl->mem.addr <= addr && sl->mem.addr + sl->mem.len > addr){
			*dom = sl->mem.dom;
			return sl->mem.len - (addr - sl->mem.addr);
		}
	return 0;
}


/*
 * we use mp->machno (or index in Mach array) as the identifier,
 * but ACPI relies on the apic identifier.
 */
int
corecolor(int core)
{
#warning "can't do core colors yet"
return -1;
#if 0
	struct Srat *sl;
	static int colors[32];

	if(core < 0 || core >= num_cpus)
		return -1;
	m = sys->machptr[core];
	if(m == NULL)
		return -1;

	if(core >= 0 && core < ARRAY_SIZE(colors) && colors[core] != 0)
		return colors[core] - 1;

	for(sl = srat; sl != NULL; sl = sl->next)
		if(sl->type == SRlapic && sl->lapic.apic == m->apicno){
			if(core >= 0 && core < ARRAY_SIZE(colors))
				colors[core] = 1 + sl->lapic.dom;
			return sl->lapic.dom;
		}
	return -1;
#endif
}


int
pickcore(int mycolor, int index)
{
	int color;
	int ncorepercol;

	if(slit == NULL) {
		return 0;
	}
	ncorepercol = num_cpus/slit->rowlen;
	color = slit->e[mycolor][index/ncorepercol].dom;
	return color * ncorepercol + index % ncorepercol;
}


static void
dumpmadt(struct Madt *apics)
{
	struct Apicst *st;

	printk("acpi: madt lapic paddr %llux pcat %d:\n", apics->lapicpa, apics->pcat);
	for(st = apics->st; st != NULL; st = st->next)
		switch(st->type){
		case ASlapic:
			printk("\tlapic pid %d id %d\n", st->lapic.pid, st->lapic.id);
			break;
		case ASioapic:
		case ASiosapic:
			printk("\tioapic id %d addr %#llux ibase %d\n",
				st->ioapic.id, st->ioapic.addr, st->ioapic.ibase);
			break;
		case ASintovr:
			printk("\tintovr irq %d intr %d flags $%p\n",
				st->intovr.irq, st->intovr.intr,st->intovr.flags);
			break;
		case ASnmi:
			printk("\tnmi intr %d flags $%p\n",
				st->nmi.intr, st->nmi.flags);
			break;
		case ASlnmi:
			printk("\tlnmi pid %d lint %d flags $%p\n",
				st->lnmi.pid, st->lnmi.lint, st->lnmi.flags);
			break;
		case ASlsapic:
			printk("\tlsapic pid %d id %d eid %d puid %d puids %s\n",
				st->lsapic.pid, st->lsapic.id,
				st->lsapic.eid, st->lsapic.puid,
				st->lsapic.puids);
			break;
		case ASintsrc:
			printk("\tintr type %d pid %d peid %d iosv %d intr %d %#x\n",
				st->type, st->intsrc.pid,
				st->intsrc.peid, st->intsrc.iosv,
				st->intsrc.intr, st->intsrc.flags);
			break;
		case ASlx2apic:
			printk("\tlx2apic puid %d id %d\n", st->lx2apic.puid, st->lx2apic.id);
			break;
		case ASlx2nmi:
			printk("\tlx2nmi puid %d intr %d flags $%p\n",
				st->lx2nmi.puid, st->lx2nmi.intr, st->lx2nmi.flags);
			break;
		default:
			printk("\t<unknown madt entry>\n");
		}
	printk("\n");
}

static struct Atable*
acpimadt(uint8_t *p, int len)
{
	uint8_t *pe;
	struct Apicst *st, *l, **stl;
	int stlen, id;

	apics = kzmalloc(sizeof(struct Madt), 1);
	apics->lapicpa = l32get(p+36);
	apics->pcat = l32get(p+40);
	apics->st = NULL;
	stl = &apics->st;
	pe = p + len;
	for(p += 44; p < pe; p += stlen){
		st = kzmalloc(sizeof(struct Apicst), 1);
		st->type = p[0];
		st->next = NULL;
		stlen = p[1];
		switch(st->type){
		case ASlapic:
			st->lapic.pid = p[2];
			st->lapic.id = p[3];
			if(l32get(p+4) == 0){
				kfree(st);
				st = NULL;
			}
			break;
		case ASioapic:
			st->ioapic.id = id = p[2];
			st->ioapic.addr = l32get(p+4);
			st->ioapic.ibase = l32get(p+8);
			/* iosapic overrides any ioapic entry for the same id */
			for(l = apics->st; l != NULL; l = l->next)
				if(l->type == ASiosapic && l->iosapic.id == id){
					st->ioapic = l->iosapic;
					/* we leave it linked; could be removed */
					break;
				}
			break;
		case ASintovr:
			st->intovr.irq = p[3];
			st->intovr.intr = l32get(p+4);
			st->intovr.flags = l16get(p+8);
			break;
		case ASnmi:
			st->nmi.flags = l16get(p+2);
			st->nmi.intr = l32get(p+4);
			break;
		case ASlnmi:
			st->lnmi.pid = p[2];
			st->lnmi.flags = l16get(p+3);
			st->lnmi.lint = p[5];
			break;
		case ASladdr:
			/* This is for 64 bits, perhaps we should not
			 * honor it on 32 bits.
			 */
			apics->lapicpa = l64get(p+8);
			break;
		case ASiosapic:
			id = st->iosapic.id = p[2];
			st->iosapic.ibase = l32get(p+4);
			st->iosapic.addr = l64get(p+8);
			/* iosapic overrides any ioapic entry for the same id */
			for(l = apics->st; l != NULL; l = l->next)
				if(l->type == ASioapic && l->ioapic.id == id){
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
			st->lsapic.puid = l32get(p+12);
			if(l32get(p+8) == 0){
				kfree(st);
				st = NULL;
			}else
				kstrdup(&st->lsapic.puids, (char *)p+16);
			break;
		case ASintsrc:
			st->intsrc.flags = l16get(p+2);
			st->type = p[4];
			st->intsrc.pid = p[5];
			st->intsrc.peid = p[6];
			st->intsrc.iosv = p[7];
			st->intsrc.intr = l32get(p+8);
			st->intsrc.any = l32get(p+12);
			break;
		case ASlx2apic:
			st->lx2apic.id = l32get(p+4);
			st->lx2apic.puid = l32get(p+12);
			if(l32get(p+8) == 0){
				kfree(st);
				st = NULL;
			}
			break;
		case ASlx2nmi:
			st->lx2nmi.flags = l16get(p+2);
			st->lx2nmi.puid = l32get(p+4);
			st->lx2nmi.intr = p[8];
			break;
		default:
			printd("unknown APIC structure\n");
			kfree(st);
			st = NULL;
		}
		if(st != NULL){
			*stl = st;
			stl = &st->next;
		}
	}

	dumpmadt(apics);
	return NULL;	/* can be unmapped once parsed */
}

/*
 * Map the table and keep it there.
 */
static struct Atable*
acpitable(uint8_t *p, int len)
{
	if(len < Sdthdrsz) {
		return NULL;
	}
	return newtable(p);
}

static void
dumptable(char *sig, uint8_t *p, int l)
{
	int n, i;

	if(2 > 1){
		printk("%s @ %#p\n", sig, p);
		if(2 > 2)
			n = l;
		else
			n = 256;
		for(i = 0; i < n; i++){
			if((i % 16) == 0)
				printk("%x: ", i);
			printk(" %2.2ux", p[i]);
			if((i % 16) == 15)
				printk("\n");
		}
		printk("\n");
		printk("\n");
	}
}

static char*
seprinttable(char *s, char *e, struct Atable *t)
{
	uint8_t *p;
	int i, n;

	p = ( uint8_t *)t->tbl;	/* include header */
	n = Sdthdrsz + t->dlen;
	s = seprintf(s, e, "%s @ %#p\n", t->sig, p);
	for(i = 0; i < n; i++){
		if((i % 16) == 0)
			s = seprintf(s, e, "%x: ", i);
		s = seprintf(s, e, " %2.2ux", p[i]);
		if((i % 16) == 15)
			s = seprintf(s, e, "\n");
	}
	return seprintf(s, e, "\n\n");
}

/*
 * process xsdt table and load tables with sig, or all if NULL.
 * (XXX: should be able to search for sig, oemid, oemtblid)
 */
static int
acpixsdtload(char *sig)
{
	int i, l, t, unmap, found;
	uintptr_t dhpa;
	uint8_t *sdt;
	char tsig[5];

	found = 0;
	for(i = 0; i < xsdt->len; i += xsdt->asize){
		if(xsdt->asize == 8)
			dhpa = l64get(xsdt->p+i);
		else
			dhpa = l32get(xsdt->p+i);
		if((sdt = sdtmap(dhpa, &l, 1)) == NULL)
			continue;
		unmap = 1;
		memmove(tsig, sdt, 4);
		tsig[4] = 0;
		if(sig == NULL || strcmp(sig, tsig) == 0){
			printk("acpi: %s addr %#p\n", tsig, sdt);
			for(t = 0; t < ARRAY_SIZE(ptables); t++)
				if(strcmp(tsig, ptables[t].sig) == 0){
					dumptable(tsig, sdt, l);
					unmap = ptables[t].f(sdt, l) == NULL;
					found = 1;
					break;
				}
		}
//		if(unmap)
//			vunmap(sdt, l);
	}
	return found;
}

static void*
rsdscan(uint8_t* addr, int len, char* signature)
{
	int sl;
	uint8_t *e, *p;

	printk("SCANNNNNNNNNNNNNNNNNNNNNNNNNNNNN\n");
	e = addr+len;
	sl = strlen(signature);
	for(p = addr; p+sl < e; p += 16){
		if (p == (void *)0xf15c0)
			printk("CHECK F15C0!!!!!!!!!!!!!!!\n");
		if(memcmp(p, signature, sl))
			continue;
		printk("WE GOT %p\n", p);
		return p;
	}

	return NULL;
}

static void*
rsdsearch(char* signature)
{
	uintptr_t p;
	uint8_t *bda;
	void *rsd;

	/*
	 * Search for the data structure signature:
	 * 1) in the BIOS ROM between 0xE0000 and 0xFFFFF.
	 */
	return rsdscan(KADDR(0xE0000), 0x20000, signature);
}

static void
acpirsdptr(void)
{
	struct Rsdp *rsd;
	int asize;
	uintptr_t sdtpa;

	if((rsd = rsdsearch("RSD PTR ")) == NULL) {
		return;
	}

	assert(sizeof(struct Sdthdr) == 36);

	printk("acpi: RSD PTR@ %#p, physaddr $%p length %ud %#llux rev %d\n",
		rsd, l32get(rsd->raddr), l32get(rsd->length),
		l64get(rsd->xaddr), rsd->revision);

	if(rsd->revision >= 2){
		if(sdtchecksum(rsd, 36) == NULL){
			printk("acpi: RSD: bad checksum\n");
			return;
		}
		sdtpa = l64get(rsd->xaddr);
		asize = 8;
	}
	else{
		if(sdtchecksum(rsd, 20) == NULL){
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
	if(xsdt == NULL){
		printk("acpi: malloc failed\n");
		return;
	}
	if((xsdt->p = sdtmap(sdtpa, &xsdt->len, 1)) == NULL){
		printk("acpi: sdtmap failed\n");
		return;
	}
	if((xsdt->p[0] != 'R' && xsdt->p[0] != 'X') || memcmp(xsdt->p+1, "SDT", 3) != 0){
		printk("acpi: xsdt sig: %c%c%c%c\n",
			xsdt->p[0], xsdt->p[1], xsdt->p[2], xsdt->p[3]);
		kfree(xsdt);
		xsdt = NULL;
		//vunmap(xsdt, xsdt->len);
		return;
	}
	xsdt->p += sizeof(struct Sdthdr);
	xsdt->len -= sizeof(struct Sdthdr);
	xsdt->asize = asize;
	printk("acpi: XSDT %#p\n", xsdt);
	acpixsdtload(NULL);
	/* xsdt is kept and not unmapped */

}

static int
acpigen(struct chan *c, char *unused_char_p_t, struct dirtab *tab, int ntab, int i,
	struct dir *dp)
{
	struct qid qid;

	if(i == DEVDOTDOT){
		mkqid(&qid, Qdir, 0, QTDIR);
		devdir(c, qid, ".", 0, eve, 0555, dp);
		return 1;
	}
	i++; /* skip first element for . itself */
	if(tab==0 || i>=ntab) {
		return -1;
	}
	tab += i;
	qid = tab->qid;
	qid.path &= ~Qdir;
	qid.vers = 0;
	devdir(c, qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

void
dumpGas(char *prefix, struct Gas *g)
{
	static char* rnames[] = {
			"mem", "io", "pcicfg", "embed",
			"smb", "cmos", "pcibar", "ipmi"};
	printk("%s", prefix);

	switch(g->spc){
	case Rsysmem:
	case Rsysio:
	case Rembed:
	case Rsmbus:
	case Rcmos:
	case Rpcibar:
	case Ripmi:
		printk("[%s ", rnames[g->spc]);
		break;
	case Rpcicfg:
		printk("[pci ");
		printk("dev %#p ", (uint32_t)(g->addr >> 32) & 0xFFFF);
		printk("fn %#p ", (uint32_t)(g->addr & 0xFFFF0000) >> 16);
		printk("adr %#p ", (uint32_t)(g->addr &0xFFFF));
		break;
	case Rfixedhw:
		printk("[hw ");
		break;
	default:
		printk("[spc=%#p ", g->spc);
	}
	printk("off %d len %d addr %#ullx sz%d]",
		g->off, g->len, g->addr, g->accsz);
}

static unsigned int
getbanked(uintptr_t ra, uintptr_t rb, int sz)
{
	unsigned int r;

	r = 0;
	switch(sz){
	case 1:
		if(ra != 0)
			r |= inb(ra);
		if(rb != 0)
			r |= inb(rb);
		break;
	case 2:
		if(ra != 0)
			r |= inw(ra);
		if(rb != 0)
			r |= inw(rb);
		break;
	case 4:
		if(ra != 0)
			r |= inl(ra);
		if(rb != 0)
			r |= inl(rb);
		break;
	default:
		printd("getbanked: wrong size\n");
	}
	return r;
}

static unsigned int
setbanked(uintptr_t ra, uintptr_t rb, int sz, int v)
{
	unsigned int r;

	r = -1;
	switch(sz){
	case 1:
		if(ra != 0)
			outb(ra, v);
		if(rb != 0)
			outb(rb, v);
		break;
	case 2:
		if(ra != 0)
			outw(ra, v);
		if(rb != 0)
			outw(rb, v);
		break;
	case 4:
		if(ra != 0)
			outl(ra, v);
		if(rb != 0)
			outl(rb, v);
		break;
	default:
		printd("setbanked: wrong size\n");
	}
	return r;
}

static unsigned int
getpm1ctl(void)
{
	return getbanked(fadt.pm1acntblk, fadt.pm1bcntblk, fadt.pm1cntlen);
}

static void
setpm1sts(unsigned int v)
{
	printk("acpi: setpm1sts %#p\n", v);
	setbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen/2, v);
}

static unsigned int
getpm1sts(void)
{
	return getbanked(fadt.pm1aevtblk, fadt.pm1bevtblk, fadt.pm1evtlen/2);
}

static unsigned int
getpm1en(void)
{
	int sz;

	sz = fadt.pm1evtlen/2;
	return getbanked(fadt.pm1aevtblk+sz, fadt.pm1bevtblk+sz, sz);
}

static int
getgpeen(int n)
{
	return inb(gpes[n].enio) & 1<<gpes[n].enbit;
}

static void
setgpeen(int n, unsigned int v)
{
	int old;

	printk("acpi: setgpe %d %d\n", n, v);
	old = inb(gpes[n].enio);
	if(v)
		outb(gpes[n].enio, old | 1<<gpes[n].enbit);
	else
		outb(gpes[n].enio, old & ~(1<<gpes[n].enbit));
}

static void
clrgpests(int n)
{
	outb(gpes[n].stsio, 1<<gpes[n].stsbit);
}

static unsigned int
getgpests(int n)
{
	return inb(gpes[n].stsio) & 1<<gpes[n].stsbit;
}

#warning "no acpi interrupts yet"
#if 0
static void
acpiintr(Ureg*, void*)
{
	int i;
	unsigned int sts, en;

	printd("acpi: intr\n");

	for(i = 0; i < ngpes; i++)
		if(getgpests(i)){
			printd("gpe %d on\n", i);
 			en = getgpeen(i);
			setgpeen(i, 0);
			clrgpests(i);
			if(en != 0)
				printd("acpiitr: calling gpe %d\n", i);
		//	queue gpe for calling gpe->ho in the
		//	aml process.
		//	enable it again when it returns.
		}
	sts = getpm1sts();
	en = getpm1en();
	printd("acpiitr: pm1sts %#p pm1en %#p\n", sts, en);
	if(sts&en)
		printd("have enabled events\n");
	if(sts&1)
		printd("power button\n");
	// XXX serve other interrupts here.
	setpm1sts(sts);	
}
#endif
static void
initgpes(void)
{
	int i, n0, n1;

	n0 = fadt.gpe0blklen/2;
	n1 = fadt.gpe1blklen/2;
	ngpes = n0 + n1;
	gpes = kzmalloc(sizeof(struct Gpe) * ngpes, 1);
	for(i = 0; i < n0; i++){
		gpes[i].nb = i;
		gpes[i].stsbit = i&7;
		gpes[i].stsio = fadt.gpe0blk + (i>>3);
		gpes[i].enbit = (n0 + i)&7;
		gpes[i].enio = fadt.gpe0blk + ((n0 + i)>>3);
	}
	for(i = 0; i + n0 < ngpes; i++){
		gpes[i + n0].nb = fadt.gp1base + i;
		gpes[i + n0].stsbit = i&7;
		gpes[i + n0].stsio = fadt.gpe1blk + (i>>3);
		gpes[i + n0].enbit = (n1 + i)&7;
		gpes[i + n0].enio = fadt.gpe1blk + ((n1 + i)>>3);
	}
	for(i = 0; i < ngpes; i++){
		setgpeen(i, 0);
		clrgpests(i);
	}
}

static void
acpiioalloc(unsigned int addr, int len)
{
	if(addr != 0){
		printk("Just TAKING port %016lx to %016lx\n", 
		       addr, addr + len);
		//ioalloc(addr, len, 0, "acpi");
	}
}

int
acpiinit(void)
{
	if(fadt.smicmd == 0){
		//fmtinstall('G', Gfmt);
		acpirsdptr();
		if(fadt.smicmd == 0) {
			return -1;
		}
	}
	return 0;
}

static struct chan*
acpiattach(char *spec)
{
	int i;

	printk("ACPI attach\n");
	/*
	 * This was written for the stock kernel.
	 * This code must use 64 registers to be acpi ready in nix.
	 */
	if(acpiinit() < 0){
		printk("ACPIINIT is called\n");
		error("no acpi");
	}

	/*
	 * should use fadt->xpm* and fadt->xgpe* registers for 64 bits.
	 * We are not ready in this kernel for that.
	 */
	printk("acpi io alloc\n");
	acpiioalloc(fadt.smicmd, 1);
	acpiioalloc(fadt.pm1aevtblk, fadt.pm1evtlen);
	acpiioalloc(fadt.pm1bevtblk, fadt.pm1evtlen );
	acpiioalloc(fadt.pm1acntblk, fadt.pm1cntlen);
	acpiioalloc(fadt.pm1bcntblk, fadt.pm1cntlen);
	acpiioalloc(fadt.pm2cntblk, fadt.pm2cntlen);
	acpiioalloc(fadt.pmtmrblk, fadt.pmtmrlen);
	acpiioalloc(fadt.gpe0blk, fadt.gpe0blklen);
	acpiioalloc(fadt.gpe1blk, fadt.gpe1blklen);

	printk("acpi init gpes\n");
	initgpes();

	/*
	 * This starts ACPI, which may require we handle
	 * power mgmt events ourselves. Use with care.
	 */
	printk("acpi NOT starting\n");
	if (0){
	outb(fadt.smicmd, fadt.acpienable);
	for(i = 0; i < 10; i++)
		if(getpm1ctl() & Pm1SciEn)
			break;
	if(i == 10)
		error("acpi: failed to enable\n");
//	if(fadt.sciint != 0)
//		intrenable(fadt.sciint, acpiintr, 0, BUSUNKNOWN, "acpi");
	}
	return devattach('a', spec);
}

static struct walkqid*
acpiwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static int
acpistat(struct chan *c, uint8_t *dp, int n)
{
	return devstat(c, dp, n, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static struct chan*
acpiopen(struct chan *c, int omode)
{
	return devopen(c, omode, acpidir, ARRAY_SIZE(acpidir), acpigen);
}

static void
acpiclose(struct chan *unused)
{
}

static char*ttext;
static int tlen;

static long
acpiread(struct chan *c, void *a, long n, int64_t off)
{
	long q;
	struct Atable *t;
	char *ns, *s, *e, *ntext;

	q = c->qid.path;
	switch(q){
	case Qdir:
		return devdirread(c, a, n, acpidir, ARRAY_SIZE(acpidir), acpigen);
	case Qtbl:
		if(ttext == NULL){
			tlen = 1024;
			ttext = kzmalloc(tlen, 0);
			if(ttext == NULL){
				printd("acpi: no memory\n");
				return 0;
			}
			s = ttext;
			e = ttext + tlen;
			strncpy(s,  "no tables\n", sizeof(s));
			for(t = tfirst; t != NULL; t = t->next){
				ns = seprinttable(s, e, t);
				while(ns == e - 1){
					printk("acpiread: allocated %d\n", tlen*2);
					ntext = krealloc(ttext, tlen*2, 0);
					if(ntext == NULL)
						panic("acpi: no memory\n");
					s = ntext + (ttext - s);
					ttext = ntext;
					tlen *= 2;
					e = ttext + tlen;
					ns = seprinttable(s, e, t);
				}
				s = ns;
			}
					
		}
		return readstr(off, a, n, ttext);
	case Qio:
		if(reg == NULL)
			error("region not configured");
		return regio(reg, a, n, off, 0);
	}
	error(Eperm);
	return -1;
}

static long
acpiwrite(struct chan *c, void *a, long n, int64_t off)
{
	ERRSTACK(2);
	struct cmdtab *ct;
	struct cmdbuf *cb;
	struct Reg *r;
	unsigned int rno, fun, dev, bus, i;

	if(c->qid.path == Qio){
		if(reg == NULL)
			error("region not configured");
		return regio(reg, a, n, off, 1);
	}
	if(c->qid.path != Qctl)
		error(Eperm);

	cb = parsecmd(a, n);
	if(waserror()){
		kfree(cb);
		nexterror();
	}
	ct = lookupcmd(cb, ctls, ARRAY_SIZE(ctls));
	printk("acpi ctl %s\n", cb->f[0]);
	switch(ct->index){
	case CMregion:
		r = reg;
		if(r == NULL){
			r = kzmalloc(sizeof(struct Reg), 0);
			r->name = NULL;
		}
		kstrdup(&r->name, cb->f[1]);
		r->spc = acpiregid(cb->f[2]);
		if(r->spc < 0){
			kfree(r);
			reg = NULL;
			error("bad region type");
		}
		if(r->spc == Rpcicfg || r->spc == Rpcibar){
			rno = r->base>>Rpciregshift & Rpciregmask;
			fun = r->base>>Rpcifunshift & Rpcifunmask;
			dev = r->base>>Rpcidevshift & Rpcidevmask;
			bus = r->base>>Rpcibusshift & Rpcibusmask;
#define MKBUS(t,b,d,f)	(((t)<<24)|(((b)&0xFF)<<16)|(((d)&0x1F)<<11)|(((f)&0x07)<<8))
			r->tbdf = MKBUS(0/*BusPCI*/, bus, dev, fun);
			r->base = rno;	/* register ~ our base addr */
		}
		r->base = strtoul(cb->f[3], NULL, 0);
		r->len = strtoul(cb->f[4], NULL, 0);
		r->accsz = strtoul(cb->f[5], NULL, 0);
		if(r->accsz < 1 || r->accsz > 4){
			kfree(r);
			reg = NULL;
			error("bad region access size");
		}
		reg = r;
		printk("region %s %s %p %p sz%d",
			r->name, acpiregstr(r->spc), r->base, r->len, r->accsz);
		break;
	case CMgpe:
		i = strtoul(cb->f[1], NULL, 0);
		if(i >= ngpes)
			error("gpe out of range");
		kstrdup(&gpes[i].obj, cb->f[2]);
		printk("gpe %d %s\n", i, gpes[i].obj);
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
