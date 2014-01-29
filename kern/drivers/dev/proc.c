//#define DEBUG
/* Copyright 2014 Google Inc.
 * Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * devvm/#V: a device for VMs
 *
 */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <sys/queue.h>
#include <smp.h>
#include <kref.h>
#include <atomic.h>
#include <alarm.h>
#include <event.h>
#include <umem.h>
#include <devalarm.h>
#include <arch/types.h>
#include <arch/vm.h>
#include <arch/emulate.h>
#include <arch/vmdebug.h>

#define ADDR_SHIFT 32
#define QID2PID(q) ((struct vm*)KADDR(((q).path >> ADDR_SHIFT)))
#define TYPE(q) ((q).path & ((1 << ADDR_SHIFT) - 1))
#define QID(pid, type) ((PADDR(pid) << ADDR_SHIFT) | type)

/* the QID definition allows 2^32 procs and
 * 2^32 types.
 */
enum {
	Qtopdir = 1,
	Qprocdir,
	Qctl,
	Qns,
};

static int procgen(struct chan *c, char *entry_name,
				   struct dirtab *unused, int unused_nr_dirtab,
				   int s, struct dir *dp)
{
	print_func_entry();
	struct qid q;

	printd("GEN s %d\n", s);
	/* Whether we're in one dir or at the top, .. still takes us to the top. */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, c->qid, "#p", 0, eve, 0555, dp);
		print_func_exit();
		return 1;
	}
	printd("TYPE %d\n", TYPE(c->qid));
	switch (TYPE(c->qid)) {
		case Qtopdir:
			printd("Qtopdir s %d nvm %d\n", s, nvm);
			return 1;
		case Qprocdir:
			/* Gen the contents of the proc dirs */
			s += Qctl;	/* first time through, start on Qctl */
			switch (s) {
				case Qctl:
					mkqid(&q, QID(QID2PID(c->qid), Qctl), 0, QTFILE);
					devdir(c, q, "ctl", 0, eve, 0666, dp);
					print_func_exit();
					return 1;
				case Qns:
					mkqid(&q, QID(QID2PID(c->qid), Qns), 0, QTFILE);
					devdir(c, q, "image", 0, eve, 0666, dp);
					print_func_exit();
					return 1;
			}
			print_func_exit();
			return -1;
			/* Need to also provide a direct hit for Qclone and all other files (at
			 * all levels of the hierarchy).  Every file is both
			 * generated (via the s increments in their respective directories) and
			 * directly gen-able.  devstat() will call gen with a specific path in
			 * the qid.  In these cases, we make a dir for whatever they are asking
			 * for.  Note the qid stays the same.  I think this is what the old
			 * plan9 comments above devgen were talking about for (ii).
			 *
			 * We don't need to do this for the directories - devstat will look for
			 * the a directory by path and fail.  Then it will manually build the
			 * stat output (check the -1 case in devstat). */
		case Qclone:
			devdir(c, c->qid, "clone", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		case Qstat:
			devdir(c, c->qid, "stat", 0, eve, 0444, dp);
			print_func_exit();
			return 1;
		case Qctl:
			devdir(c, c->qid, "ctl", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
		case Qns:
			devdir(c, c->qid, "image", 0, eve, 0666, dp);
			print_func_exit();
			return 1;
	}
	print_func_exit();
	return -1;
}

static void vminit(void)
{
	return;
	print_func_entry();
	int i;
	spinlock_init_irqsave(&vmlock);
	spinlock_init_irqsave(vmidlock);
	i = vmx_init();
	printk("vminit: litevm_init returns %d\n", i);

	print_func_exit();
}

static struct chan *vmattach(char *spec)
{
	print_func_entry();
	struct chan *c = devattach('V', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	print_func_exit();
	return c;
}

static struct walkqid *vmwalk(struct chan *c, struct chan *nc, char **name,
							  int nname)
{
	print_func_entry();
	print_func_exit();
	return devwalk(c, nc, name, nname, 0, 0, vmgen);
}

static int vmstat(struct chan *c, uint8_t * db, int n)
{
	print_func_entry();
	print_func_exit();
	return devstat(c, db, n, 0, 0, vmgen);
}

/* It shouldn't matter if p = current is DYING.  We'll eventually fail to insert
 * the open chan into p's fd table, then decref the chan. */
static struct chan *vmopen(struct chan *c, int omode)
{
	print_func_entry();
	ERRSTACK(1);
	struct vm *v = QID2PID(c->qid);
	printk("vmopen: v is %p\n", v);
	if (waserror()) {
		nexterror();
	}
	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qvmdir:
			if (omode & ORCLOSE)
				error(Eperm);
			if (!IS_RDONLY(omode))
				error(Eisdir);
			break;
		case Qclone:
			spin_lock_irqsave(&vmlock);
			vms = krealloc(vms, sizeof(vms[0]) * (nvm + 1), 0);
			v = &vms[nvm];
			nvm++;
			spin_unlock(&vmlock);
			kref_init(&v->kref, vm_release, 1);
			v->id = newvmid();
			mkqid(&c->qid, QID(v, Qctl), 0, QTFILE);
			c->aux = v;
			printd("New VM id %d\n", v->id);
			v->archvm = vmx_open();
			if (!v->archvm) {
				printk("vm_open failed\n");
				error("vm_open failed");
			}
			if (vmx_create_vcpu(v->archvm, v->id) < 0) {
				printk("vm_create failed");
				error("vm_create failed");
			}
			break;
		case Qstat:
			break;
		case Qctl:
		case Qns:
			c->aux = QID2PID(c->qid);
			printk("open qctl: aux is %p\n", c->aux);
			break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	poperror();
	print_func_exit();
	return c;
}

static void vmcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	print_func_entry();
	error(Eperm);
	print_func_exit();
}

static void vmremove(struct chan *c)
{
	print_func_entry();
	error(Eperm);
	print_func_exit();
}

static int vmwstat(struct chan *c, uint8_t * dp, int n)
{
	print_func_entry();
	error("No vmwstat");
	print_func_exit();
	return 0;
}

static void vmclose(struct chan *c)
{
	print_func_entry();
	struct vm *v = c->aux;
	if (!v) {
		print_func_exit();
		return;
	}
	/* There are more closes than opens.  For instance, sysstat doesn't open,
	 * but it will close the chan it got from namec.  We only want to clean
	 * up/decref chans that were actually open. */
	if (!(c->flag & COPEN)) {
		print_func_exit();
		return;
	}
	switch (TYPE(c->qid)) {
			/* for now, leave the VM active even when we close ctl */
		case Qctl:
			break;
		case Qns:
			kref_put(&v->kref);
			break;
	}
	print_func_exit();
}

static long vmread(struct chan *c, void *ubuf, long n, int64_t offset)
{
	print_func_entry();
	struct vm *v = c->aux;
	printd("VMREAD\n");
	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qvmdir:
			print_func_exit();
			return devdirread(c, ubuf, n, 0, 0, vmgen);
		case Qstat:
			print_func_exit();
			return readnum(offset, ubuf, n, nvm, NUMSIZE32);
		case Qctl:
			assert(v);
			print_func_exit();
			return readnum(offset, ubuf, n, v->id, NUMSIZE32);
		case Qns:
			assert(v);
			print_func_exit();
			return readmem(offset, ubuf, n, v->image, v->imagesize);
		default:
			panic("Bad QID %p in devvm", c->qid.path);
	}
	print_func_exit();
	return 0;
}

static long vmwrite(struct chan *c, void *ubuf, long n, int64_t unused)
{
	print_func_entry();
	ERRSTACK(3);
	char buf[32];
	struct cmdbuf *cb;
	struct vm *vm;
	struct litevm *litevm;
	uint64_t hexval;
	printd("vmwrite(%p, %p, %d)\n", c, ubuf, n);
	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qvmdir:
		case Qstat:
			error(Eperm);
		case Qctl:
			vm = c->aux;
			cb = parsecmd(ubuf, n);
			if (waserror()) {
				kfree(cb);
				nexterror();
			}
			if (!strcmp(cb->f[0], "run")) {
				int ret;
				if (cb->nf != 4)
					error("usage: run vcpu emulated mmio_completed");
				litevm = vm->archvm;
				struct litevm_run vmr;
				vmr.vcpu = strtoul(cb->f[1], NULL, 0);
				vmr.emulated = strtoul(cb->f[2], NULL, 0);
				vmr.mmio_completed = strtoul(cb->f[3], NULL, 0);
				ret = vm_run(litevm, &vmr);
				printk("vm_run returns %d\n", ret);
				print_func_exit();
				return ret;
			} else if (!strcmp(cb->f[0], "stop")) {
				error("can't stop a vm yet");
			} else if (!strcmp(cb->f[0], "fillmem")) {
				struct chan *file;
				void *v;
				vm = c->aux;
				litevm = vm->archvm;
				uint64_t filesize;
				struct litevm_memory_region vmr;
				int got;

				if (cb->nf != 6)
					error("usage: mapmem file slot flags addr size");
				vmr.slot = strtoul(cb->f[2], NULL, 0);
				vmr.flags = strtoul(cb->f[3], NULL, 0);
				vmr.guest_phys_addr = strtoul(cb->f[4], NULL, 0);
				filesize = strtoul(cb->f[5], NULL, 0);
				vmr.memory_size = (filesize + 4095) & ~4095ULL;

				file = namec(cb->f[1], Aopen, OREAD, 0);
				printk("after namec file is %p\n", file);
				if (waserror()) {
					cclose(file);
					nexterror();
				}
				/* at some point we want to mmap from the kernel
				 * but we don't have that yet. This all needs
				 * rethinking but the abstractions of kvm do too.
				 */
				v = kmalloc(vmr.memory_size, KMALLOC_WAIT);
				if (waserror()) {
					kfree(v);
					nexterror();
				}

				readn(file, v, filesize);
				vmr.init_data = v;

				if (vm_set_memory_region(litevm, &vmr))
					error("vm_set_memory_region failed");
				poperror();
				poperror();
				kfree(v);
				cclose(file);

			} else if (!strcmp(cb->f[0], "region")) {
				void *v;
				struct litevm_memory_region vmr;
				litevm = vm->archvm;
				if (cb->nf != 5)
					error("usage: mapmem slot flags addr size");
				vmr.slot = strtoul(cb->f[2], NULL, 0);
				vmr.flags = strtoul(cb->f[3], NULL, 0);
				vmr.guest_phys_addr = strtoul(cb->f[4], NULL, 0);
				vmr.memory_size = strtoul(cb->f[5], NULL, 0);
				if (vm_set_memory_region(litevm, &vmr))
					error("vm_set_memory_region failed");
			} else {
				error("%s: not implemented", cb->f[0]);
			}
			kfree(cb);
			poperror();
			break;
		case Qns:
			error("Can't write namespace this way");
			break;
		default:
			panic("Bad QID %p in devvm", c->qid.path);
	}
	print_func_exit();
	return n;
}

struct dev procdevtab __devtab = {
	'p',
	"proc",

	devreset,
	procinit,
	devshutdown,
	procattach,
	procwalk,
	procstat,
	procopen,
	proccreate,
	procclose,
	procread,
	devbread,
	procwrite,
	devbwrite,
	procremove,
	procwstat,
	devpower,
//  devconfig,
	devchaninfo,
};
