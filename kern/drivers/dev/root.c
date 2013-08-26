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
	Qboot = 0x1000,

	Nrootfiles = 32,
	Nbootfiles = 32,
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
	{"boot",	{Qboot, 0, QTDIR},	0,		DMDIR|0555},
};
static uint8_t *rootdata[Nrootfiles];
static struct dirlist rootlist =
{
	0,
	rootdir,
	rootdata,
	2,
	Nrootfiles
};

static struct dirtab bootdir[Nbootfiles] = {
	{"boot",	{Qboot, 0, QTDIR},	0,		DMDIR|0555},
};
static uint8_t *bootdata[Nbootfiles];
static struct dirlist bootlist =
{
	Qboot,
	bootdir,
	bootdata,
	1,
	Nbootfiles
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
 *  add a root file
 */
void
addbootfile(char *name, uint8_t *contents, uint32_t len)
{
	addlist(&bootlist, name, contents, len, 0555);
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
	addrootdir("bin");
	addrootdir("dev");
	addrootdir("env");
	addrootdir("fd");
	addrootdir("mnt");
	addrootdir("net");
	addrootdir("net.alt");
	addrootdir("proc");
	addrootdir("root");
	addrootdir("srv");
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
	case Qboot:
		if(s == DEVDOTDOT){
			devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		return devgen(c, name, bootlist.dir, bootlist.ndir, s, dp);
	default:
		if(s == DEVDOTDOT){
			if((int)c->qid.path < Qboot)
				devdir(c, (struct qid){Qdir, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			else
				devdir(c, (struct qid){Qboot, 0, QTDIR}, "#/", 0, eve, 0555, dp);
			return 1;
		}
		if(s != 0)
			return -1;
		if((int)c->qid.path < Qboot){
			t = c->qid.path-1;
			l = &rootlist;
		}else{
			t = c->qid.path - Qboot - 1;
			l = &bootlist;
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
	case Qboot:
		return devdirread(c, buf, n, NULL, 0, rootgen);
	}

	if(t<Qboot)
		l = &rootlist;
	else{
		t -= Qboot;
		l = &bootlist;
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

