/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * Storage Device.
 */

#include <vfs.h>

#include <assert.h>
#include <cpio.h>
#include <error.h>
#include <net/ip.h>
#include <kfs.h>
#include <kmalloc.h>
#include <kref.h>
#include <pmap.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>

#include <sd.h>

extern struct dev sddevtab;
struct sdifc sdiahciifc;

/* In Plan 9, this array is auto-generated. That's almost certainly not
 * necessary;
 * we can use linker sets at some point, as we do elsewhere in Akaros. */
struct sdifc *sdifc[] = {
    &sdiahciifc, NULL,
};

static const char Echange[] = "media or partition has changed";

static const char devletters[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

static struct sdev *devs[sizeof(devletters) - 1];

static qlock_t devslock = QLOCK_INITIALIZER(devslock);

enum {
	Rawcmd,
	Rawdata,
	Rawstatus,
};

enum {
	Qtopdir = 1, /* top level directory */
	Qtopbase,
	Qtopctl = Qtopbase,

	Qunitdir, /* directory per unit */
	Qunitbase,
	Qctl = Qunitbase,
	Qraw,
	Qpart,

	TypeLOG = 4,
	NType = (1 << TypeLOG),
	TypeMASK = (NType - 1),
	TypeSHIFT = 0,

	PartLOG = 8,
	NPart = (1 << PartLOG),
	PartMASK = (NPart - 1),
	PartSHIFT = TypeLOG,

	UnitLOG = 8,
	NUnit = (1 << UnitLOG),
	UnitMASK = (NUnit - 1),
	UnitSHIFT = (PartLOG + TypeLOG),

	DevLOG = 8,
	NDev = (1 << DevLOG),
	DevMASK = (NDev - 1),
	DevSHIFT = (UnitLOG + PartLOG + TypeLOG),

	Ncmd = 20,
};

#define TYPE(q) ((((uint32_t)(q).path) >> TypeSHIFT) & TypeMASK)
#define PART(q) ((((uint32_t)(q).path) >> PartSHIFT) & PartMASK)
#define UNIT(q) ((((uint32_t)(q).path) >> UnitSHIFT) & UnitMASK)
#define DEV(q) ((((uint32_t)(q).path) >> DevSHIFT) & DevMASK)
#define QID(d, u, p, t)                                                        \
	(((d) << DevSHIFT) | ((u) << UnitSHIFT) | ((p) << PartSHIFT) |             \
	 ((t) << TypeSHIFT))

void sdaddpart(struct sdunit *unit, char *name, uint64_t start, uint64_t end)
{
	struct sdpart *pp;
	int i, partno;

	/*
	 * Check name not already used
	 * and look for a free slot.
	 */
	if (unit->part != NULL) {
		partno = -1;
		for (i = 0; i < unit->npart; i++) {
			pp = &unit->part[i];
			if (!pp->valid) {
				if (partno == -1)
					partno = i;
				break;
			}
			if (strcmp(name, pp->sdperm.name) == 0) {
				if (pp->start == start && pp->end == end)
					return;
				error(EINVAL, "%s: '%s' is not valid", __func__, name);
			}
		}
	} else {
		unit->part = kzmalloc(sizeof(struct sdpart) * SDnpart, 0);
		if (unit->part == NULL)
			error(ENOMEM, "%s: can't allocate %d bytes", __func__,
			      sizeof(struct sdpart) * SDnpart);
		unit->npart = SDnpart;
		partno = 0;
	}

	/*
	 * If no free slot found then increase the
	 * array size (can't get here with unit->part == NULL).
	 */
	if (partno == -1) {
		if (unit->npart >= NPart)
			error(ENOMEM, "%s: no memory", __func__);
		pp = kzmalloc(sizeof(struct sdpart) * (unit->npart + SDnpart), 0);
		if (pp == NULL)
			error(ENOMEM, "%s: Can't allocate space for %d partitions",
			      unit->npart + SDnpart);
		memmove(pp, unit->part, sizeof(struct sdpart) * unit->npart);
		kfree(unit->part);
		unit->part = pp;
		partno = unit->npart;
		unit->npart += SDnpart;
	}

	/*
	 * Check size and extent are valid.
	 */
	if (start > end)
		error(EINVAL, "%s: start %d > end %d", __func__, start, end);
	if (end > unit->sectors)
		error(EINVAL, "%s: end %d > number of sectors %d", __func__, end,
		      unit->sectors);
	pp = &unit->part[partno];
	pp->start = start;
	pp->end = end;
	kstrdup(&pp->sdperm.name, name);
	kstrdup(&pp->sdperm.user, eve.name);
	pp->sdperm.perm = 0640;
	pp->valid = 1;
}

static void sddelpart(struct sdunit *unit, char *name)
{
	int i;
	struct sdpart *pp;
	/*
	 * Look for the partition to delete.
	 * Can't delete if someone still has it open.
	 */
	pp = unit->part;
	for (i = 0; i < unit->npart; i++) {
		if (strcmp(name, pp->sdperm.name) == 0)
			break;
		pp++;
	}
	if (i >= unit->npart)
		error(EINVAL, "%s: %d > npart %d", __func__, i, unit->npart);

	/* TODO: Implement permission checking and raise errors as appropriate. */
	// if (strcmp(current->user.name, pp->SDperm.user) && !iseve())
		// error(Eperm);

	pp->valid = 0;
	pp->vers++;
}

static void sdincvers(struct sdunit *unit)
{
	int i;

	unit->vers++;
	if (unit->part) {
		for (i = 0; i < unit->npart; i++) {
			unit->part[i].valid = 0;
			unit->part[i].vers++;
		}
	}
}

static int sdinitpart(struct sdunit *unit)
{
#if 0
	Mach *m;
	int nf;
	uint64_t start, end;
	char *f[4], *p, *q, buf[10];

	m = machp();
#endif
	if (unit->sectors > 0) {
		unit->sectors = unit->secsize = 0;
		sdincvers(unit);
	}

	/* device must be connected or not; other values are trouble */
	if (unit->inquiry[0] & 0xC0) /* see SDinq0periphqual */
		return 0;
	switch (unit->inquiry[0] & SDinq0periphtype) {
	case SDperdisk:
	case SDperworm:
	case SDpercd:
	case SDpermo:
		break;
	default:
		return 0;
	}

	if (unit->dev->ifc->online)
		unit->dev->ifc->online(unit);
	if (unit->sectors) {
		sdincvers(unit);
		sdaddpart(unit, "data", 0, unit->sectors);

/*
 * Use partitions passed from boot program,
 * e.g.
 *	sdC0part=dos 63 123123/plan9 123123 456456
 * This happens before /boot sets hostname so the
 * partitions will have the null-string for user.
 * The gen functions patch it up.
 */
#if 0
		snprintf(buf, sizeof(buf), "%spart", unit->sdperm.name);
		for (p = getconf(buf); p != NULL; p = q) {
			q = strchr(p, '/');
			if (q)
				*q++ = '\0';
			nf = tokenize(p, f, ARRAY_SIZE(f));
			if (nf < 3)
				continue;

			start = strtoull(f[1], 0, 0);
			end = strtoull(f[2], 0, 0);
			if (!waserror())
				sdaddpart(unit, f[0], start, end);
			poperror();
		}
#endif
	}

	return 1;
}

static int sdindex(int idno)
{
	char *p;

	p = strchr(devletters, idno);
	if (p == NULL)
		return -1;
	return p - devletters;
}

static struct sdev *sdgetdev(int idno)
{
	struct sdev *sdev;
	int i;

	if ((i = sdindex(idno)) < 0)
		return NULL;

	qlock(&devslock);
	sdev = devs[i];
	if (sdev)
		kref_get(&sdev->r, 1);
	qunlock(&devslock);
	return sdev;
}

static struct sdunit *sdgetunit(struct sdev *sdev, int subno)
{
	struct sdunit *unit;
	char buf[32];

	/*
	 * Associate a unit with a given device and sub-unit
	 * number on that device.
	 * The device will be probed if it has not already been
	 * successfully accessed.
	 */
	qlock(&sdev->unitlock);
	if (subno > sdev->nunit) {
		qunlock(&sdev->unitlock);
		return NULL;
	}

	unit = sdev->unit[subno];
	if (unit == NULL) {
		/*
		 * Probe the unit only once. This decision
		 * may be a little severe and reviewed later.
		 */
		if (sdev->unitflg[subno]) {
			qunlock(&sdev->unitlock);
			return NULL;
		}
		unit = kzmalloc(sizeof(struct sdunit), 0);
		if (unit == NULL) {
			qunlock(&sdev->unitlock);
			return NULL;
		}
		sdev->unitflg[subno] = 1;

		snprintf(buf, sizeof(buf), "%s%d", sdev->name, subno);
		kstrdup(&unit->sdperm.name, buf);
		kstrdup(&unit->sdperm.user, eve.name);
		unit->sdperm.perm = 0555;
		unit->subno = subno;
		unit->dev = sdev;
		qlock_init(&unit->ctl);

		if (sdev->enabled == 0 && sdev->ifc->enable)
			sdev->ifc->enable(sdev);
		sdev->enabled = 1;

		/*
		 * No need to lock anything here as this is only
		 * called before the unit is made available in the
		 * sdunit[] array.
		 */
		if (unit->dev->ifc->verify(unit) == 0) {
			qunlock(&sdev->unitlock);
			kfree(unit);
			return NULL;
		}
		sdev->unit[subno] = unit;
	}
	qunlock(&sdev->unitlock);
	return unit;
}

static void sdreset(void)
{
	int i;
	struct sdev *sdev;

	/*
	 * Probe all known controller types and register any devices found.
	 */
	for (i = 0; sdifc[i] != NULL; i++) {
		if (sdifc[i]->pnp == NULL)
			continue;
		sdev = sdifc[i]->pnp();
		if (sdev == NULL)
			continue;
		sdadddevs(sdev);
	}
}

void sdadddevs(struct sdev *sdev)
{
	int i, j, id;
	struct sdev *next;

	for (; sdev; sdev = next) {
		next = sdev->next;

		sdev->unit = (struct sdunit **)kzmalloc(
		    sdev->nunit * sizeof(struct sdunit *), 0);
		sdev->unitflg = (int *)kzmalloc(sdev->nunit * sizeof(int), 0);
		if (sdev->unit == NULL || sdev->unitflg == NULL) {
			printd("sdadddevs: out of memory\n");
		giveup:
			kfree(sdev->unit);
			kfree(sdev->unitflg);
			if (sdev->ifc->clear)
				sdev->ifc->clear(sdev);
			kfree(sdev);
			continue;
		}
		id = sdindex(sdev->idno);
		if (id == -1) {
			printd("sdadddevs: bad id number %d (%C)\n", id, id);
			goto giveup;
		}
		qlock(&devslock);
		for (i = 0; i < ARRAY_SIZE(devs); i++) {
			j = (id + i) % ARRAY_SIZE(devs);
			if (devs[j] == NULL) {
				sdev->idno = devletters[j];
				devs[j] = sdev;
				snprintf(sdev->name, sizeof(sdev->name), "sd%c", devletters[j]);
				break;
			}
		}
		qunlock(&devslock);
		if (i == ARRAY_SIZE(devs)) {
			printd("sdadddevs: out of device letters\n");
			goto giveup;
		}
	}
}

void sdaddallconfs(void (*addconf)(struct sdunit *))
{
	int i, u;
	struct sdev *sdev;

	for (i = 0; i < ARRAY_SIZE(devs); i++) /* each controller */
		for (sdev = devs[i]; sdev; sdev = sdev->next)
			for (u = 0; u < sdev->nunit; u++) /* each drive */
				(*addconf)(sdev->unit[u]);
}

static int sd2gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;
	uint64_t l;
	struct sdpart *pp;
	struct sdperm *perm;
	struct sdunit *unit;
	struct sdev *sdev;
	int rv;

	sdev = sdgetdev(DEV(c->qid));
	assert(sdev);
	unit = sdev->unit[UNIT(c->qid)];

	rv = -1;
	switch (i) {
	case Qctl:
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qctl),
		      unit->vers, QTFILE);
		perm = &unit->ctlperm;
		if (emptystr(perm->user)) {
			kstrdup(&perm->user, eve.name);
			perm->perm = 0644; /* nothing secret in ctl */
		}
		devdir(c, q, "ctl", 0, perm->user, perm->perm, dp);
		rv = 1;
		break;

	case Qraw:
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qraw),
		      unit->vers, QTFILE);
		perm = &unit->rawperm;
		if (emptystr(perm->user)) {
			kstrdup(&perm->user, eve.name);
			perm->perm = DMEXCL | 0600;
		}
		devdir(c, q, "raw", 0, perm->user, perm->perm, dp);
		rv = 1;
		break;

	case Qpart:
		pp = &unit->part[PART(c->qid)];
		l = (pp->end - pp->start) * unit->secsize;
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qpart),
		      unit->vers + pp->vers, QTFILE);
		if (emptystr(pp->sdperm.user))
			kstrdup(&pp->sdperm.user, eve.name);
		devdir(c, q, pp->sdperm.name, l, pp->sdperm.user, pp->sdperm.perm, dp);
		rv = 1;
		break;
	}

	kref_put(&sdev->r);
	return rv;
}

static int sd1gen(struct chan *c, int i, struct dir *dp)
{
	struct qid q;

	switch (i) {
	case Qtopctl:
		mkqid(&q, QID(0, 0, 0, Qtopctl), 0, QTFILE);
		devdir(c, q, "sdctl", 0, eve.name, 0644, dp); /* no secrets */
		return 1;
	}
	return -1;
}

static int sdgen(struct chan *c, char *d, struct dirtab *dir, int j, int s,
                 struct dir *dp)
{
	struct qid q = {};
	int64_t l;
	int i, r;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;

	switch (TYPE(c->qid)) {
	case Qtopdir:
		if (s == DEVDOTDOT) {
			mkqid(&q, QID(0, 0, 0, Qtopdir), 0, QTDIR);
			snprintf(get_cur_genbuf(), GENBUF_SZ, "#%s", sddevtab.name);
			devdir(c, q, get_cur_genbuf(), 0, eve.name, 0555, dp);
			return 1;
		}

		if (s + Qtopbase < Qunitdir)
			return sd1gen(c, s + Qtopbase, dp);
		s -= (Qunitdir - Qtopbase);

		qlock(&devslock);
		for (i = 0; i < ARRAY_SIZE(devs); i++) {
			if (devs[i]) {
				if (s < devs[i]->nunit)
					break;
				s -= devs[i]->nunit;
			}
		}

		if (i == ARRAY_SIZE(devs)) {
			/* Run off the end of the list */
			qunlock(&devslock);
			return -1;
		}

		sdev = devs[i];
		if (sdev == NULL) {
			qunlock(&devslock);
			return 0;
		}

		kref_get(&sdev->r, 1);
		qunlock(&devslock);

		unit = sdev->unit[s];
		if (unit == NULL)
			unit = sdgetunit(sdev, s);
		if (unit == NULL) {
			kref_put(&sdev->r);
			return 0;
		}

		mkqid(&q, QID(sdev->idno, s, 0, Qunitdir), 0, QTDIR);
		if (emptystr(unit->sdperm.user))
			kstrdup(&unit->sdperm.user, eve.name);
		devdir(c, q, unit->sdperm.name, 0, unit->sdperm.user, unit->sdperm.perm,
		       dp);
		kref_put(&sdev->r);
		return 1;

	case Qunitdir:
		if (s == DEVDOTDOT) {
			mkqid(&q, QID(0, 0, 0, Qtopdir), 0, QTDIR);
			snprintf(get_cur_genbuf(), GENBUF_SZ, "#%s", sddevtab.name);
			devdir(c, q, get_cur_genbuf(), 0, eve.name, 0555, dp);
			return 1;
		}

		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL) {
			devdir(c, c->qid, "unavailable", 0, eve.name, 0, dp);
			return 1;
		}

		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->ctl);

		/*
		 * Check for media change.
		 * If one has already been detected, sectors will be zero.
		 * If there is one waiting to be detected, online
		 * will return > 1.
		 * Online is a bit of a large hammer but does the job.
		 */
		if (unit->sectors == 0 ||
		    (unit->dev->ifc->online && unit->dev->ifc->online(unit) > 1))
			sdinitpart(unit);

		i = s + Qunitbase;
		if (i < Qpart) {
			r = sd2gen(c, i, dp);
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			return r;
		}
		i -= Qpart;
		if (unit->part == NULL || i >= unit->npart) {
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			break;
		}
		pp = &unit->part[i];
		if (!pp->valid) {
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			return 0;
		}
		l = (pp->end - pp->start) * (int64_t)unit->secsize;
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), i, Qpart),
		      unit->vers + pp->vers, QTFILE);
		if (emptystr(pp->sdperm.user))
			kstrdup(&pp->sdperm.user, eve.name);
		devdir(c, q, pp->sdperm.name, l, pp->sdperm.user, pp->sdperm.perm, dp);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		return 1;
	case Qraw:
	case Qctl:
	case Qpart:
		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL) {
			devdir(c, q, "unavailable", 0, eve.name, 0, dp);
			return 1;
		}
		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->ctl);
		r = sd2gen(c, TYPE(c->qid), dp);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		return r;
	case Qtopctl:
		return sd1gen(c, TYPE(c->qid), dp);
	default:
		break;
	}

	return -1;
}

static struct chan *sdattach(char *spec)
{
	struct chan *c;
	char *p;
	struct sdev *sdev;
	int idno, subno;

	if (*spec == '\0') {
		c = devattach(sddevtab.name, spec);
		mkqid(&c->qid, QID(0, 0, 0, Qtopdir), 0, QTDIR);
		return c;
	}

	if (spec[0] != 's' || spec[1] != 'd')
		error(EINVAL, "First two characters of spec must be 'sd', not %c%c",
		      spec[0], spec[1]);
	idno = spec[2];
	subno = strtol(&spec[3], &p, 0);
	if (p == &spec[3])
		error(EINVAL, "subno '%s' is not a number", &spec[3]);

	sdev = sdgetdev(idno);
	if (sdev == NULL)
		error(ENOENT, "No such unit %d", idno);
	if (sdgetunit(sdev, subno) == NULL) {
		kref_put(&sdev->r);
		error(ENOENT, "No such subno %d", subno);
	}

	c = devattach(sddevtab.name, spec);
	mkqid(&c->qid, QID(sdev->idno, subno, 0, Qunitdir), 0, QTDIR);
	c->dev = (sdev->idno << UnitLOG) + subno;
	kref_put(&sdev->r);
	return c;
}

static struct walkqid *sdwalk(struct chan *c, struct chan *nc, char **name,
                              unsigned int nname)
{
	return devwalk(c, nc, name, nname, NULL, 0, sdgen);
}

static size_t sdstat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, NULL, 0, sdgen);
}

static struct chan *sdopen(struct chan *c, int omode)
{
	ERRSTACK(1);
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	uint8_t tp;

	c = devopen(c, omode, 0, 0, sdgen);
	if ((tp = TYPE(c->qid)) != Qctl && tp != Qraw && tp != Qpart)
		return c;

	sdev = sdgetdev(DEV(c->qid));
	if (sdev == NULL)
		error(ENOENT, "No such device");

	unit = sdev->unit[UNIT(c->qid)];

	switch (TYPE(c->qid)) {
	case Qctl:
		c->qid.vers = unit->vers;
		break;
	case Qraw:
		c->qid.vers = unit->vers;
		if (test_and_set_bit(0, (unsigned long *)&unit->rawinuse) !=
		    0) {
			c->flag &= ~COPEN;
			kref_put(&sdev->r);
			error(EBUSY, "In use");
		}
		unit->state = Rawcmd;
		break;
	case Qpart:
		qlock(&unit->ctl);
		if (waserror()) {
			qunlock(&unit->ctl);
			c->flag &= ~COPEN;
			kref_put(&sdev->r);
			nexterror();
		}
		pp = &unit->part[PART(c->qid)];
		c->qid.vers = unit->vers + pp->vers;
		qunlock(&unit->ctl);
		poperror();
		break;
	}
	kref_put(&sdev->r);
	return c;
}

static void sdclose(struct chan *c)
{
	struct sdunit *unit;
	struct sdev *sdev;

	if (c->qid.type & QTDIR)
		return;
	if (!(c->flag & COPEN))
		return;

	switch (TYPE(c->qid)) {
	default:
		break;
	case Qraw:
		sdev = sdgetdev(DEV(c->qid));
		if (sdev) {
			unit = sdev->unit[UNIT(c->qid)];
			unit->rawinuse = 0;
			kref_put(&sdev->r);
		}
		break;
	}
}

static size_t sdbio(struct chan *c, int write, char *a, size_t len,
                    off64_t off)
{
	ERRSTACK(2);
	int nchange;
	uint8_t *b;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	int64_t bno;
	size_t l, max, nb, offset;

	sdev = sdgetdev(DEV(c->qid));
	if (sdev == NULL) {
		kref_put(&sdev->r);
		error(ENOENT, "No such file or directory");
	}
	unit = sdev->unit[UNIT(c->qid)];
	if (unit == NULL)
		error(ENOENT, "No such file or directory");

	nchange = 0;
	qlock(&unit->ctl);
	while (waserror()) {
		/* notification of media change; go around again */
		/* Meta-comment: I'm leaving commented-out code in place,
		 * which originally contained a strcmp of the error string to
		 * a value, to remind us: plan 9 is a distributed system. It's
		 * possible in principle to have the storage device on this
		 * machine use an sdi{ata,ahci} on another machine, and it all
		 * works. Nobody is going to do that, now, so get_errno() it is.
		 * if (strcmp(up->errstr, Eio) == 0 ... */
		if ((get_errno() == EIO) && (unit->sectors == 0) && (nchange++ == 0)) {
			sdinitpart(unit);
			poperror();
			continue;
		}

		/* other errors; give up */
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		nexterror();
	}
	pp = &unit->part[PART(c->qid)];
	if (unit->vers + pp->vers != c->qid.vers)
		error(EIO, "disk changed");

	/*
	 * Check the request is within bounds.
	 * Removeable drives are locked throughout the I/O
	 * in case the media changes unexpectedly.
	 * Non-removeable drives are not locked during the I/O
	 * to allow the hardware to optimise if it can; this is
	 * a little fast and loose.
	 * It's assumed that non-removeable media parameters
	 * (sectors, secsize) can't change once the drive has
	 * been brought online.
	 */
	bno = (off / unit->secsize) + pp->start;
	nb = ((off + len + unit->secsize - 1) / unit->secsize) + pp->start - bno;
	max = SDmaxio / unit->secsize;
	if (nb > max)
		nb = max;
	if (bno + nb > pp->end)
		nb = pp->end - bno;
	if (bno >= pp->end || nb == 0) {
		if (write)
			error(EIO, "bno(%d) >= pp->end(%d) or nb(%d) == 0", bno, pp->end,
			      nb);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		poperror();
		return 0;
	}
	if (!(unit->inquiry[1] & SDinq1removable)) {
		qunlock(&unit->ctl);
		poperror();
	}

	b = kzmalloc(nb * unit->secsize, MEM_WAIT);
	if (b == NULL)
		error(ENOMEM, "%s: could not allocate %d bytes", nb * unit->secsize);
	if (waserror()) {
		kfree(b);
		if (!(unit->inquiry[1] & SDinq1removable))
			kref_put(&sdev->r); /* gadverdamme! */
		nexterror();
	}

	offset = off % unit->secsize;
	if (offset + len > nb * unit->secsize)
		len = nb * unit->secsize - offset;
	if (write) {
		if (offset || (len % unit->secsize)) {
			l = unit->dev->ifc->bio(unit, 0, 0, b, nb, bno);
			if (l < 0)
				error(EIO, "IO Error");
			if (l < (nb * unit->secsize)) {
				nb = l / unit->secsize;
				l = nb * unit->secsize - offset;
				if (len > l)
					len = l;
			}
		}
		memmove(b + offset, a, len);
		l = unit->dev->ifc->bio(unit, 0, 1, b, nb, bno);
		if (l < 0)
			error(EIO, "IO Error");
		if (l < offset)
			len = 0;
		else if (len > l - offset)
			len = l - offset;
	} else {
		l = unit->dev->ifc->bio(unit, 0, 0, b, nb, bno);
		if (l < 0)
			error(EIO, "IO Error");
		if (l < offset)
			len = 0;
		else if (len > l - offset)
			len = l - offset;
		memmove(a, b + offset, len);
	}
	kfree(b);
	poperror();

	if (unit->inquiry[1] & SDinq1removable) {
		qunlock(&unit->ctl);
		poperror();
	}

	kref_put(&sdev->r);
	return len;
}

static size_t sdrio(struct sdreq *r, void *a, size_t n)
{
	ERRSTACK(1);
	void *data;

	if (n >= SDmaxio || n < 0)
		error(EINVAL, "%d is < 0 or > SDmaxio", n);

	data = NULL;
	if (n) {
		data = kzmalloc(n, MEM_WAIT);
		if (data == NULL)
			error(ENOMEM, "Alloc of %d bytes failed", n);
		if (r->write)
			memmove(data, a, n);
	}
	r->data = data;
	r->dlen = n;

	if (waserror()) {
		kfree(data);
		r->data = NULL;
		nexterror();
	}

	if (r->unit->dev->ifc->rio(r) != SDok)
		error(EIO, "IO Error");

	if (!r->write && r->rlen > 0)
		memmove(a, data, r->rlen);
	kfree(data);
	r->data = NULL;
	poperror();

	return r->rlen;
}

/*
 * SCSI simulation for non-SCSI devices
 */
int sdsetsense(struct sdreq *r, int status, int key, int asc, int ascq)
{
	int len;
	struct sdunit *unit;

	unit = r->unit;
	unit->sense[2] = key;
	unit->sense[12] = asc;
	unit->sense[13] = ascq;

	r->status = status;
	if (status == SDcheck && !(r->flags & SDnosense)) {
		/* request sense case from sdfakescsi */
		len = sizeof unit->sense;
		if (len > sizeof(r->sense) - 1)
			len = sizeof(r->sense) - 1;
		memmove(r->sense, unit->sense, len);
		unit->sense[2] = 0;
		unit->sense[12] = 0;
		unit->sense[13] = 0;
		r->flags |= SDvalidsense;
		return SDok;
	}
	return status;
}

int sdmodesense(struct sdreq *r, uint8_t *cmd, void *info, int ilen)
{
	int len;
	uint8_t *data;

	/*
	 * Fake a vendor-specific request with page code 0,
	 * return the drive info.
	 */
	if ((cmd[2] & 0x3F) != 0 && (cmd[2] & 0x3F) != 0x3F)
		return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
	len = (cmd[7] << 8) | cmd[8];
	if (len == 0)
		return SDok;
	if (len < 8 + ilen)
		return sdsetsense(r, SDcheck, 0x05, 0x1A, 0);
	if (r->data == NULL || r->dlen < len)
		return sdsetsense(r, SDcheck, 0x05, 0x20, 1);
	data = r->data;
	memset(data, 0, 8);
	data[0] = ilen >> 8;
	data[1] = ilen;
	if (ilen)
		memmove(data + 8, info, ilen);
	r->rlen = 8 + ilen;
	return sdsetsense(r, SDok, 0, 0, 0);
}

int sdfakescsi(struct sdreq *r, void *info, int ilen)
{
	uint8_t *cmd, *p;
	uint64_t len;
	struct sdunit *unit;

	cmd = r->cmd;
	r->rlen = 0;
	unit = r->unit;

	/*
	 * Rewrite read(6)/write(6) into read(10)/write(10).
	 */
	switch (cmd[0]) {
	case 0x08: /* read */
	case 0x0A: /* write */
		cmd[9] = 0;
		cmd[8] = cmd[4];
		cmd[7] = 0;
		cmd[6] = 0;
		cmd[5] = cmd[3];
		cmd[4] = cmd[2];
		cmd[3] = cmd[1] & 0x0F;
		cmd[2] = 0;
		cmd[1] &= 0xE0;
		cmd[0] |= 0x20;
		break;
	}

	/*
	 * Map SCSI commands into ATA commands for discs.
	 * Fail any command with a LUN except INQUIRY which
	 * will return 'logical unit not supported'.
	 */
	if ((cmd[1] >> 5) && cmd[0] != 0x12)
		return sdsetsense(r, SDcheck, 0x05, 0x25, 0);

	switch (cmd[0]) {
	default:
		return sdsetsense(r, SDcheck, 0x05, 0x20, 0);

	case 0x00: /* test unit ready */
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x03: /* request sense */
		if (cmd[4] < sizeof unit->sense)
			len = cmd[4];
		else
			len = sizeof unit->sense;
		if (r->data && r->dlen >= len) {
			memmove(r->data, unit->sense, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x12: /* inquiry */
		if (cmd[4] < sizeof unit->inquiry)
			len = cmd[4];
		else
			len = sizeof unit->inquiry;
		if (r->data && r->dlen >= len) {
			memmove(r->data, unit->inquiry, len);
			r->rlen = len;
		}
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x1B: /* start/stop unit */
		/*
		 * nop for now, can use power management later.
		 */
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x25: /* read capacity */
		if ((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if (r->data == NULL || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);

		/*
		 * Read capacity returns the LBA of the last sector.
		 */
		len = unit->sectors - 1;
		p = r->data;
		*p++ = len >> 24;
		*p++ = len >> 16;
		*p++ = len >> 8;
		*p++ = len;
		len = 512;
		*p++ = len >> 24;
		*p++ = len >> 16;
		*p++ = len >> 8;
		*p++ = len;
		r->rlen = p - (uint8_t *)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x9E: /* long read capacity */
		if ((cmd[1] & 0x01) || cmd[2] || cmd[3])
			return sdsetsense(r, SDcheck, 0x05, 0x24, 0);
		if (r->data == NULL || r->dlen < 8)
			return sdsetsense(r, SDcheck, 0x05, 0x20, 1);
		/*
		 * Read capcity returns the LBA of the last sector.
		 */
		len = unit->sectors - 1;
		p = r->data;
		*p++ = len >> 56;
		*p++ = len >> 48;
		*p++ = len >> 40;
		*p++ = len >> 32;
		*p++ = len >> 24;
		*p++ = len >> 16;
		*p++ = len >> 8;
		*p++ = len;
		len = 512;
		*p++ = len >> 24;
		*p++ = len >> 16;
		*p++ = len >> 8;
		*p++ = len;
		r->rlen = p - (uint8_t *)r->data;
		return sdsetsense(r, SDok, 0, 0, 0);

	case 0x5A: /* mode sense */
		return sdmodesense(r, cmd, info, ilen);

	case 0x28: /* read */
	case 0x2A: /* write */
	case 0x88: /* read16 */
	case 0x8a: /* write16 */
		return SDnostatus;
	}
}

static size_t sdread(struct chan *c, void *a, size_t n, off64_t off)
{
	ERRSTACK(1);
	char *p, *e, *buf;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	off64_t offset;
	int i, l, mm, status;

	offset = off;
	switch (TYPE(c->qid)) {
	default:
		error(EPERM, "Permission denied");
	case Qtopctl:
		mm = 64 * 1024; /* room for register dumps */
		p = buf = kzmalloc(mm, 0);
		if (p == NULL)
			error(ENOMEM, "Alloc of %d bytes failed", mm);
		e = p + mm;
		qlock(&devslock);
		for (i = 0; i < ARRAY_SIZE(devs); i++) {
			sdev = devs[i];
			if (sdev && sdev->ifc->rtopctl)
				p = sdev->ifc->rtopctl(sdev, p, e);
		}
		qunlock(&devslock);
		n = readstr(offset, a, n, buf);
		kfree(buf);
		return n;

	case Qtopdir:
	case Qunitdir:
		return devdirread(c, a, n, 0, 0, sdgen);

	case Qctl:
		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL)
			error(ENOENT, "No such device");

		unit = sdev->unit[UNIT(c->qid)];
		mm = 16 * 1024; /* room for register dumps */
		p = kzmalloc(mm, 0);
		if (p == NULL)
			error(ENOMEM, "Alloc of %d bytes failed", mm);
		l = snprintf(p, mm, "inquiry %.48s\n", (char *)unit->inquiry + 8);
		qlock(&unit->ctl);
		/*
		 * If there's a device specific routine it must
		 * provide all information pertaining to night geometry
		 * and the garscadden trains.
		 */
		if (unit->dev->ifc->rctl)
			l += unit->dev->ifc->rctl(unit, p + l, mm - l);
		if (unit->sectors == 0)
			sdinitpart(unit);
		if (unit->sectors) {
			if (unit->dev->ifc->rctl == NULL)
				l += snprintf(p + l, mm - l, "geometry %llu %lu\n",
				              unit->sectors, unit->secsize);
			pp = unit->part;
			for (i = 0; i < unit->npart; i++) {
				if (pp->valid)
					l += snprintf(p + l, mm - l, "part %s %llu %llu\n",
					              pp->sdperm.name, pp->start, pp->end);
				pp++;
			}
		}
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		l = readstr(offset, a, n, p);
		kfree(p);
		return l;

	case Qraw:
		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL)
			error(ENOENT, "No such file or directory");

		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->raw);
		if (waserror()) {
			qunlock(&unit->raw);
			kref_put(&sdev->r);
			nexterror();
		}
		if (unit->state == Rawdata) {
			unit->state = Rawstatus;
			n = sdrio(unit->req, a, n);
		} else if (unit->state == Rawstatus) {
			status = unit->req->status;
			unit->state = Rawcmd;
			kfree(unit->req);
			unit->req = NULL;
			n = readnum(0, a, n, status, NUMSIZE);
		} else
			n = 0;
		qunlock(&unit->raw);
		kref_put(&sdev->r);
		poperror();
		return n;

	case Qpart:
		return sdbio(c, 0, a, n, off);
	}
}

static void legacytopctl(struct cmdbuf *);

static size_t sdwrite(struct chan *c, void *a, size_t n, off64_t off)
{
	ERRSTACK(2);
	char *f0;
	int i;
	uint64_t end, start;
	struct cmdbuf *cb;
	struct sdifc *ifc;
	struct sdreq *req;
	struct sdunit *unit;
	struct sdev *sdev;

	switch (TYPE(c->qid)) {
	default:
		error(EPERM, "Permission denied");
	case Qtopctl:
		cb = parsecmd(a, n);
		if (waserror()) {
			kfree(cb);
			nexterror();
		}
		if (cb->nf == 0)
			error(EINVAL, "empty control message");
		f0 = cb->f[0];
		cb->f++;
		cb->nf--;
		if (strcmp(f0, "config") == 0) {
			/* wormhole into ugly legacy interface */
			legacytopctl(cb);
			poperror();
			kfree(cb);
			break;
		}
		/*
		 * "ata arg..." invokes sdifc[i]->wtopctl(NULL, cb),
		 * where sdifc[i]->sdperm.name=="ata" and cb contains the args.
		 */
		ifc = NULL;
		sdev = NULL;
		for (i = 0; sdifc[i]; i++) {
			if (strcmp(sdifc[i]->name, f0) == 0) {
				ifc = sdifc[i];
				sdev = NULL;
				goto subtopctl;
			}
		}
		/*
		 * "sd1 arg..." invokes sdifc[i]->wtopctl(sdev, cb),
		 * where sdifc[i] and sdev match controller letter "1",
		 * and cb contains the args.
		 */
		if (f0[0] == 's' && f0[1] == 'd' && f0[2] && f0[3] == 0) {
			sdev = sdgetdev(f0[2]);
			if (sdev != NULL) {
				ifc = sdev->ifc;
				goto subtopctl;
			}
		}
		error(EINVAL, "unknown interface");

	subtopctl:
		if (waserror()) {
			if (sdev)
				kref_put(&sdev->r);
			nexterror();
		}
		if (ifc->wtopctl)
			ifc->wtopctl(sdev, cb);
		else
			error(EINVAL, "Bad control");
		poperror();
		poperror();
		if (sdev)
			kref_put(&sdev->r);
		kfree(cb);
		break;

	case Qctl:
		cb = parsecmd(a, n);
		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL)
			error(ENOENT, "No such file or directory");
		unit = sdev->unit[UNIT(c->qid)];

		qlock(&unit->ctl);
		if (waserror()) {
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			kfree(cb);
			nexterror();
		}
		if (unit->vers != c->qid.vers)
			error(EIO, "Unit changed");

		if (cb->nf < 1)
			error(EINVAL, "%s requires at least one argument", cb->f[0]);
		if (strcmp(cb->f[0], "part") == 0) {
			if (cb->nf != 4)
				error(EINVAL, "Part got %d arguments, requires 4", cb->nf);
			if (unit->sectors == 0)
				error(EINVAL, "unit->sectors was 0");
			if (!sdinitpart(unit))
				error(EIO, "sdinitpart failed");
			start = strtoul(cb->f[2], 0, 0);
			end = strtoul(cb->f[3], 0, 0);
			sdaddpart(unit, cb->f[1], start, end);
		} else if (strcmp(cb->f[0], "delpart") == 0) {
			if (cb->nf != 2)
				error(EINVAL, "delpart got %d args, 2 required");
			if (unit->part == NULL)
				error(EIO, "partition was NULL");
			sddelpart(unit, cb->f[1]);
		} else if (unit->dev->ifc->wctl)
			unit->dev->ifc->wctl(unit, cb);
		else
			error(EINVAL, "Bad control %s", cb->f[0]);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		poperror();
		kfree(cb);
		break;

	case Qraw:
		sdev = sdgetdev(DEV(c->qid));
		if (sdev == NULL)
			error(ENOENT, "No such file or directory");
		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->raw);
		if (waserror()) {
			qunlock(&unit->raw);
			kref_put(&sdev->r);
			nexterror();
		}
		switch (unit->state) {
		case Rawcmd:
			if (n < 6 || n > sizeof(req->cmd))
				error(EINVAL, "%d is < 6 or > %d", n, sizeof(req->cmd));
			req = kzmalloc(sizeof(struct sdreq), 0);
			if (req == NULL)
				error(ENOMEM, "Can't allocate an sdreq");
			req->unit = unit;
			memmove(req->cmd, a, n);
			req->clen = n;
			req->flags = SDnosense;
			req->status = ~0;

			unit->req = req;
			unit->state = Rawdata;
			break;

		case Rawstatus:
			unit->state = Rawcmd;
			kfree(unit->req);
			unit->req = NULL;
			error(EINVAL, "Bad use of rawstatus");

		case Rawdata:
			unit->state = Rawstatus;
			unit->req->write = 1;
			n = sdrio(unit->req, a, n);
		}
		qunlock(&unit->raw);
		kref_put(&sdev->r);
		poperror();
		break;
	case Qpart:
		return sdbio(c, 1, a, n, off);
	}

	return n;
}

static size_t sdwstat(struct chan *c, uint8_t *dp, size_t n)
{
	ERRSTACK(2);
	struct dir *d;
	struct sdpart *pp;
	struct sdperm *perm;
	struct sdunit *unit;
	struct sdev *sdev;

	if (c->qid.type & QTDIR)
		error(EPERM, "Not a directory");

	sdev = sdgetdev(DEV(c->qid));
	if (sdev == NULL)
		error(ENOENT, "No such file or device");
	unit = sdev->unit[UNIT(c->qid)];
	qlock(&unit->ctl);
	d = NULL;
	if (waserror()) {
		kfree(d);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		nexterror();
	}

	switch (TYPE(c->qid)) {
	default:
		error(EPERM, "Permission denied");
	case Qctl:
		perm = &unit->ctlperm;
		break;
	case Qraw:
		perm = &unit->rawperm;
		break;
	case Qpart:
		pp = &unit->part[PART(c->qid)];
		if (unit->vers + pp->vers != c->qid.vers)
			error(ENOENT, "No such file or directory");
		perm = &pp->sdperm;
		break;
	}

	/* TODO: Implement permissions checking and raise errors as appropriate. */
	// if (strcmp(current->user.name, perm->user) && !iseve())
		// error(Eperm);

	d = kzmalloc(sizeof(struct dir) + n, 0);
	n = convM2D(dp, n, &d[0], (char *)&d[1]);
	if (n == 0)
		error(EIO, "Short status");
	if (!emptystr(d[0].uid))
		kstrdup(&perm->user, d[0].uid);
	if (d[0].mode != -1)
		perm->perm = (perm->perm & ~0777) | (d[0].mode & 0777);

	kfree(d);
	qunlock(&unit->ctl);
	kref_put(&sdev->r);
	poperror();
	return n;
}

static int configure(char *spec, struct devconf *cf)
{
	struct sdev *s, *sdev;
	char *p;
	int i;

	if (sdindex(*spec) < 0)
		error(EINVAL, "bad sd spec '%s'", spec);

	p = strchr(cf->type, '/');
	if (p != NULL)
		*p++ = '\0';

	for (i = 0; sdifc[i] != NULL; i++)
		if (strcmp(sdifc[i]->name, cf->type) == 0)
			break;
	if (sdifc[i] == NULL)
		error(ENOENT, "sd type not found");
	if (p)
		*(p - 1) = '/';

	if (sdifc[i]->probe == NULL)
		error(EIO, "sd type cannot probe");

	sdev = sdifc[i]->probe(cf);
	for (s = sdev; s; s = s->next)
		s->idno = *spec;
	sdadddevs(sdev);
	return 0;
}

static int unconfigure(char *spec)
{
	int i;
	struct sdev *sdev;
	struct sdunit *unit;

	if ((i = sdindex(*spec)) < 0)
		error(ENOENT, "No such file or directory '%s'", spec);

	qlock(&devslock);
	sdev = devs[i];
	if (sdev == NULL) {
		qunlock(&devslock);
		error(ENOENT, "No such file or directory at index %d", i);
	}
	if (kref_refcnt(&sdev->r)) {
		qunlock(&devslock);
		error(EBUSY, "%s is busy", spec);
	}
	devs[i] = NULL;
	qunlock(&devslock);

	/* make sure no interrupts arrive anymore before removing resources */
	if (sdev->enabled && sdev->ifc->disable)
		sdev->ifc->disable(sdev);

	for (i = 0; i != sdev->nunit; i++) {
		unit = sdev->unit[i];
		if (unit) {
			kfree(unit->sdperm.name);
			kfree(unit->sdperm.user);
			kfree(unit);
		}
	}

	if (sdev->ifc->clear)
		sdev->ifc->clear(sdev);
	kfree(sdev);
	return 0;
}

static int sdconfig(int on, char *spec, struct devconf *cf)
{
	if (on)
		return configure(spec, cf);
	return unconfigure(spec);
}

struct dev sddevtab __devtab = {
    .name = "sd",

    .reset = sdreset,
    .init = devinit,
    .shutdown = devshutdown,
    .attach = sdattach,
    .walk = sdwalk,
    .stat = sdstat,
    .open = sdopen,
    .create = devcreate,
    .close = sdclose,
    .read = sdread,
    .bread = devbread,
    .write = sdwrite,
    .bwrite = devbwrite,
    .remove = devremove,
    .wstat = sdwstat,
    .power = devpower,
};

/*
 * This is wrong for so many reasons.  This code must go.
 */
struct confdata {
	int on;
	char *spec;
	struct devconf cf;
};

static void parseswitch(struct confdata *cd, char *option)
{
	if (!strcmp("on", option))
		cd->on = 1;
	else if (!strcmp("off", option))
		cd->on = 0;
	else
		error(EINVAL, "Got %s, must be on or off", option);
}

static void parsespec(struct confdata *cd, char *option)
{
	if (strlen(option) > 1)
		error(EINVAL, "spec is %d bytes, must be 1", strlen(option));
	cd->spec = option;
}

static struct devport *getnewport(struct devconf *dc)
{
	struct devport *p;

	p = (struct devport *)kzmalloc((dc->nports + 1) * sizeof(struct devport),
	                               0);
	if (p == NULL)
		error(ENOMEM, "Can't allocate %d bytes for %d ports", dc->nports,
		      (dc->nports + 1) * sizeof(struct devport));
	if (dc->nports > 0) {
		memmove(p, dc->ports, dc->nports * sizeof(struct devport));
		kfree(dc->ports);
	}
	dc->ports = p;
	p = &dc->ports[dc->nports++];
	p->size = -1;
	p->port = (uint32_t)-1;
	return p;
}

static void parseport(struct confdata *cd, char *option)
{
	char *e;
	struct devport *p;

	if ((cd->cf.nports == 0) ||
	    (cd->cf.ports[cd->cf.nports - 1].port != (uint32_t)-1))
		p = getnewport(&cd->cf);
	else
		p = &cd->cf.ports[cd->cf.nports - 1];
	p->port = strtol(option, &e, 0);
	if (e == NULL || *e != '\0')
		error(EINVAL, "option %s is not a number", option);
}

static void parsesize(struct confdata *cd, char *option)
{
	char *e;
	struct devport *p;

	if (cd->cf.nports == 0 || cd->cf.ports[cd->cf.nports - 1].size != -1)
		p = getnewport(&cd->cf);
	else
		p = &cd->cf.ports[cd->cf.nports - 1];
	p->size = (int)strtol(option, &e, 0);
	if (e == NULL || *e != '\0')
		error(EINVAL, "%s is not a number", option);
}

static void parseirq(struct confdata *cd, char *option)
{
	char *e;

	cd->cf.intnum = strtoul(option, &e, 0);
	if (e == NULL || *e != '\0')
		error(EINVAL, "%s is not a number", option);
}

static void parsetype(struct confdata *cd, char *option)
{
	cd->cf.type = option;
}

static struct {
	char *name;
	void (*parse)(struct confdata *, char *unused_char_p_t);
} options[] = {
    {"switch", parseswitch},
    {"spec", parsespec},
    {"port", parseport},
    {"size", parsesize},
    {"irq", parseirq},
    {"type", parsetype},
};

static void legacytopctl(struct cmdbuf *cb)
{
	char *opt;
	int i, j;
	struct confdata cd;

	memset(&cd, 0, sizeof(cd));
	cd.on = -1;
	for (i = 0; i < cb->nf; i += 2) {
		if (i + 2 > cb->nf)
			error(EINVAL, "FIX ME. I don't know what this means");
		opt = cb->f[i];
		for (j = 0; j < ARRAY_SIZE(options); j++)
			if (strcmp(opt, options[j].name) == 0) {
				options[j].parse(&cd, cb->f[i + 1]);
				break;
			}
		if (j == ARRAY_SIZE(options))
			error(EINVAL, "FIX ME");
	}
	/* this has been rewritten to accommodate sdaoe */
	if (cd.on < 0 || cd.spec == 0)
		error(EINVAL, "cd.on(%d) < 0 or cd.spec == 0", cd.on);
	if (cd.on && cd.cf.type == NULL)
		error(EINVAL, "cd.on non-zero and cd.cf.type == NULL");
	sdconfig(cd.on, cd.spec, &cd.cf);
}
