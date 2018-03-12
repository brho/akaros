/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

// regression device.
// Currently, has only one file, monitor, which is used to send
// commands to the monitor.
// TODO: read them back :-)

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
#include <net/ip.h>
#include <monitor.h>
#include <ktest.h>

struct dev regressdevtab;

static char *devname(void)
{
	return regressdevtab.name;
}

struct regress
{
	spinlock_t lock;
	struct queue *monitor;
};
struct regress regress;

enum{
	Monitordirqid = 0,
	Monitordataqid,
	Monitorctlqid,
};

struct dirtab regresstab[]={
	{".",		{Monitordirqid, 0, QTDIR},0,	DMDIR|0550},
	{"mondata",	{Monitordataqid},		0,	0600},
	{"monctl",	{Monitorctlqid},		0,	0600},
};

static char *ctlcommands = "ktest";

static struct chan*
regressattach(char *spec)
{
	uint32_t n;

	regress.monitor = qopen(2 << 20, 0, 0, 0);
	if (! regress.monitor) {
		printk("monitor allocate failed. No monitor output\n");
	}
	return devattach(devname(), spec);
}

static void
regressinit(void)
{
}

static struct walkqid *regresswalk(struct chan *c, struct chan *nc, char **name,
                                   unsigned int nname)
{
	return devwalk(c, nc, name, nname, regresstab, ARRAY_SIZE(regresstab),
	               devgen);
}

static size_t regressstat(struct chan *c, uint8_t *db, size_t n)
{
	if (regress.monitor)
		regresstab[Monitordataqid].length = qlen(regress.monitor);
	else
		regresstab[Monitordataqid].length = 0;

	return devstat(c, db, n, regresstab, ARRAY_SIZE(regresstab), devgen);
}

static struct chan*
regressopen(struct chan *c, int omode)
{
	if(c->qid.type & QTDIR){
		if(openmode(omode) != O_READ)
			error(EPERM, ERROR_FIXME);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
regressclose(struct chan*unused)
{
}

static size_t regressread(struct chan *c, void *va, size_t n, off64_t off)
{
	uint64_t w, *bp;
	char *a, *ea;
	uintptr_t offset = off;
	uint64_t pc;
	int snp_ret, ret = 0;

	switch((int)c->qid.path){
	case Monitordirqid:
		n = devdirread(c, va, n, regresstab, ARRAY_SIZE(regresstab), devgen);
		break;

	case Monitorctlqid:
		n = readstr(off, va, n, ctlcommands);
		break;

	case Monitordataqid:
		if (regress.monitor) {
			printd("monitordataqid: regress.monitor %p len %p\n", regress.monitor, qlen(kprof.monitor));
			if (qlen(regress.monitor) > 0)
				n = qread(regress.monitor, va, n);
			else
				n = 0;
		} else
			error(EFAIL, "no monitor queue");
		break;
	default:
		n = 0;
		break;
	}
	return n;
}

static size_t regresswrite(struct chan *c, void *a, size_t n, off64_t unused)
{
	ERRSTACK(1);
	uintptr_t pc;
	struct cmdbuf *cb;
	cb = parsecmd(a, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	switch((int)(c->qid.path)){
	case Monitorctlqid:
		if(strncmp(a, "ktest", 5) == 0){
			run_registered_ktest_suites();
		} else {
			error(EFAIL, "regresswrite: only commands are %s", ctlcommands);
		}
		break;

	case Monitordataqid:
		if (onecmd(cb->nf, cb->f, NULL) < 0)
			n = -1;
		break;
	default:
		error(EBADFD, ERROR_FIXME);
	}
	kfree(cb);
	poperror();
	return n;
}

struct dev regressdevtab __devtab = {
	.name = "regress",

	.reset = devreset,
	.init = regressinit,
	.shutdown = devshutdown,
	.attach = regressattach,
	.walk = regresswalk,
	.stat = regressstat,
	.open = regressopen,
	.create = devcreate,
	.close = regressclose,
	.read = regressread,
	.bread = devbread,
	.write = regresswrite,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
};
