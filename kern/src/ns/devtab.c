/*
 * Stub.
 */
//#define DEBUG
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

void devtabreset(void)
{
	ERRSTACK(1);
	volatile int i;

	if (waserror()) {
		panic("A devtab reset (probably %p) failed!", devtab[i].reset);
		poperror();
		return;
	}
	for (i = 0; &devtab[i] < __devtabend; i++) {
		printk("%s: #%s\n", __func__, devtab[i].name);
		if (devtab[i].reset)
			devtab[i].reset();
	}
	poperror();
	printk("%s complete\n", __func__);
}

void devtabinit(void)
{
	ERRSTACK(1);
	volatile int i;

	if (waserror()) {
		panic("A devtab init (probably %p) failed!", devtab[i].init);
		poperror();
		return;
	}
	for (i = 0; &devtab[i] < __devtabend; i++) {
		/* if we have errors, check the align of struct dev and objdump
		 */
		printd("i %d, '%s', dev %p, init %p\n", i, devtab[i].name,
		       &devtab[i], devtab[i].init);
		printk("%s: #%s\n", __func__, devtab[i].name);
		if (devtab[i].init)
			devtab[i].init();
	}
	poperror();
	printk("%s complete\n", __func__);
}

void devtabshutdown(void)
{
	int i;

	/*
	 * Shutdown in reverse order.
	 */
	for (i = 0; &devtab[i] < __devtabend; i++) ;
	for (i--; i >= 0; i--) {
		if (devtab[i].shutdown)
			devtab[i].shutdown();
	}
}

struct dev *devtabget(const char *name, int user)
{
	int i = devno(name, user);

	if (i > 0)
		return &devtab[i];

	printk("devtabget FAILED %s\n", name);
	error(ENOENT, ERROR_FIXME);
}

long devtabread(struct chan *c, void *buf, long n, int64_t off)
{
	ERRSTACK(1);

	int i;
	struct dev *dev;
	char *alloc, *e, *p;

	alloc = kzmalloc(READSTR, MEM_WAIT);
	if (alloc == NULL)
		error(ENOMEM, ERROR_FIXME);

	p = alloc;
	e = p + READSTR;
	for (i = 0; &devtab[i] < __devtabend; i++) {
		dev = &devtab[i];
		printd("p %p e %p e-p %d\n", p, e, e - p);
		printd("do %d %s\n", i, dev->name);
		p += snprintf(p, e - p, "#%s\n", dev->name);
	}

	if (waserror()) {
		kfree(alloc);
		nexterror();
	}
	n = readstr(off, buf, n, alloc);

	kfree(alloc);
	poperror();

	return n;
}
