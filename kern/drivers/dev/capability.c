/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

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

#include <crypto/2crypto.h>
#include <crypto/2hmac.h>
#include <crypto/2id.h>
#include <crypto/2sha.h>

enum {
	Hashlen = VB2_SHA256_BLOCK_SIZE,
	Maxhash = 256,
};

/*
 *  if a process knows cap->cap, it can change user
 *  to capabilty->user.
 */
struct Caphash {
	struct Caphash *next;
	char hash[Hashlen];
};

struct {
	qlock_t qlock;
	struct Caphash *first;
	int nhash;
} capalloc;

enum {
	Qdir,
	Qhash,
	Quse,
};

/* caphash must be last */
struct dirtab capdir[] = {
	{".",       {Qdir, 0, QTDIR}, 0, DMDIR | 0500},
	{"capuse",  {Quse}, 0, 0222,},
	{"caphash", {Qhash},          0, 0200,},
};
int ncapdir = ARRAY_SIZE(capdir);

static struct chan *capattach(char *spec)
{
	return devattach("capability", spec);
}

static struct walkqid *capwalk(struct chan *c, struct chan *nc, char **name,
                               int nname)
{
	return devwalk(c, nc, name, nname, capdir, ncapdir, devgen);
}

static void capremove(struct chan *c)
{
	if (iseve() && c->qid.path == Qhash)
		ncapdir = ARRAY_SIZE(capdir) - 1;
	else
		error(EPERM, "Permission denied");
}

static int32_t capstat(struct chan *c, uint8_t *db, int32_t n)
{
	return devstat(c, db, n, capdir, ncapdir, devgen);
}

/*
 *  if the stream doesn't exist, create it
 */
static struct chan *capopen(struct chan *c, int omode)
{
	if (c->qid.type & QTDIR) {
		if (omode != O_RDONLY)
			error(EISDIR, "Is a directory");
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	switch ((uint32_t)c->qid.path) {
	case Qhash:
		if (!iseve())
			error(EPERM, "Permission denied: only eve() can open Qhash");
		break;
	}

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

/*
  static char*
  hashstr(uint8_t *hash)
  {
  static char buf[2*Hashlen+1];
  int i;

  for(i = 0; i < Hashlen; i++)
  sprint(buf+2*i, "%2.2x", hash[i]);
  buf[2*Hashlen] = 0;
  return buf;
  }
*/

static struct Caphash *remcap(uint8_t *hash)
{
	struct Caphash *t, **l;

	qlock(&capalloc.qlock);

	/* find the matching capability */
	for (l = &capalloc.first; *l != NULL;) {
		t = *l;
		if (memcmp(hash, t->hash, Hashlen) == 0)
			break;
		l = &t->next;
	}
	t = *l;
	if (t != NULL) {
		capalloc.nhash--;
		*l = t->next;
	}
	qunlock(&capalloc.qlock);

	return t;
}

/* add a capability, throwing out any old ones */
static void addcap(uint8_t *hash)
{
	struct Caphash *p, *t, **l;

	p = kzmalloc(sizeof(*p), 0);
	memmove(p->hash, hash, Hashlen);
	p->next = NULL;

	qlock(&capalloc.qlock);

	/* trim extras */
	while (capalloc.nhash >= Maxhash) {
		t = capalloc.first;
		if (t == NULL)
			panic("addcap");
		capalloc.first = t->next;
		kfree(t);
		capalloc.nhash--;
	}

	/* add new one */
	for (l = &capalloc.first; *l != NULL; l = &(*l)->next)
		;
	*l = p;
	capalloc.nhash++;

	qunlock(&capalloc.qlock);
}

static void capclose(struct chan *c)
{
}

static long capread(struct chan *c, void *va, long n, int64_t m)
{
	switch ((uint32_t)c->qid.path) {
	case Qdir:
		return devdirread(c, va, n, capdir, ncapdir, devgen);

	default:
		error(EPERM, "Permission denied: can't read capability files");
		break;
	}
	return n;
}

static long capwrite(struct chan *c, void *va, long n, int64_t m)
{
	struct Caphash *p;
	char *cp;
	uint8_t hash[Hashlen];
	char *key, *from, *to;
	char err[256];
	int ret;
	ERRSTACK(1);

	switch ((uint32_t)c->qid.path) {
	case Qhash:
		if (!iseve())
			error(EPERM, "permission denied: you must be eve");
		if (n < Hashlen)
			error(EIO, "Short read: on Qhash");
		memmove(hash, va, Hashlen);
		addcap(hash);
		break;

	case Quse:
		/* copy key to avoid a fault in hmac_xx */
		cp = NULL;
		if (waserror()) {
			kfree(cp);
			nexterror();
		}
		cp = kzmalloc(n + 1, 0);
		memmove(cp, va, n);
		cp[n] = 0;

		from = cp;
		key = strrchr(cp, '@');
		if (key == NULL)
			error(EIO, "short read: Quse");
		*key++ = 0;

		ret = hmac(VB2_ALG_RSA1024_SHA256, key, strlen(key),
		           from, strlen(from), hash, sizeof(hash));
		if (ret)
			error(EINVAL, "HMAC failed");

		p = remcap(hash);
		if (p == NULL) {
			snprintf(err, sizeof(err), "invalid capability %s@%s", from, key);
			error(EINVAL, err);
		}

		/* if a from user is supplied, make sure it matches */
		to = strchr(from, '@');
		if (to == NULL) {
			to = from;
		} else {
			*to++ = 0;
			panic("todo");
			/*
			if (strcmp(from, up->user) != 0)
				error(EINVAL, "capability must match user");
			*/
		}

		/* set user id */
		// TODO: make user a char *, not a fixed array.
		//kstrdup(&current->user, to);
		// In the original user names were NULL-terminated; ensure
		// that is still the case.
		if (strlen(to) > sizeof(current->user)-1)
			error(EINVAL, "New user name is > %d bytes", sizeof(current->user));
		memset(current->user, 0, sizeof(current->user));
		strncpy(current->user, to, sizeof(current->user));
		//up->basepri = PriNormal;


		kfree(p);
		kfree(cp);
		poperror();
		break;

	default:
		error(EPERM, "permission denied: capwrite");
		break;
	}

	return n;
}

struct dev capdevtab __devtab = {
	.name = "capability",

	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = capattach,
	.walk = capwalk,
	.stat = capstat,
	.open = capopen,
	.create = devcreate,
	.close = capclose,
	.read = capread,
	.bread = devbread,
	.write = capwrite,
	.bwrite = devbwrite,
	.remove = capremove,
	.wstat = devwstat,
};
