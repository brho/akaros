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

/* trivial root file system for testing. bind #r /
 * then do stuff like
 * bind '#p' /proc
 */
enum {
	Qdir = 0,
	Qnet = 1,
	Qproc,
	Qbin,
	Qmax,
};

static struct dirtab rootdir[Qmax] = {
	{".", {Qdir, 0, QTDIR}, 0, 0555,},
	{"net", {Qnet, 0, QTDIR}, 0, 0555,},
	{"proc", {Qproc, 0, QTDIR}, 0, 0555,},
	{"bin", {Qbin, 0, QTDIR}, 0, 0555,},
};

static struct chan *rootattach(char *spec, struct errbuf *perrbuf)
{
	return devattach('r', spec, perrbuf);
}

struct walkqid *rootwalk(struct chan *c, struct chan *nc, char **name,
							int nname, struct errbuf *perrbuf)
{
	return devwalk(c, nc, name, nname, rootdir, Qmax, devgen, perrbuf);
}

static long
rootstat(struct chan *c, uint8_t * dp, long n, struct errbuf *perrbuf)
{
	return devstat(c, dp, n, rootdir, Qmax, devgen, perrbuf);
}

static struct chan *rootopen(struct chan *c, int omode,
			     struct errbuf *perrbuf)
{
	return devopen(c, omode, rootdir, Qmax, devgen, perrbuf);
}

static void rootclose(struct chan *c, struct errbuf *perrbuf)
{
}

static long
rootread(struct chan *c, void *a, long n, int64_t offset,
			struct errbuf *perrbuf)
{
	char *buf, *p;
	static char ctl[128];
	printd("rootread %d\n", (uint32_t) c->qid.path);

	switch ((uint32_t) c->qid.path) {

		case Qdir:
		case Qnet:
		case Qproc:
		case Qbin:
			return devdirread(c, a, n, rootdir, Qmax, devgen, perrbuf);
			/* you may have files some day! */
		default:
			error(Eperm);
			break;
	}

	return n;
}

static long
rootwrite(struct chan *c, void *a, long n, int64_t offset,
			 struct errbuf *perrbuf)
{
	char *p;
	unsigned long amt;

	switch ((uint32_t) c->qid.path) {
		default:
			printd("devroot eperm\n");
			error(Eperm);
			break;
	}
	return 0;
}

struct dev rootdevtab = {
	'r',
	"root",

	devreset,
	devinit,
	devshutdown,
	rootattach,
	rootwalk,
	rootstat,
	rootopen,
	devcreate,
	rootclose,
	rootread,
	devbread,
	rootwrite,
	devbwrite,
	devremove,
	devwstat,
};
