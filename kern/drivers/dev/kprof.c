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


#define LRES	3		/* log of PC resolution */
#define CELLSIZE	8		/* sizeof of count cell; well known as 4 */

struct
{
	uintptr_t	minpc;
	uintptr_t	maxpc;
	int	nbuf;
	int	time;
	uint64_t	*buf;
	spinlock_t lock;
}kprof;

enum{
	Kprofdirqid,
	Kprofdataqid,
	Kprofctlqid,
};
struct dirtab kproftab[]={
	{".",		{Kprofdirqid, 0, QTDIR},0,	DMDIR|0550},
	{"kpdata",	{Kprofdataqid},		0,	0600},
	{"kpctl",	{Kprofctlqid},		0,	0600},
};

static struct chan*
kprofattach(char *spec)
{
	uint32_t n;

	/* allocate when first used */
	kprof.minpc = KERN_LOAD_ADDR;
	kprof.maxpc = (uintptr_t)&etext;
	kprof.nbuf = (kprof.maxpc-kprof.minpc) >> LRES;
	n = kprof.nbuf*CELLSIZE;
	if(kprof.buf == 0) {
		printk("Allocate %d bytes\n", n);
		kprof.buf = kzmalloc(n, KMALLOC_WAIT);
		if(kprof.buf == 0)
			error(Enomem);
	}
	kproftab[1].length = n;
	return devattach('K', spec);
}

static void
_kproftimer(uintptr_t pc)
{
	if(kprof.time == 0)
		return;

	/*
	 * if the pc corresponds to the idle loop, don't consider it.

	if(m->inidle)
		return;
	 */
	/*
	 *  if the pc is coming out of spllo or splx,
	 *  use the pc saved when we went splhi.

	if(pc>=PTR2UINT(spllo) && pc<=PTR2UINT(spldone))
		pc = m->splpc;
	 */

//	ilock(&kprof);
#warning "how do we fill out this timing info"
#if 0
	kprof.buf[0] += TK2MS(1);
	if(kprof.minpc<=pc && pc<kprof.maxpc){
		pc -= kprof.minpc;
		pc >>= LRES;
		kprof.buf[pc] += TK2MS(1);
	}else
		kprof.buf[1] += TK2MS(1);
#endif
//	iunlock(&kprof);
}

static void
kprofinit(void)
{
	if(CELLSIZE != sizeof kprof.buf[0])
		panic("kprof size");
#warning "how do we set up this timer"
	//kproftimer = _kproftimer;
}

static struct walkqid*
kprofwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static int
kprofstat(struct chan *c, uint8_t *db, int n)
{
	return devstat(c, db, n, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static struct chan*
kprofopen(struct chan *c, int omode)
{
	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Eperm);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
kprofclose(struct chan*unused)
{
}

static long
kprofread(struct chan *c, void *va, long n, int64_t off)
{
	uintptr_t end;
	uint64_t w, *bp;
	uint8_t *a, *ea;
	uintptr_t offset = off;

	switch((int)c->qid.path){
	case Kprofdirqid:
		return devdirread(c, va, n, kproftab, ARRAY_SIZE(kproftab), devgen);

	case Kprofdataqid:
		end = kprof.nbuf*CELLSIZE;
		if(offset & (CELLSIZE-1))
			error(Ebadarg);
		if(offset >= end){
			n = 0;
			break;
		}
		if(offset+n > end)
			n = end-offset;
		n &= ~(CELLSIZE-1);
		a = va;
		ea = a + n;
		bp = kprof.buf + offset/CELLSIZE;
		while(a < ea){
			w = *bp++;
			/* I'd really like not to have to worry
			 * about 32 bit machines any more!
			 */
			*a++ = w>>56;
			*a++ = w>>48;
			*a++ = w>>40;
			*a++ = w>>32;
			*a++ = w>>24;
			*a++ = w>>16;
			*a++ = w>>8;
			*a++ = w>>0;
		}
		break;

	default:
		n = 0;
		break;
	}
	return n;
}

static long
kprofwrite(struct chan *c, void *a, long n, int64_t unused)
{
	switch((int)(c->qid.path)){
	case Kprofctlqid:
		if(strncmp(a, "startclr", 8) == 0){
			memset((char *)kprof.buf, 0, kprof.nbuf*CELLSIZE);
			kprof.time = 1;
		}else if(strncmp(a, "start", 5) == 0)
			kprof.time = 1;
		else if(strncmp(a, "stop", 4) == 0)
			kprof.time = 0;
		break;
	default:
		error(Ebadusefd);
	}
	return n;
}

struct dev kprofdevtab __devtab = {
	'K',
	"kprof",

	devreset,
	kprofinit,
	devshutdown,
	kprofattach,
	kprofwalk,
	kprofstat,
	kprofopen,
	devcreate,
	kprofclose,
	kprofread,
	devbread,
	kprofwrite,
	devbwrite,
	devremove,
	devwstat,
};
