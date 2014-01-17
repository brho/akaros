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

/* at some point this will be done via ldscript Hackes. */
//extern struct dev* devtab[];
// the sooner the better!

extern struct dev alarmdevtab;
extern struct dev consdevtab;
/*extern struct dev regressdevtab;
extern struct dev pipedevtab;
extern struct dev procdevtab;
extern struct dev etherdevtab;
extern struct dev rootdevtab;
extern struct dev ipdevtab;
extern struct dev mntdevtab;
extern struct dev srvdevtab;
extern struct dev vmdevtab;*/
struct dev *devtab[] = {
	&alarmdevtab,
	&consdevtab,
/*
	&rootdevtab,
	&regressdevtab,
	&pipedevtab,
	&procdevtab,
	&etherdevtab,
	&ipdevtab,
	&mntdevtab,
	&srvdevtab,
	&vmdevtab,*/
	NULL,
};

void devtabreset()
{
	int i;
	printk("devtabresets\n");

	for (i = 0; devtab[i] != NULL; i++)
		devtab[i]->reset();
}

void devtabinit()
{
	int i;

	printk("devtabinit\n");
	for (i = 0; devtab[i] != NULL; i++)
		devtab[i]->init();
}

void devtabshutdown()
{
	int i;

	/*
	 * Shutdown in reverse order.
	 */
	for (i = 0; devtab[i] != NULL; i++) ;
	for (i--; i >= 0; i--)
		devtab[i]->shutdown();
}

struct dev *devtabget(int dc, int user)
{
	int i;

	for (i = 0; devtab[i] != NULL; i++) {
		if (devtab[i]->dc == dc)
			return devtab[i];
	}

	printk("devtabget FAILED %c\n", dc);
	error(Enonexist);
}

long
devtabread(struct chan *c, void *buf, long n, int64_t off)
{
	ERRSTACK(1);

	int i;
	struct dev *dev;
	char *alloc, *e, *p;

	alloc = kzmalloc(READSTR, KMALLOC_WAIT);
	if (alloc == NULL)
		error(Enomem);

	p = alloc;
	e = p + READSTR;
	for (i = 0; devtab[i] != NULL; i++) {
		dev = devtab[i];
		printd("p %p e %p e-p %d\n", p, e, e - p);
		printd("do %d %c %s\n", i, dev->dc, dev->name);
		p += snprintf(p, e - p, "#%c %s\n", dev->dc, dev->name);
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
