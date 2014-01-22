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

enum {
	Qtopdir = 1,
};

struct SrvFile {
	char *name;
	char *user;
	uint32_t perm;
	struct qid qid;
	/* set to 1 on create; only decremented
	 * by remove.
	 */
	struct kref ref;
	struct chan *chan;
	/* set to 1 on create;
	 * hence will never go to 0
	 * but can be handily used
	 * for exclusive open.*/
	struct kref opens;
	struct SrvFile *entry;
	int length;
};

static int
srvgen(struct chan *c, char *name,
	   struct dirtab *tab, int ntab, int s, struct dir *dp)
{
	struct SrvFile *f;

	if (s == DEVDOTDOT) {
		devdir(c, c->qid, "#s", 0, eve, 0555, dp);
		return 1;
	}
	f = c->aux;
	if ((c->qid.type & QTDIR) == 0) {
		if (s > 0)
			return -1;
		devdir(c, f->qid, f->name, f->length, f->user, f->perm, dp);
		return 1;
	}

	for (f = f->entry; f != NULL; f = f->entry) {
		if (s-- == 0)
			break;
	}
	if (f == NULL)
		return -1;

	devdir(c, f->qid, f->name, f->length, f->user, f->perm, dp);
	return 1;
}

static void srvinit(void)
{
}

static int srvcanattach(struct SrvFile * d)
{
	if (strcmp(d->user, current->user) == 0)
		return 1;

	/*
	 * Need write permission in other to allow attaches if
	 * we are not the owner
	 */
	if (d->perm & 2)
		return 1;

	return 0;
}

static struct chan *srvattach(char *spec)
{
	/* the inferno attach was pretty complicated, but
	 * we're not sure that complexity is needed.
	 * Assume not. */
	struct chan *c = devattach('s', spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	return c;
}

static struct walkqid *srvwalk(struct chan *c, struct chan *nc, char **name,
							   int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, srvgen);
}

static int srvstat(struct chan *c, uint8_t * db, int n)
{
	n = devstat(c, db, n, 0, 0, srvgen);
	return n;
}

static void srvputdir(struct SrvFile * dir)
{
	kfree(dir->user);
	kfree(dir->name);
	kfree(dir);
}

static void srv_release(struct kref *kref)
{
	struct SrvFile *f = container_of(kref, struct SrvFile, ref);
	srvputdir(f);
}

static struct chan *srvopen(struct chan *c, int omode)
{
	ERRSTACK(2);
	struct SrvFile *sf;
/* NEEDS TO RETURN SP->CHAN */
	openmode(omode);	/* check it */
	if (c->qid.type & QTDIR) {
		if (omode != OREAD)
			error(Eisdir);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	sf = c->aux;

	devpermcheck(sf->user, sf->perm, omode);
	/* do we have ORCLOSE yet?
	if (omode & ORCLOSE && strcmp(sf->user, up->env->user) != 0)
		error(Eperm);
	if (sf->perm & DMEXCL && sf->opens != 0)
		error(Einuse);
	*/
	kref_init(&sf->opens, fake_release, 1);
	kref_init(&sf->ref, srv_release, 1);
	/*	if (omode & ORCLOSE)
	 *		sf->flags |= SORCLOSE;
	 */
	c->offset = 0;
	c->flag |= COPEN;
	c->mode = openmode(omode);

	return c;
}

static int srvwstat(struct chan *c, uint8_t * dp, int n)
{
	/* some other time. */
	error(Eperm);
	return -1;
}

static void srvclose(struct chan *c)
{
	struct SrvFile *s = c->aux;
	kref_put(&s->opens);
}

static void srvremove(struct chan *c)
{
	struct SrvFile *s = c->aux;
	kref_put(&s->ref);
}

/* N.B. srvopen gives the chan back. The only 'reading' we do
 * in srv is of the top level directory.
 */
static long srvread(struct chan *c, void *va, long count, int64_t offset)
{
	return devdirread(c, va, count, 0, 0, srvgen);
}

static long srvwrite(struct chan *c, void *va, long count, int64_t offset)
{
	/* basic operation. 
	 * Verify the srv entry is there.
	 * Lock it.
	 * get the string from the va.
	 * It is an integer fd, so get the
	 * chan for the fd. 
	 * Set the srv chan to that chan
	 * (if it's not set. If it is, that's bad.)
	 * That that fdtochan increments the ref, so
	 * no need to do that.
	 */
#if 0
	if (c->qid.type & QTDIR)
		error(Eperm);

	sp = c->aux;
#endif
	error("srvwrite: notyet");
	return -1;
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
	devcreate,
	srvclose,
	srvread,
	devbread,
	srvwrite,
	devbwrite,
	srvremove,
	srvwstat
};
