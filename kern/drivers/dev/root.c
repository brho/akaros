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
/* make it a power of 2 and nobody gets hurt */
#define MAXFILE 1024
int rootmaxq = MAXFILE;
int inumber = 13;
/* this gives you some idea of how much I like linked lists. Just make
 * a big old table. Later on we can put next and prev indices into the
 * data if we want but, our current kfs is 1-3 levels deep and very small
 * (< 200 entries) so I doubt we'll need to do that. It just makes debugging
 * memory a tad easier.
 */
/* we pack the qid as follows: path is the index, vers is ., and type is type */
struct dirtab roottab[MAXFILE] = {
	{"",	{0, 0, QTDIR},	 0,	0777},
	{"chan",	{1, 0, QTDIR},	 0,	0777},
	{"dev",	{2, 0, QTDIR},	 0,	0777},
	{"fd",	{3, 0, QTDIR},	 0,	0777},
	{"prog",	{4, 0, QTDIR},	 0,	0777},
	{"prof",	{5, 0, QTDIR},	 0,	0777},
	{"net",	{6, 0, QTDIR},	 0,	0777},
	{"net.alt",	{7, 0, QTDIR},	 0,	0777},
	{"nvfs",	{8, 0, QTDIR},	 0,	0777},
	{"env",	{9, 0, QTDIR},	 0,	0777},
	{"root",	{10, 0, QTDIR},	 0,	0777},
	{"srv",	{11, 0, QTDIR},	 0,	0777},
	/* not courtesy of mkroot */
	{"mnt",	{12, 0, QTDIR},	 0,	0777},
};

struct rootdata
{
	int	dotdot;
	void	*ptr;
	int	size;
	int	*sizep;
};

struct rootdata rootdata[MAXFILE] = {
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
		devdir(c, c->qid, name, 0, eve, 0777, dp);
		return 1;
	}

	if(name != NULL){
		int path = c->qid.path;
		isdir(c);
		r = &rootdata[path];
		tab = r->ptr;
		/* it's almost always at or after your current spot. */
		for(i=0; i<r->size; i++, tab++)
			if(strcmp(tab->name, name) == 0){
				devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
				return 1;
			}
		/* well we tried. */
		/* Round up the usual suspects. Search every level. */
		/* luckily this is pretty fast. */
		tab = roottab;
		r = rootdata;
		for(i=0; i<path; i++, tab++, r++)
			if(tab->name && (roottab[r->dotdot].qid.path == c->qid.path) &&
			   (strcmp(tab->name, name) == 0)){
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
	if (0){
		printk("rootwalk: c %p. ", c);
		if (nname){
			int i;
			for(i = 0; i < nname-1; i++)
				printk("%s/", name[i]);
			printk("%s", name[i]);
		}
	}
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

static void
rootcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	struct dirtab *r = &roottab[c->qid.path], *newr;
	struct rootdata *rd = &rootdata[c->qid.path], *newrd;
	if (0)printk("rootcreate: c %p, name %s, omode %o, perm %x\n", 
	       c, name, omode, perm);
	/* find an empty slot */
	//wlock(&root)
	int path = c->qid.path;
	int newfile = inumber++; // kref
	newr = &roottab[newfile];
	strncpy(newr->name, name, sizeof(newr->name));
	mkqid(&newr->qid, newfile, newfile, perm&DMDIR);
	newr->length = 0;
	newr->perm = perm;
	newrd = &rootdata[newfile];
	newrd->dotdot = path;
	newrd->ptr = newr;
	newrd->size = 0;
	newrd->sizep = &newrd->size;
	rd->size++;
	if (newfile > rootmaxq)
		rootmaxq = newfile;
	if (1) printk("create: %s, newfile %d, dotdot %d, rootmaxq %d\n", name, newfile,
		  newrd->dotdot, rootmaxq);
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

void dumprootdev(void)
{
	struct dirtab *r = roottab;
	struct rootdata *rd = rootdata;
	int i;

	for(i = 0; i < rootmaxq; i++){
		if (! r->name[0])
			continue;
		printk("%s: [%d, %d, %d], %d, %o; ",
		       r->name, r->qid.path, r->qid.vers, r->qid.type,
		       r->length, r->perm);
		printk("dotdot %d, ptr %p, size %d, sizep %p\n", 
		       rd->dotdot, rd->ptr, rd->size, rd->sizep);
	}
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
	rootcreate,
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
