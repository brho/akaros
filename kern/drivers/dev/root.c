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


enum
{
	Qdir = 0,
	Qbin = 0x1000,
	Qlib = 0x2000,

	Nrootfiles = 32,
	Nbinfiles = 256,
	Nlibfiles = 256,
};

struct dirlist
{
	unsigned int base;
	struct dirtab *dir;
	uint8_t **data;
	int ndir;
	int mdir;
};

static struct dirtab rootdir[Nrootfiles] = {
	{"#/",	{Qdir, 0, QTDIR},	0,		DMDIR|0555},
	{"bin",	{Qbin, 0, QTDIR},	0,		DMDIR|0555},
	{"lib",	{Qlib, 0, QTDIR},	0,		DMDIR|0555},
};
static uint8_t *rootdata[Nrootfiles];
static struct dirlist rootlist =
{
	0,
	rootdir,
	rootdata,
	3,
	Nrootfiles
};

static struct dirtab bindir[Nbinfiles] = {
	{"bin",	{Qbin, 0, QTDIR},	0,		DMDIR|0555},
};
static uint8_t *bindata[Nbinfiles];
static struct dirlist binlist =
{
	Qbin,
	bindir,
	bindata,
	1,
	Nbinfiles
};

static struct dirtab libdir[Nlibfiles] = {
	{"lib",	{Qlib, 0, QTDIR},	0,		DMDIR|0555},
};
static uint8_t *libdata[Nlibfiles];
static struct dirlist liblist =
{
	Qlib,
	libdir,
	libdata,
	1,
	Nlibfiles
};

/*
 *  add a file to the list
 */
static void
addlist(struct dirlist *l, char *name, uint8_t *contents, uint32_t len, int perm)
{
	struct dirtab *d;

	if(l->ndir >= l->mdir)
		panic("too many root files");
	l->data[l->ndir] = contents;
	d = &l->dir[l->ndir];
	strncpy(d->name,  name, sizeof(d->name));
	d->length = len;
	d->perm = perm;
	d->qid.type = 0;
	d->qid.vers = 0;
	d->qid.path = ++l->ndir + l->base;
	if(perm & DMDIR)
		d->qid.type |= QTDIR;
}

/*
 *  add a file
 */
void
addfile(struct dirlist *d, char *name, uint8_t *contents, uint32_t len)
{
	addlist(d, name, contents, len, 0555);
}

/*
 *  add a root directory
 */
static void
addrootdir(char *name)
{
	addlist(&rootlist, name, NULL, 0, DMDIR|0555);
}

static void
rootreset(void)
{
	addrootdir("dev");
	addrootdir("env");
	addrootdir("fd");
	addrootdir("mnt");
	addrootdir("net");
	addrootdir("net.alt");
	addrootdir("proc");
	addrootdir("root");
	addrootdir("srv");
	addfile(&binlist, "hi", "this is bin", strlen("this is bin"));
	addfile(&liblist, "hilib", "this is lib", strlen("this is lib"));
}

static struct chan*
rootattach(char *spec)
{
	return devattach('r', spec);
}

static int
rootgen(struct chan *c, char *name, struct dirtab*unused_d, int unused_i, int s, struct dir *dp)
{
	int t;
	struct dirtab *d;
	struct dirlist *l;

	switch((int)c->qid.path){
	case Qdir:
		if(s == DEVDOTDOT){
			devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		return devgen(c, name, rootlist.dir, rootlist.ndir, s, dp);
	case Qbin:
		if(s == DEVDOTDOT){
			devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		return devgen(c, name, binlist.dir, liblist.ndir, s, dp);
	case Qlib:
		if(s == DEVDOTDOT){
			devdir(c, (struct qid){Qlib, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		return devgen(c, name, liblist.dir, liblist.ndir, s, dp);
	default:
		if(s == DEVDOTDOT){
			if((int)c->qid.path < Qbin)
				devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			else if((int)c->qid.path < Qlib)
				devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			else
				devdir(c, (struct qid){Qlib, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		if(s != 0)
			return -1;
		if((int)c->qid.path < Qbin){
			t = c->qid.path-1;
			l = &rootlist;
		}else if((int)c->qid.path < Qlib){
			t = c->qid.path - Qbin - 1;
			l = &binlist;
		
		}else {
			t = c->qid.path - Qlib - 1;
			l = &liblist;
		}
		if(t >= l->ndir)
			return -1;
if(t < 0){
printd("rootgen %#llux %d %d\n", c->qid.path, s, t);
panic("whoops");
}
		d = &l->dir[t];
		devdir(c, d->qid, d->name, d->length, eve, d->perm, dp);
		return 1;
	}
}

static struct walkqid*
rootwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
printd("rootwalk nname %d name[0] %s\n", nname, name[0]);
	return devwalk(c,  nc, name, nname, NULL, 0, rootgen);
}

static long
rootstat(struct chan *c, uint8_t *dp, long n)
{
	return devstat(c, dp, n, NULL, 0, rootgen);
}

static struct chan*
rootopen(struct chan *c, int omode)
{
	return devopen(c, omode, NULL, 0, devgen);
}

/*
 * sysremove() knows this is a nop
 */
static void
rootclose(struct chan*c)
{
}

static long
rootread(struct chan *c, void *buf, long n, int64_t off)
{
	uint32_t t;
	struct dirtab *d;
	struct dirlist *l;
	uint8_t *data;
	uint32_t offset = off;

	t = c->qid.path;
	switch(t){
	case Qdir:
	case Qbin:
	case Qlib:
		return devdirread(c, buf, n, NULL, 0, rootgen);
	}

	if(t<Qbin)
		l = &rootlist;
	else if (t<Qlib){
		t -= Qbin;
		l = &binlist;
	}
	else{
		t -= Qlib;
		l = &liblist;
	}

	t--;
	if(t >= l->ndir)
		error(Egreg);

	d = &l->dir[t];
	data = l->data[t];
	if(offset >= d->length)
		return 0;
	if(offset+n > d->length)
		n = d->length - offset;
	memmove(buf, data+offset, n);
	return n;
}

static long
rootwrite(struct chan*c, void*v, long l, int64_t o)
{
	error(Egreg);
	return 0;
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
	devcreate,
	rootclose,
	rootread,
	devbread,
	rootwrite,
	devbwrite,
	devremove,
	devwstat,
};

