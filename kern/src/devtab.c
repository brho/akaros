/*
 * Stub.
 */
//#define DEBUG
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

struct dev *devtab[] = {
    &regressdevtab,
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

	if(user == 0)
		panic("devtabget %C\n", dc, perrbuf);

	return NULL;
}

long
devtabread(struct chan*c, void* buf, long n, int64_t off, struct errbuf *perrbuf)
{
	int ret;

	int i;
	struct dev *dev;
	char *alloc, *e, *p;

	alloc = kzmalloc(/*READSTR*/4096, KMALLOC_WAIT);
	if(alloc == NULL)
	  error(Enomem);

	p = alloc;
	e = p + 4096; //READSTR;
	for(i = 0; devtab[i] != NULL; i++){
		dev = devtab[i];
		/* hmm.
		p = seprint(p, e, "#%C %s\n", dev->dc, dev->name, perrbuf);
		*/
		p += snprintf(p, e-p, "#%C %s\n", dev->dc, dev->name, perrbuf);
	}

	/*
	errstack = perrbuf; perrbuf = errbuf;
	if(waserror()){
		kfree(alloc);
		nexterror(errstack);
	}
	*/
	//n = readstr(off, buf, n, alloc, perrbuf);
	// akaros assumes it all went. 
	// and no support for offsets. 
	// and it won't throw an error, so no need for any waserror.
	ret = memcpy_to_user(current, buf, alloc, n);
	
	kfree(alloc);

	return ret;
}
