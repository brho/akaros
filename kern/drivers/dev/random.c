/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

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
#include <net/ip.h>
#include <random/fortuna.h>

static qlock_t rl;

/*
 * Add entropy. This is not currently used but we might want to hook it into a
 * hardware entropy source.
 */
void random_add(void *xp)
{
	ERRSTACK(1);

	qlock(&rl);
	if (waserror()) {
		qunlock(&rl);
		nexterror();
	}

	fortuna_add_entropy(xp, sizeof(xp));
	qunlock(&rl);

	poperror();
}

/*
 *  consume random bytes
 */
uint32_t random_read(void *xp, uint32_t n)
{
	ERRSTACK(1);

	qlock(&rl);

	if (waserror()) {
		qunlock(&rl);
		nexterror();
	}

	fortuna_get_bytes(n, xp);
	qunlock(&rl);

	poperror();

	return n;
}

/**
 * Fast random generator
 **/
uint32_t urandom_read(void *xp, uint32_t n)
{
	uint64_t seed[16];
	uint8_t *e, *p;
	uint32_t x = 0;
	uint64_t s0;
	uint64_t s1;

	if (n <= sizeof(seed))
		return random_read(xp, n);
	// The initial seed is from a good random pool.
	random_read(seed, sizeof(seed));
	p = xp;
	for (e = p + n; p < e;) {
		s0 = seed[x];
		s1 = seed[x = (x + 1) & 15];
		s1 ^= s1 << 31;
		s1 ^= s1 >> 11;
		s0 ^= s0 >> 30;
		*p++ = (seed[x] = s0 ^ s1) * 1181783497276652981LL;
	}

	return n;
}

struct dev randomdevtab;

static char *devname(void)
{
	return randomdevtab.name;
}

enum {
	Qdir,
	Qrandom,
	Qurandom
};

static struct dirtab randomdir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0500},
	{"random", {Qrandom}, 0, 0444},
	{"urandom", {Qurandom}, 0, 0444},
};

static void randominit(void)
{
	qlock_init(&rl);
}

/*
 *  create a random, no streams are created until an open
 */
static struct chan *randomattach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *randomwalk(struct chan *c, struct chan *nc, char **name,
                                  unsigned int nname)
{
	return devwalk(c, nc, name, nname, randomdir,
		       ARRAY_SIZE(randomdir), devgen);
}

static size_t randomstat(struct chan *c, uint8_t *dp, size_t n)
{
	struct dir dir;
	struct dirtab *tab;
	int perm;

	switch (c->qid.path) {
	case Qrandom:
		tab = &randomdir[Qrandom];
		perm = tab->perm | DMREADABLE;
		devdir(c, tab->qid, tab->name, 0, eve.name, perm, &dir);
		return dev_make_stat(c, &dir, dp, n);
	case Qurandom:
		tab = &randomdir[Qurandom];
		perm = tab->perm | DMREADABLE;
		devdir(c, tab->qid, tab->name, 0, eve.name, perm, &dir);
		return dev_make_stat(c, &dir, dp, n);
	default:
		return devstat(c, dp, n, randomdir, ARRAY_SIZE(randomdir),
			       devgen);
	}
}

/*
 *  if the stream doesn't exist, create it
 */
static struct chan *randomopen(struct chan *c, int omode)
{
	return devopen(c, omode, randomdir, ARRAY_SIZE(randomdir), devgen);
}

static void randomclose(struct chan *c)
{
}

static size_t randomread(struct chan *c, void *va, size_t n, off64_t ignored)
{
	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, va, n, randomdir,
				  ARRAY_SIZE(randomdir), devgen);
	case Qrandom:
		return random_read(va, n);
	case Qurandom:
		return urandom_read(va, n);
	default:
		panic("randomread: qid %d is impossible", c->qid.path);
	}
	return -1;	/* not reached */
}

/*
 *  A write to a closed random causes an ERANDOM error to be thrown.
 */
static size_t randomwrite(struct chan *c, void *va, size_t n, off64_t ignored)
{
	error(EPERM, "No use for writing random just yet");
	return -1;
}

static long randombwrite(struct chan *c, struct block *bp, uint32_t junk)
{
	error(EPERM, "No use for writing random just yet");
	return -1;
}

static int random_tapfd(struct chan *c, struct fd_tap *tap, int cmd)
{
	/* We don't actually support HANGUP, but epoll implies it. */
	#define RANDOM_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_HANGUP)

	if (tap->filter & ~RANDOM_TAPS) {
		set_error(ENOSYS, "Unsupported #%s tap %p, must be %p",
			  devname(), tap->filter, RANDOM_TAPS);
		return -1;
	}
	switch (c->qid.path) {
	case Qrandom:
	case Qurandom:
		/* Faking any legit command on (u)random, which never blocks. */
		return 0;
	default:
		set_error(ENOSYS, "Can't tap #%s file type %d", devname(),
		          c->qid.path);
		return -1;
	}
}

struct dev randomdevtab __devtab = {
	.name = "random",

	.reset = devreset,
	.init = randominit,
	.shutdown = devshutdown,
	.attach = randomattach,
	.walk = randomwalk,
	.stat = randomstat,
	.open = randomopen,
	.create = devcreate,
	.close = randomclose,
	.read = randomread,
	.write = randomwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
	.tapfd = random_tapfd,
};
