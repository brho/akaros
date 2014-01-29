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
/* Da Rules.
 * The roottab contains [name, qid, length, perm]. Length means length for files.
 * Qid is [path, vers, type]. Path is me. vers is next. Type is 'd' for dir
 * and 'f' for file and 0 for empty.
 * Data is [dotdot, ptr, size, *sizep, next]
 * dotdot is .., ptr is data (for files)
 * size is # elements (for dirs)
 * *sizep is a pointer for reasons not understood.
 * next is the sibling. For a dir, it's the first element after '.'.
 *	int	dotdot;
 *      int     child; 
 *	void	*ptr;
 *	int	size;
 *	int	*sizep;
 * 
 * entry is empty if type is 0. We look in roottab to determine that. 
*/
/* we pack the qid as follows: path is the index, vers is ., and type is type */
struct dirtab roottab[MAXFILE] = {
	{"", {0, 1, QTDIR}, 0, 0777},
	{"chan", {1, 2, QTDIR}, 0, 0777},
	{"dev", {2, 3, QTDIR}, 0, 0777},
	{"fd", {3, 4, QTDIR}, 0, 0777},
	{"prog", {4, 5, QTDIR}, 0, 0777},
	{"prof", {5, 6, QTDIR}, 0, 0777},
	{"net", {6, 7, QTDIR}, 0, 0777},
	{"net.alt", {7, 8, QTDIR}, 0, 0777},
	{"nvfs", {8, 9, QTDIR}, 0, 0777},
	{"env", {9, 10, QTDIR}, 0, 0777},
	{"root", {10, 11, QTDIR}, 0, 0777},
	{"srv", {11, 12, QTDIR}, 0, 0777},
	/* not courtesy of mkroot */
	{"mnt", {12, 0, QTDIR}, 0, 0777},
};

struct rootdata {
	int dotdot;
	int child;
	void *ptr;
	int size;
	int *sizep;
};

struct rootdata rootdata[MAXFILE] = {
	{0,	1,	 &roottab[1],	 12,	NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
	{0,	0,	 NULL,	 0,	 NULL},
};

void dumprootdev(void)
{
	struct dirtab *r = roottab;
	struct rootdata *rd = rootdata;
	int i;

	for (i = 0; i < rootmaxq; i++, r++, rd++) {
		if (i && (!r->name[0]))
			continue;
		printk("[%p]%s: [%d, %d, %d], %d, %o; ",
			   r,
			   r->name, r->qid.path, r->qid.vers, r->qid.type,
			   r->length, r->perm);
		printk("dotdot %d, child %d, ptr %p, size %d, sizep %p\n",
			   rd->dotdot, rd->child, rd->ptr, rd->size, rd->sizep);
	}
}

static int findempty(void)
{
	int i;
	for (i = 0; i < rootmaxq; i++) {
		if (!roottab[i].qid.type) {
			return i;
		}
	}
	memset(&roottab[i], 0, sizeof(roottab[i]));
	return -1;
}

static void freeempty(int i)
{
	roottab[i].qid.type = 0;
}

static int newentry(int old)
{
	int n = findempty();
	int sib;
	if (n < 0)
		error("#r. No more");
	printd("new entry is %d\n", n);
	sib = rootdata[old].child;
	if (sib) {
		roottab[n].qid.vers = roottab[sib].qid.vers;
		roottab[sib].qid.vers = n;
	}
	rootdata[old].child = n;
	return n;
}

static int createentry(int dir, char *name, int omode, int perm)
{
	int n = newentry(dir);
	strncpy(roottab[n].name, name, sizeof(roottab[n].name));
	roottab[n].length = 0;
	roottab[n].perm = perm;
	/* vers is already properly set. */
	mkqid(&roottab[n].qid, n, roottab[n].qid.vers, omode & DMDIR ? QTDIR : 'f');
	rootdata[n].dotdot = roottab[dir].qid.path;
	rootdata[dir].ptr = &roottab[n];
	rootdata[n].size = 0;
	rootdata[n].sizep = &rootdata[n].size;
	return n;
}

static struct chan *rootattach(char *spec)
{
	int i;
	uint32_t len;
	struct rootdata *r;

	if (*spec)
		error(Ebadspec);
	/* this begins with the root. */
	for (i = 0;; i++) {
		r = &rootdata[i];
		if (r->sizep) {
			len = *r->sizep;
			r->size = len;
			roottab[i].length = len;
		}
		i = roottab[i].qid.vers;
		if (!i)
			break;
	}
	return devattach('r', spec);
}

static int
rootgen(struct chan *c, char *name,
		struct dirtab *tab, int nd, int s, struct dir *dp)
{
	int p, i;
	struct rootdata *r;
	int iter;
	printd("rootgen, path is %d, tap %p, nd %d s %d name %s\n", c->qid.path,
	       tab, nd, s, name);

	if(s == DEVDOTDOT){
		p = rootdata[c->qid.path].dotdot;
		c->qid.path = p;
		c->qid.type = QTDIR;
		name = "#r";
		if (p != 0) {
			for (i = p;;) {
				if (roottab[i].qid.path == c->qid.path) {
					name = roottab[i].name;
					break;
				}
				i = roottab[i].qid.vers;
				if (!i)
					break;
			}
		}
		devdir(c, c->qid, name, 0, eve, 0777, dp);
		return 1;
	}

	if (name != NULL) {
		int path = c->qid.path;
		isdir(c);
		tab = &roottab[rootdata[path].child];
		/* we're starting at a directory. It might be '.' */
		for(iter = 0, i=path; ; iter++){
			if(strcmp(tab->name, name) == 0){
				printd("Rootgen returns 1 for %s\n", name);
				devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
				printd("return 1 with [%d, %d, %d]\n", dp->qid.path, c->qid.vers, c->qid.type);
				return 1;
			}
			if (iter > rootmaxq) {
				printk("BUG:");
				dumprootdev();
				printk("name %s\n", name);
				return -1;
			}
			i = roottab[i].qid.vers;
			if (!i)
				break;
			tab = &roottab[i];
		}
		printd("rootgen: :%s: failed at path %d\n", name, path);
		return -1;
	}

	if (s >= nd) {
		return -1;
	}
	tab += s;

	devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static struct walkqid *rootwalk(struct chan *c, struct chan *nc, char **name,
								int nname)
{
	uint32_t p;
	if (0){
		printk("rootwalk: c %p. :", c);
		if (nname){
			int i;
			for (i = 0; i < nname - 1; i++)
				printk("%s/", name[i]);
			printk("%s:\n", name[i]);
		}
	}
	p = c->qid.path;
	if((nname == 0) && 0)
		p = rootdata[p].dotdot;
	printd("Start from #%d at %p\n", p, &roottab[p]);
	return devwalk(c, nc, name, nname, &roottab[p], rootdata[p].size, rootgen);
}

static int rootstat(struct chan *c, uint8_t * dp, int n)
{
	int p;

	p = rootdata[c->qid.path].dotdot;
	return devstat(c, dp, n, rootdata[p].ptr, rootdata[p].size, rootgen);
}

static struct chan *rootopen(struct chan *c, int omode)
{
	int p;

	p = c->qid.path;
	return devopen(c, omode, rootdata[p].ptr, rootdata[p].size, rootgen);
}

static void rootcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	struct dirtab *r = &roottab[c->qid.path], *newr;
	struct rootdata *rd = &rootdata[c->qid.path];
	if (1)printk("rootcreate: c %p, name %s, omode %o, perm %x\n", 
	       c, name, omode, perm);
	/* find an empty slot */
	int path = c->qid.path;
	int newfile;
	newfile = createentry(path, name, omode, perm);
	rd->size++;
	if (newfile > rootmaxq)
		rootmaxq = newfile;
	if (0) printk("create: %s, newfile %d, dotdot %d, rootmaxq %d\n", name, newfile,
		  rootdata[newfile].dotdot, rootmaxq);
}

/*
 * sysremove() knows this is a nop
 */
static void rootclose(struct chan *c)
{
}

static long rootread(struct chan *c, void *buf, long n, int64_t offset)
{
	uint32_t p, len;
	uint8_t *data;

	p = c->qid.path;
	if (c->qid.type & QTDIR) {
		return devdirread(c, buf, n, rootdata[p].ptr, rootdata[p].size,
						  rootgen);
	}
	len = rootdata[p].size;
	if (offset < 0 || offset >= len) {
		return 0;
	}
	if (offset + n > len)
		n = len - offset;
	data = rootdata[p].ptr;
	memmove(buf, data + offset, n);
	return n;
}

static long rootwrite(struct chan *c, void *a, long n, int64_t off)
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
