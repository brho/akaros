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

enum {
	Qdir = 0,
	Qctl = 1,
	Qmalloc,
	Qmax,
};

static struct dirtab regressdir[Qmax] = {
    {".",		{ Qdir, 0, QTDIR },	0,	0555,},
    {"regressctl",	{ Qctl, 0 },	0,	0666,},
    {"malloc",	{ Qmalloc, 0 },	0,	0666,},
};

int verbose = 0;

static struct chan*
regressattach(char* spec, struct errbuf *perrbuf)
{
	return devattach('Z', spec, perrbuf);
}

struct walkqid*
regresswalk(struct chan* c, struct chan *nc, char** name, int nname, struct errbuf *perrbuf)
{
    return devwalk(c, nc, name, nname, regressdir, Qmax, devgen, perrbuf);
}

static long
regressstat(struct chan* c, uint8_t* dp, long n, struct errbuf *perrbuf)
{
	return devstat(c, dp, n, regressdir, Qmax, devgen, perrbuf);
}

static struct chan*
regressopen(struct chan* c, int omode, struct errbuf *perrbuf)
{
    return devopen(c, omode, regressdir, Qmax, devgen, perrbuf);
}

static void
regressclose(struct chan*c, struct errbuf *perrbuf)
{
}

static long
regressread(struct chan *c, void *a, long n, int64_t offset, struct errbuf *perrbuf)
{
	char *buf, *p;
printd("regressread %d\n", (uint32_t)c->qid.path);

	switch((uint32_t)c->qid.path){

	case Qdir:
		return devdirread(c, a, n, regressdir, Qmax, devgen, perrbuf);

	case Qmalloc:
		break;

	default:
	    error(Eperm);
		break;
	}

	return n;
}

static long
regresswrite(struct chan *c, void *a, long n, int64_t offset, struct errbuf *perrbuf)
{
	char *p;
	unsigned long amt;

	switch((uint32_t)c->qid.path){

	case Qmalloc:
	    printd("hi\n");
		return n;

	case Qctl:
		p = a;
		if (*p == 'v'){
			if (verbose)
				verbose--;
		} else if (*p == 'V')
			verbose++;
		else
		    error("Only v or V");
		printd("Regression verbosity now %d\n", verbose);
		return n;
		
	default:
		printd("devreg eperm\n");
	    error(Eperm);
		break;
	}
	return 0;
}

struct dev regressdevtab = {
	'Z',
	"regress",

	devreset,
	devinit,
	devshutdown,
	regressattach,
	regresswalk,
	regressstat,
	regressopen,
	devcreate,
	regressclose,
	regressread,
	devbread,
	regresswrite,
	devbwrite,
	devremove,
	devwstat,
};

