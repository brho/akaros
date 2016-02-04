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
static uint32_t _randomread(void *xp, uint32_t n)
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
uint32_t urandomread(void *xp, uint32_t n)
{
	ERRSTACK(1);
	uint64_t seed[16];
	uint8_t *e, *p;
	uint32_t x = 0;
	uint64_t s0;
	uint64_t s1;

	// The initial seed is from a good random pool.
	_randomread(seed, sizeof(seed));
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

static
struct dirtab randomdir[] = {
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
								int nname)
{
	return devwalk(c, nc, name, nname, randomdir,
		       ARRAY_SIZE(randomdir), devgen);
}

static int randomstat(struct chan *c, uint8_t *dp, int n)
{
	return devstat(c, dp, n, randomdir, ARRAY_SIZE(randomdir), devgen);
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

static long randomread(struct chan *c, void *va, long n, int64_t ignored)
{
	switch (c->qid.path) {
		case Qdir:
			return devdirread(c, va, n, randomdir,
					  ARRAY_SIZE(randomdir), devgen);
		case Qrandom:
			return _randomread(va, n);
		case Qurandom:
			return urandomread(va, n);
		default:
			panic("randomread: qid %d is impossible", c->qid.path);
	}
	return -1;	/* not reached */
}

/*
 *  A write to a closed random causes an ERANDOM error to be thrown.
 */
static long randomwrite(struct chan *c, void *va, long n, int64_t ignored)
{
	error(EPERM, "No use for writing random just yet");
	return -1;
}

static long randombwrite(struct chan *c, struct block *bp, uint32_t junk)
{
	error(EPERM, "No use for writing random just yet");
	return -1;
}

static int randomwstat(struct chan *c, uint8_t *dp, int n)
{
	error(EPERM, "No use for wstat random just yet");
	return -1;
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
	.wstat = randomwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};

/* This is something used by the TCP stack.
 * I have no idea of why these numbers were chosen. */
static uint32_t randn;

static void seedrand(void)
{
	ERRSTACK(2);
	if (!waserror()) {
		_randomread((void *)&randn, sizeof(randn));
		poperror();
	}
}

int nrand(int n)
{
	if (randn == 0)
		seedrand();
	randn = randn * 1103515245 + 12345 + read_tsc();
	return (randn >> 16) % n;
}

