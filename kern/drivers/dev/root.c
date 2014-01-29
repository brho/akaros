// INFERNO
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
#include <ip.h>

/* courtesy of the inferno mkroot script. */
int rootmaxq = 13;
struct dirtab roottab[13] = {
	{"",	{0, 0, QTDIR},	 0,	0555},
	{"chan",	{1, 0, QTDIR},	 0,	0555},
	{"dev",	{2, 0, QTDIR},	 0,	0555},
	{"fd",	{3, 0, QTDIR},	 0,	0555},
	{"prog",	{4, 0, QTDIR},	 0,	0555},
	{"prof",	{5, 0, QTDIR},	 0,	0555},
	{"net",	{6, 0, QTDIR},	 0,	0555},
	{"net.alt",	{7, 0, QTDIR},	 0,	0555},
	{"nvfs",	{8, 0, QTDIR},	 0,	0555},
	{"env",	{9, 0, QTDIR},	 0,	0555},
	{"root",	{10, 0, QTDIR},	 0,	0555},
	{"srv",	{11, 0, QTDIR},	 0,	0555},
	/* not courtesy of mkroot */
	{"mnt",	{12, 0, QTDIR},	 0,	0555},
};
struct rootdata rootdata[13] = {
	{0,	 &roottab[1],	 12,	NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
	{0,	 NULL,	 0,	 NULL},
};

static struct chan*
rootattach(char *spec)
{
	int i;
	uint32_t len;
	struct rootdata *r;

	if(*spec)
		error(Ebadspec);
	for (i = 0; i < rootmaxq; i++){
		r = &rootdata[i];
		if (r->sizep){
			len = *r->sizep;
			r->size = len;
			roottab[i].length = len;
		}
	}
	return devattach('r', spec);
}

static int
rootgen(struct chan *c, char *name,
	struct dirtab *tab, int nd, int s, struct dir *dp)
{
	int p, i;
	struct rootdata *r;

	if(s == DEVDOTDOT){
		p = rootdata[c->qid.path].dotdot;
		c->qid.path = p;
		c->qid.type = QTDIR;
		name = "#r";
		if(p != 0){
			for(i = 0; i < rootmaxq; i++)
				if(roottab[i].qid.path == c->qid.path){
					name = roottab[i].name;
					break;
				}
		}
		devdir(c, c->qid, name, 0, eve, 0555, dp);
		return 1;
	}
	if(name != NULL){
		isdir(c);
		r = &rootdata[(int)c->qid.path];
		tab = r->ptr;
		for(i=0; i<r->size; i++, tab++)
			if(strcmp(tab->name, name) == 0){
				devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
				return 1;
			}
		return -1;
	}
	if(s >= nd)
		return -1;
	tab += s;
	devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static struct walkqid*
rootwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	uint32_t p;

	p = c->qid.path;
	if(nname == 0)
		p = rootdata[p].dotdot;
	return devwalk(c, nc, name, nname, rootdata[p].ptr, rootdata[p].size, rootgen);
}

static int
rootstat(struct chan *c, uint8_t *dp, int n)
{
	int p;

	p = rootdata[c->qid.path].dotdot;
	return devstat(c, dp, n, rootdata[p].ptr, rootdata[p].size, rootgen);
}

static struct chan*
rootopen(struct chan *c, int omode)
{
	int p;

	p = rootdata[c->qid.path].dotdot;
	return devopen(c, omode, rootdata[p].ptr, rootdata[p].size, rootgen);
}

/*
 * sysremove() knows this is a nop
 */
static void	 
rootclose(struct chan *c)
{
}

static long	 
rootread(struct chan *c, void *buf, long n, int64_t offset)
{
	uint32_t p, len;
	uint8_t *data;

	p = c->qid.path;
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, rootdata[p].ptr, rootdata[p].size, rootgen);
	len = rootdata[p].size;
	if(offset < 0 || offset >= len)
		return 0;
	if(offset+n > len)
		n = len - offset;
	data = rootdata[p].ptr;
	memmove(buf, data+offset, n);
	return n;
}

static long	 
rootwrite(struct chan *c, void *a, long n, int64_t off)
{
	error(Eperm);
	return 0;
}

struct dev rootdevtab __devtab = {
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
	devpower,
	devchaninfo,
};
