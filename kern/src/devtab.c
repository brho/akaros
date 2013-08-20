/*
 * Stub.
 */
#define DEBUG
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
#include <umem.h>

/* at some point this will be done via ldscript Hackes. */
//extern struct dev* devtab[];

extern struct dev regressdevtab;
extern struct dev pipedevtab;
extern struct dev procdevtab;
extern struct dev miscdevtab;
struct dev *devtab[] = {
	&miscdevtab,
	&regressdevtab,
	&pipedevtab,
	&procdevtab,
	NULL,
};

void
devtabreset()
{
	int i;

	for(i = 0; devtab[i] != NULL; i++)
		devtab[i]->reset(current);
}

void
devtabinit()
{
	int i;

	for(i = 0; devtab[i] != NULL; i++)
		devtab[i]->init(current);
}

void
devtabshutdown()
{
	int i;

	/*
	 * Shutdown in reverse order.
	 */
	for(i = 0; devtab[i] != NULL; i++)
		;
	for(i--; i >= 0; i--)
		devtab[i]->shutdown(current);
}


struct dev*
devtabget(int dc, int user, struct errbuf *perrbuf)
{
	int i;

	for(i = 0; devtab[i] != NULL; i++){
		if(devtab[i]->dc == dc)
			return devtab[i];
	}

	printd("devtabget %c\n", dc, perrbuf);
	error(Enonexist);
}

long
devtabread(struct chan*c, void* buf, long n, int64_t off, struct errbuf *perrbuf)
{
	ERRSTACK(1);

	int i;
	struct dev *dev;
	char *alloc, *e, *p;

	alloc = kzmalloc(READSTR, KMALLOC_WAIT);
	if(alloc == NULL)
	  error(Enomem);

	p = alloc;
	e = p + READSTR;
	for(i = 0; devtab[i] != NULL; i++){
		dev = devtab[i];
printd("p %p e %p e-p %d\n", p, e, e-p);
printd("do %d %c %s\n", i, dev->dc, dev->name);
		p += snprintf(p, e-p, "#%c %s\n", dev->dc, dev->name);
	}

	if(waserror()){
		kfree(alloc);
		nexterror();
	}
	n = readstr(off, buf, n, alloc);
	
	kfree(alloc);

	return n;
}
