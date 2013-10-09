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


struct srv
{
	char	*name;
	char	*owner;
	uint32_t	perm;
	struct chan	*chan;
	struct srv	*link;
	uint32_t	path;
};

static qlock_t	srvlk;
static struct srv	*srv;
static int	qidpath;

static int
srvgen(struct chan *c, char *unused_char_p_t, struct dirtab*unused_dirtab, int unused_int, int s, struct dir *dp)
{
	struct srv *sp;
	struct qid q;

	if(s == DEVDOTDOT){
		devdir(c, c->qid, "#s", 0, eve, 0555, dp);
		return 1;
	}

	qlock(&srvlk);
	for(sp = srv; sp && s; sp = sp->link)
		s--;

	if(sp == 0) {
		qunlock(&srvlk);
		return -1;
	}

	mkqid(&q, sp->path, 0, QTFILE);
	/* make sure name string continues to exist after we release lock */
	strncpy(current->genbuf, sp->name,  sizeof current->genbuf);
	devdir(c, q, current->genbuf, 0, sp->owner, sp->perm, dp);
	qunlock(&srvlk);
	return 1;
}

static void
srvinit(void)
{
	qidpath = 1;
	qlock_init(&srvlk);
}

static struct chan*
srvattach(char *spec)
{
	return devattach('s', spec);
}

static struct walkqid*
srvwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, srvgen);
}

static struct srv*
srvlookup(char *name, uint32_t qidpath)
{
	struct srv *sp;
	for(sp = srv; sp; sp = sp->link)
		if(sp->path == qidpath || (name && strcmp(sp->name, name) == 0))
			return sp;
	return NULL;
}

static long
srvstat(struct chan *c, uint8_t *db, long n)
{
	return devstat(c, db, n, 0, 0, srvgen);
}

char*
srvname(struct chan *c)
{
	struct srv *sp;
	char *s;

	for(sp = srv; sp; sp = sp->link)
		if(sp->chan == c){
			int len = 3 + strlen(sp->name) + 1;
			s = kzmalloc(len, 0);
			snprintf(s, len, "#s/%s", sp->name);
			return s;
		}
	return NULL;
}

static struct chan*
srvopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	struct srv *sp;

	if(c->qid.type == QTDIR){
		if(omode & ORCLOSE)
			error(Eperm);
		if(omode != OREAD)
			error(Eisdir);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		nexterror();
	}

	sp = srvlookup(NULL, c->qid.path);
	if(sp == 0 || sp->chan == 0)
		error(Eshutdown);

	if(omode&OTRUNC)
		error("srv file already exists");
	if(openmode(omode)!=sp->chan->mode && sp->chan->mode!=ORDWR)
		error(Eperm);
	devpermcheck(sp->owner, sp->perm, omode);

	cclose(c);
	kref_get(&sp->chan->ref, 1);
	qunlock(&srvlk);
	poperror();
	return sp->chan;
}

static void
srvcreate(struct chan *c, char *name, int omode, int perm)
{
	ERRSTACK(2);
	struct srv *sp;
	char *sname;

	if(openmode(omode) != OWRITE)
		error(Eperm);

	if(omode & OCEXEC)	/* can't happen */
		panic("someone broke namec");

	sp = kzmalloc(sizeof *sp, 0);
	sname = kzmalloc(strlen(name) + 1, 0);

	qlock(&srvlk);
	if(waserror()){
		kfree(sname);
		kfree(sp);
		qunlock(&srvlk);
		nexterror();
	}
	if(sp == NULL || sname == NULL)
		error(Enomem);
	if(srvlookup(name, -1))
		error(Eexist);

	sp->path = qidpath++;
	sp->link = srv;
	strncpy(sname,  name, sizeof(sname));
	sp->name = sname;
	c->qid.type = QTFILE;
	c->qid.path = sp->path;
	srv = sp;
	qunlock(&srvlk);
	poperror();

	kstrdup(&sp->owner, "eve"/*up->user*/);
	sp->perm = perm&0777;

	c->flag |= COPEN;
	c->mode = OWRITE;
}

static void
srvremove(struct chan *c)
{
	ERRSTACK(2);
	struct srv *sp, **l;

	if(c->qid.type == QTDIR)
		error(Eperm);

	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		nexterror();
	}
	l = &srv;
	for(sp = *l; sp; sp = sp->link) {
		if(sp->path == c->qid.path)
			break;

		l = &sp->link;
	}
	if(sp == 0)
		error(Enonexist);

	/*
	 * Only eve can remove system services.
	 * No one can remove #s/boot.
	 */
	if(strcmp(sp->owner, eve) == 0 && !iseve())
		error(Eperm);
	if(strcmp(sp->name, "boot") == 0)
		error(Eperm);

	/*
	 * No removing personal services.
	 */
	if((sp->perm&7) != 7 && strcmp(sp->owner, "eve"/*up->user*/) && !iseve())
		error(Eperm);

	*l = sp->link;
	qunlock(&srvlk);
	poperror();

	if(sp->chan)
		cclose(sp->chan);
	kfree(sp->owner);
	kfree(sp->name);
	kfree(sp);
}

static long
srvwstat(struct chan *c, uint8_t *dp, long n)
{
	ERRSTACK(2);
	struct dir d;
	struct srv *sp;
	char *strs;

	if(c->qid.type & QTDIR)
		error(Eperm);

	strs = NULL;
	qlock(&srvlk);
	if(waserror()){
		qunlock(&srvlk);
		kfree(strs);
		nexterror();
	}

	sp = srvlookup(NULL, c->qid.path);
	if(sp == 0)
		error(Enonexist);

	if(strcmp(sp->owner, "eve"/*up->user*/) != 0 && !iseve())
		error(Eperm);

	strs = kzmalloc(n, 0);
	n = convM2D(dp, n, &d, strs);
	if(n == 0)
		error(Eshortstat);
	if(d.mode != ~0UL)
		sp->perm = d.mode & 0777;
	if(d.uid && *d.uid)
		kstrdup(&sp->owner, d.uid);
	if(d.name && *d.name && strcmp(sp->name, d.name) != 0) {
		if(strchr(d.name, '/') != NULL)
			error(Ebadchar);
		kstrdup(&sp->name, d.name);
	}

	qunlock(&srvlk);
	kfree(strs);
	poperror();
	return n;
}

static void
srvclose(struct chan *c)
{
	ERRSTACK(1);
	/*
	 * in theory we need to override any changes in removability
	 * since open, but since all that's checked is the owner,
	 * which is immutable, all is well.
	 */
	if(c->flag & CRCLOSE){
		if(waserror())
			return;
		srvremove(c);
		poperror();
	}
}

static long
srvread(struct chan *c, void *va, long n, int64_t unused)
{
	isdir(c);
	return devdirread(c, va, n, 0, 0, srvgen);
}

static long
srvwrite(struct chan *c, void *va, long n, int64_t unused)
{
	ERRSTACK(2);
	struct srv *sp;
	struct chan *c1;
	int fd;
	char buf[32];

	if(n >= sizeof buf)
		error(Egreg);
	memmove(buf, va, n);	/* so we can NUL-terminate */
	buf[n] = 0;
	fd = strtoul(buf, 0, 0);

	c1 = fdtochan(fd, -1, 0, 1);	/* error check and inc ref */

	qlock(&srvlk);
	if(waserror()) {
		qunlock(&srvlk);
		cclose(c1);
		nexterror();
	}
	if(c1->flag & (CCEXEC|CRCLOSE))
		error("posted fd has remove-on-close or close-on-exec");
	if(c1->qid.type & QTAUTH)
		error("cannot post auth file in srv");
	sp = srvlookup(NULL, c->qid.path);
	if(sp == 0)
		error(Enonexist);

	if(sp->chan)
		error(Ebadusefd);

	sp->chan = c1;
	qunlock(&srvlk);
	poperror();
	return n;
}

struct dev srvdevtab = {
	's',
	"srv",

	devreset,
	srvinit,
	devshutdown,
	srvattach,
	srvwalk,
	srvstat,
	srvopen,
	srvcreate,
	srvclose,
	srvread,
	devbread,
	srvwrite,
	devbwrite,
	srvremove,
	srvwstat,
};
