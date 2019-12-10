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
	{".",		{Monitordirqid, 0, QTDIR},	0,	DMDIR|0550},
	{"mondata",	{Monitordataqid},		0,	0600},
	{"monctl",	{Monitorctlqid},		0,	0600},
};

static char *ctlcommands = "ktest";

static struct chan *regressattach(char *spec)
{
	uint32_t n;

	regress.monitor = qopen(2 << 20, 0, 0, 0);
	if (! regress.monitor) {
		printk("monitor allocate failed. No monitor output\n");
	}
	return devattach(devname(), spec);
}

static void regressinit(void)
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

static struct chan *regressopen(struct chan *c, int omode)
{
	if (c->qid.type & QTDIR) {
		if (openmode(omode) != O_READ)
			error(EPERM, ERROR_FIXME);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void regressclose(struct chan *unused)
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
		n = devdirread(c, va, n, regresstab, ARRAY_SIZE(regresstab),
			       devgen);
		break;

	case Monitorctlqid:
		n = readstr(off, va, n, ctlcommands);
		break;

	case Monitordataqid:
		if (regress.monitor) {
			printd("monitordataqid: regress.monitor %p len %p\n",
			       regress.monitor, qlen(kprof.monitor));
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

int __tlb_bench_x;

static void __tlb_s(void)
{
	tlbflush();
	cmb();	/* tlbflush is asm volatile, but it can still be reordered. */
	WRITE_ONCE(__tlb_bench_x, 1);
}

static void __tlb_s_ipi(struct hw_trapframe *hw_tf, void *data)
{
	__tlb_s();
}

static void __tlb_s_kmsg(uint32_t srcid, long a0, long a1, long a2)
{
	__tlb_s();
}

/* This runs the test from the calling core, which is typically core 0 if you
 * are running from the shell.  If you run from another core, note that
 * deregister_irq() will synchronize_rcu, which moves this thread to core 0 at
 * the end of the function. */
static void __tlb_shootdown_bench(int target_core, int mode)
{
	ERRSTACK(1);
	uint64_t s, *d;
	const char *str = NULL;
	struct irq_handler *irqh;
	int tbdf = MKBUS(BusIPI, 0, 0, 0);
	#define ITERS 10

	if (target_core == core_id())
		error(EINVAL, "TLB bench: Aborting, we are core %d",
		      target_core);
	if (target_core < 0 || target_core >= num_cores)
		error(EINVAL,
		      "TLB bench: Aborting, target_core %d out of range",
		      target_core);
	irqh = register_irq(I_TESTING, __tlb_s_ipi, NULL, tbdf);
	if (!irqh)
		error(EFAIL,
		      "TLB bench: Oh crap, we couldn't register the IRQ!");
	d = kmalloc(sizeof(uint64_t) * ITERS, MEM_WAIT);
	if (waserror()) {
		deregister_irq(irqh->apic_vector, tbdf);
		kfree(d);
		nexterror();
	}
	for (int i = 0; i < ITERS; i++) {
		__tlb_bench_x = 0;
		s = start_timing();
		switch (mode) {
		case 1:
			str = "NOOP";
			__tlb_bench_x = 1;
			break;
		case 2:
			tlbflush();
			str = "LOCAL";
			__tlb_bench_x = 1;
			break;
		case 3:
			/* To run this test, you need to hacked this into
			 * POKE_HANDLER.  If not, you'll wedge the machine.
				mov %cr3,%rax;\
				mov %rax,%cr3;\
				incl __tlb_bench_x;\
			* And comment out the error(). */
			error(EFAIL, "TLB bench: hack the POKE_HANDLER");

			send_ipi(target_core, I_POKE_CORE);
			str = "POKE";
			while (!READ_ONCE(__tlb_bench_x))
				cpu_relax();
			break;
		case 4:
			send_ipi(target_core, I_TESTING);
			str = "IPI";
			while (!READ_ONCE(__tlb_bench_x))
				cpu_relax();
			break;
		case 5:
			send_kernel_message(target_core, __tlb_s_kmsg, 0, 0, 0,
					    KMSG_IMMEDIATE);
			str = "KMSG";
			while (!READ_ONCE(__tlb_bench_x))
				cpu_relax();
			break;
		case 6:
			send_kernel_message(target_core, __tlb_s_kmsg, 0, 0, 0,
					    KMSG_IMMEDIATE);
			str = "NOACK-KMSG";
			break;
		case 7:
			send_ipi(target_core, I_TESTING);
			str = "NOACK-IPI";
			break;
		default:
			error(EINVAL, "TLB bench: bad mode %d", mode);
		}
		d[i] = stop_timing(s);
		/* The NOACKs still need to wait, so we don't race with the
		 * remote core and our *next* loop. */
		while (!READ_ONCE(__tlb_bench_x))
			cpu_relax();
		/* The remote core has signalled it did the TLB flush, but it
		 * takes a little while for it to halt or otherwise get back to
		 * idle.  Wait a little to get a more stable measurement.
		 * Without this delay (or something similar), I've seen extra
		 * delays of close to 400ns.  Note that in real usage, the
		 * remote core won't always be ready to handle the IRQ, so this
		 * test is best case. */
		udelay(1000);
	}
	for (int i = 0; i < ITERS; i++)
		printk("%02d: TLB %s shootdown: %llu ns\n", i, str,
		       tsc2nsec(d[i]));
	deregister_irq(irqh->apic_vector, tbdf);
	kfree(d);
	poperror();
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

	switch ((int)(c->qid.path)) {
	case Monitorctlqid:
		if (cb->nf < 1)
			error(EFAIL, "%s no command, need %s", __func__,
			      ctlcommands);
		if (!strcmp(cb->f[0], "ktest")) {
			run_registered_ktest_suites();
		} else if (!strcmp(cb->f[0], "tlb")) {
			if (cb->nf < 3)
				error(EFAIL,
				      "TLB bench: need core and mode (ints)");
			__tlb_shootdown_bench(strtol(cb->f[1], NULL, 10),
					      strtol(cb->f[2], NULL, 10));
		} else {
			error(EFAIL, "regresswrite: only commands are %s",
			      ctlcommands);
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
