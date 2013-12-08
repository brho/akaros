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
#include <kmalloc.h>

enum {
	Qdir = 0,

	Nfilesperdirtab = 128,
	Nfiles = 16384,
};

struct dirlist {
	struct dirtab *me, *dot, *dotdot;
	int ndir;
	int mdir;
	uint8_t *data;
};

static int nextdirent = 1;

static struct dirtab rootdir[Nfilesperdirtab] = {
	{"#/", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
};

/* statically allocate all the dirlist structs.
 * dynamically allocate dirtabs. Memory is cheap,
 * let's be cache friendly when we can.
 */
static struct dirlist all[Nfiles] = {
	{&rootdir[0], &rootdir[0], &rootdir[0], 1, Nfilesperdirtab, NULL},
};
static struct dirlist *rootlist = &all[0];

/*
 *  add a file to the list
 */
static int
addlist(struct dirlist *l, char *name, uint8_t * contents, uint32_t len,
		int perm)
{
	struct dirtab *dot = l->dot;
	struct dirtab *me;
	struct dirlist *newl;
	int mydirent;
	int newtype = 0;

	if (nextdirent >= Nfiles)
		panic("Too many files; max %d\n", Nfiles);
	if (l->ndir >= l->mdir)
		return -1;
	mydirent = nextdirent++;
	/* TODO: next dirent should be a kref_t */
	newl = &all[mydirent];
	newl->data = contents;
	me = &dot[l->ndir];
	if (perm & DMDIR) {
		newl->me = kmalloc(sizeof(struct dirtab)*Nfilesperdirtab,KMALLOC_WAIT);
		newl->dot = newl->me;
		newl->ndir = 1; /* me */
		newtype = QTDIR;
		newl->dot[0].qid.type = QTDIR;
		newl->dot[0].qid.vers = 0;
		newl->dot[0].qid.path = mydirent;
		newl->dot[0].name[0] = '.';
		perm |= DMDIR;
		newl->dot[0].perm = perm;
		newl->mdir = Nfilesperdirtab;
	} else {
		newl->dot = l->dot;
		newl->me = me;
		newl->dotdot = l->dotdot;
	}
	l->ndir++;
	strncpy(me->name, name, sizeof(me->name));
	me->length = len;
	me->perm = perm;
	me->qid.type = newtype;
	me->qid.vers = 0;
	me->qid.path = mydirent;
	printd("Added %s, path %d\n", name, me->qid.path);
	return me->qid.path;
}

/*
 *  add a file
 */
int addfile(struct dirlist *d, char *name, uint8_t * contents, uint32_t len)
{
	return addlist(d, name, contents, len, 0666);
}

/*
 *  add a root directory
 */
static int addrootdir(char *name)
{
	return addlist(rootlist, name, NULL, 0, DMDIR | 0555);
}

static void rootreset(void)
{
	addrootdir("bin");
	addrootdir("lib");
	addrootdir("dev");
	addrootdir("env");
	addrootdir("fd");
	addrootdir("mnt");
	addrootdir("net");
	addrootdir("net.alt");
	addrootdir("proc");
	addrootdir("root");
	addrootdir("srv");
	addrootdir("tmp");
}

static struct chan *rootattach(char *spec)
{
	return devattach('r', spec);
}

static int
rootgen(struct chan *c, char *name, struct dirtab *unused_d, int unused_i,
		int s, struct dir *dp)
{
	int t;
	struct dirtab *d;
	struct dirlist *l = &all[(int)c->qid.path];
	printd("rootgen: l %p %d\n", l,(int)c->qid.path);
	/* for directories, set c->aux to devlist, for later use in create() */
	printd("rootgen: s %d, DEVDOTDOT %d\n", s, DEVDOTDOT);
	if (s == DEVDOTDOT){
		/* TODO: path */
		devdir(c, l->dotdot->qid, l->me->name, 0, eve, 0555, dp);
		return 1;
	}
	if (c->qid.type & QTDIR){
		printd("devgen dir c %p l %p name %s ndir %d\n", c, l, name, l->ndir);
		return devgen(c, name, l->me, l->ndir, s, dp);
	}

	t = c->qid.path;
	printd("rootgen: path %d\n", t);

	d = l->me;
	printd("rootgen: devdir %s\n", name);
	devdir(c, d->qid, d->name, d->length, eve, d->perm, dp);
	return 1;
}

static struct walkqid *rootwalk(struct chan *c, struct chan *nc, char **name,
								int nname)
{
	struct walkqid *ret;
	ret = devwalk(c, nc, name, nname, NULL, 0, rootgen);
	printd("rootwalk c %p c->aux %p \n", c, c ? c->aux : NULL);
	return ret;
}

static long rootstat(struct chan *c, uint8_t * dp, long n)
{
	return devstat(c, dp, n, NULL, 0, rootgen);
}

static void rootcreate(struct chan *c, char *name, int omode, int perm)
{
	int path;
	struct dirlist *l;

	if (c->qid.type != QTDIR)
		error(Eperm);
	if (strlen(name) > MAX_PATH_LEN)
		error("name too long");
	if (omode & DMDIR)
		error("no dir creation yet");

	l = &all[(int)c->qid.path];

	omode = openmode(omode);
	/* let's hope somebody checked to see if it existed yet. */
	path = addfile(l, name, NULL, 0);

	memset(&c->qid, 0, sizeof(c->qid));
	c->qid.path = path;
	c->offset = 0;
	c->mode = omode;
	c->flag |= COPEN;
}

static struct chan *rootopen(struct chan *c, int omode)
{
	return devopen(c, omode, NULL, 0, devgen);
}

/*
 * sysremove() knows this is a nop
 */
static void rootclose(struct chan *c)
{
}

static long rootread(struct chan *c, void *buf, long n, int64_t off)
{
	uint32_t t;
	struct dirtab *d;
	struct dirlist *l;
	uint8_t *data;
	uint32_t offset = off;

	t = c->qid.path;
	l = &all[t];
	if (c->qid.type & QTDIR){
		return devdirread(c, buf, n, NULL, 0, rootgen);
	}
	d = l->me;
	t--;

	data = l->data;
	if (offset >= d->length)
		return 0;
	if (offset + n > d->length)
		n = d->length - offset;
	memmove(buf, data + offset, n);
	return n;
}

static long rootwrite(struct chan *c, void *v, long len, int64_t o)
{
	uint32_t t;
	struct dirtab *d;
	struct dirlist *l;
	uint8_t *data;

	t = c->qid.path;
	l = &all[t];
	if (c->qid.type & QTDIR){
		error(Eisdir);
		return -1;
	}

	d = l->me;
	data = l->data;
	if (o < 0)
		o = d->length;
	if ((o + len) > d->length) {
		void *newdata = kmalloc(o + len, KMALLOC_WAIT);
		l->data = newdata;
		memmove(newdata, data, d->length);
		kfree(data);
		data = newdata;
	}
	printd("rootwrite @ %lld, %d bytes\n", o, len);
	memmove(data + o, v, len);
	d->length = o + len;

	return len;
}

struct dev rootdevtab = {
	'r',
	"root",

	rootreset,
	devinit,
	devshutdown,
	rootattach,
	rootwalk,
	rootstat,
	rootopen,
	rootcreate,
	rootclose,
	rootread,
	devbread,
	rootwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
	devconfig,
	devchaninfo,
};
