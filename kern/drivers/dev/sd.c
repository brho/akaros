/*
 * Storage Device.
 * From Inferno.
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
#include <sd.h>

extern struct dev sddevtab;
#warning "In Plan 9 sdifc was built by a scripts; need linker-set magic in akaros"
static struct sdifc *sdifc[] = {
	NULL,
};

typedef struct SDevgrp {
	struct sdev*	dev;
	int	nunits;		/* num units in dev */
} SDevgrp;

static SDevgrp* devs;			/* all devices */
static qlock_t devslock;			/* insertion and removal of devices */
static int ndevs;			/* total number of devices in the system */

enum {
	Rawcmd,
	Rawdata,
	Rawstatus,
};

enum {
	Qtopdir		= 1,		/* top level directory */
	Qtopbase,
	Qtopctl = Qtopbase,
	Qtopstat,

	Qunitdir,			/* directory per unit */
	Qunitbase,
	Qctl		= Qunitbase,
	Qraw,
	Qpart,

	TypeLOG		= 4,
	NType		= (1<<TypeLOG),
	TypeMASK	= (NType-1),
	TypeSHIFT	= 0,

	PartLOG		= 8,
	NPart		= (1<<PartLOG),
	PartMASK	= (NPart-1),
	PartSHIFT	= TypeLOG,

	UnitLOG		= 8,
	NUnit		= (1<<UnitLOG),
	UnitMASK	= (NUnit-1),
	UnitSHIFT	= (PartLOG+TypeLOG),

	DevLOG		= 8,
	NDev		= (1 << DevLOG),
	DevMASK	= (NDev-1),
	DevSHIFT = (UnitLOG+PartLOG+TypeLOG),

	Ncmd = 20,
};

#define TYPE(q)		((((uint32_t)(q).path)>>TypeSHIFT) & TypeMASK)
#define PART(q)		((((uint32_t)(q).path)>>PartSHIFT) & PartMASK)
#define UNIT(q)		((((uint32_t)(q).path)>>UnitSHIFT) & UnitMASK)
#define DEV(q)		((((uint32_t)(q).path)>>DevSHIFT) & DevMASK)
#define QID(d,u, p, t)	(((d)<<DevSHIFT)|((u)<<UnitSHIFT)|\
					 ((p)<<PartSHIFT)|((t)<<TypeSHIFT))


static void
sdaddpart(struct sdunit* unit, char* name, uint32_t start, uint32_t end)
{
	struct sdpart *pp;
	int i, partno;

	/*
	 * Check name not already used
	 * and look for a free slot.
	 */
	if(unit->part != NULL){
		partno = -1;
		for(i = 0; i < unit->npart; i++){
			pp = &unit->part[i];
			if(!pp->valid){
				if(partno == -1)
					partno = i;
				break;
			}
			if(strcmp(name, pp->sdperm.name) == 0){
				if(pp->start == start && pp->end == end)
					return;
				error(Ebadctl);
			}
		}
	}
	else{
		if((unit->part = kzmalloc(sizeof(struct sdpart) * SDnpart, 0)) == NULL)
			error(Enomem);
		unit->npart = SDnpart;
		partno = 0;
	}

	/*
	 * If no free slot found then increase the
	 * array size (can't get here with unit->part == NULL).
	 */
	if(partno == -1){
		if(unit->npart >= NPart)
			error(Enomem);
		if((pp = kzmalloc(sizeof(struct sdpart) * (unit->npart + SDnpart), 0)) == NULL)
			error(Enomem);
		memmove(pp, unit->part, sizeof(struct sdpart)*unit->npart);
		kfree(unit->part);
		unit->part = pp;
		partno = unit->npart;
		unit->npart += SDnpart;
	}

	/*
	 * Check size and extent are valid.
	 */
	if(start > end || end > unit->sectors)
		error(Eio);
	pp = &unit->part[partno];
	pp->start = start;
	pp->end = end;
	kstrdup(&pp->sdperm.name, name);
	kstrdup(&pp->sdperm.user, eve);
	pp->sdperm.perm = 0640;
	pp->valid = 1;
}

static void
sddelpart(struct sdunit* unit,  char* name)
{
	int i;
	struct sdpart *pp;

	/*
	 * Look for the partition to delete.
	 * Can't delete if someone still has it open.
	 */
	pp = unit->part;
	for(i = 0; i < unit->npart; i++){
		if(strcmp(name, pp->sdperm.name) == 0)
			break;
		pp++;
	}
	if(i >= unit->npart)
		error(Ebadctl);
#warning "permission checking disabled"
#if 0
	if(strcmp(up->env->sdperm.user, pp->sdperm.user) && !iseve())
		error(Eperm);
#endif
	pp->valid = 0;
	pp->vers++;
}

static int
sdinitpart(struct sdunit* unit)
{
	int i, nf;
	uint32_t start, end;
	char *f[4], *p, *q, buf[10];

	unit->vers++;
	unit->sectors = unit->secsize = 0;
	if(unit->part){
		for(i = 0; i < unit->npart; i++){
			unit->part[i].valid = 0;
			unit->part[i].vers++;
		}
	}

	if(unit->inquiry[0] & 0xC0)
		return 0;
	switch(unit->inquiry[0] & 0x1F){
	case 0x00:			/* DA */
	case 0x04:			/* WORM */
	case 0x05:			/* CD-ROM */
	case 0x07:			/* MO */
		break;
	default:
		return 0;
	}

	if(unit->dev->ifc->online)
		unit->dev->ifc->online(unit);
	if(unit->sectors){
		sdaddpart(unit, "data", 0, unit->sectors);
#warning "Currently we have no partitions passed from boot"
#if 0	
		/*
		 * Use partitions passed from boot program,
		 * e.g.
		 *	sdC0part=dos 63 123123/plan9 123123 456456
		 * This happens before /boot sets hostname so the
		 * partitions will have the null-string for user.
		 * The gen functions patch it up.
		 */
		snprintf(buf, sizeof buf, "%spart", unit->sdperm.name);
		for(p = getconf(buf); p != NULL; p = q){
			if(q = strchr(p, '/'))
				*q++ = '\0';
			nf = tokenize(p, f, ARRAY_SIZE(f));
			if(nf < 3)
				continue;
		
			start = strtoul(f[1], 0, 0);
			end = strtoul(f[2], 0, 0);
			if(!waserror()){
				sdaddpart(unit, f[0], start, end);
				poperror();
			}
		}	
#endif		
	}

	return 1;
}

static struct sdev*
sdgetdev(int idno)
{
	struct sdev *sdev;
	int i;

	qlock(&devslock);
	for(i = 0; i != ndevs; i++)
		if(devs[i].dev->idno == idno)
			break;
	
	if(i == ndevs)
		sdev = NULL;
	else{
		sdev = devs[i].dev;
		kref_get(&sdev->r, 1);
	}
	qunlock(&devslock);
	return sdev;
}

static struct sdunit*
sdgetunit(struct sdev* sdev, int subno)
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
	if(subno > sdev->nunit){
		qunlock(&sdev->unitlock);
		return NULL;
	}

	unit = sdev->unit[subno];
	if(unit == NULL){
		/*
		 * Probe the unit only once. This decision
		 * may be a little severe and reviewed later.
		 */
		if(sdev->unitflg[subno]){
			qunlock(&sdev->unitlock);
			return NULL;
		}
		if((unit = kzmalloc(sizeof(struct sdunit), 0)) == NULL){
			qunlock(&sdev->unitlock);
			return NULL;
		}
		sdev->unitflg[subno] = 1;

		snprintf(buf, sizeof(buf), "%s%d", sdev->name, subno);
		kstrdup(&unit->sdperm.name, buf);
		kstrdup(&unit->sdperm.user, eve);
		unit->sdperm.perm = 0555;
		unit->subno = subno;
		unit->dev = sdev;

		if(sdev->enabled == 0 && sdev->ifc->enable)
			sdev->ifc->enable(sdev);
		sdev->enabled = 1;

		/*
		 * No need to lock anything here as this is only
		 * called before the unit is made available in the
		 * sdunit[] array.
		 */
		if(unit->dev->ifc->verify(unit) == 0){
			qunlock(&sdev->unitlock);
			kfree(unit);
			return NULL;
		}
		sdev->unit[subno] = unit;
	}
	qunlock(&sdev->unitlock);
	return unit;
}

static void
sdreset(void)
{
	int i;
	struct sdev *sdev, *tail, *sdlist;

	/*
	 * Probe all configured controllers and make a list
	 * of devices found, accumulating a possible maximum number
	 * of units attached and marking each device with an index
	 * into the linear top-level directory array of units.
	 */
	tail = sdlist = NULL;
	for(i = 0; sdifc[i] != NULL; i++){
		if(sdifc[i]->pnp == NULL || (sdev = sdifc[i]->pnp()) == NULL)
			continue;
		if(sdlist != NULL)
			tail->next = sdev;
		else
			sdlist = sdev;
		for(tail = sdev; tail->next != NULL; tail = tail->next){
			tail->unit = (struct sdunit**)kzmalloc(tail->nunit * sizeof(struct sdunit *), 0);
			tail->unitflg = (int*)kzmalloc(tail->nunit * sizeof(int), 0);
			assert(tail->unit && tail->unitflg);
			ndevs++;
		}
		tail->unit = (struct sdunit**)kzmalloc(tail->nunit * sizeof(struct sdunit *), 0);
		tail->unitflg = (int*)kzmalloc(tail->nunit * sizeof(int), 0);
		ndevs++;
	}
	
	/*
	 * Legacy and option code goes here. This will be hard...
	 */

	/*
	 * The maximum number of possible units is known, allocate
	 * placeholders for their datastructures; the units will be
	 * probed and structures allocated when attached.
	 * Allocate controller names for the different types.
	 */
	if(ndevs == 0)
		return;
	for(i = 0; sdifc[i] != NULL; i++){
		/*
		 * BUG: no check is made here or later when a
		 * unit is attached that the id and name are set.
		 */
		if(sdifc[i]->id)
			sdifc[i]->id(sdlist);
	}

	/* 
	  * The IDs have been set, unlink the sdlist and copy the spec to
	  * the devtab.
	  */
	devs = (SDevgrp*)kzmalloc(ndevs * sizeof(SDevgrp), 0);
	memset(devs, 0, ndevs * sizeof(SDevgrp));
	i = 0;
	while(sdlist != NULL){
		devs[i].dev = sdlist;
		devs[i].nunits = sdlist->nunit;
		sdlist = sdlist->next;
		devs[i].dev->next = NULL;
		i++;
	}
}

static int
sd2gen(struct chan* c, int i, struct dir* dp)
{
	struct qid q;
	int64_t l;
	struct sdpart *pp;
	struct sdperm *perm;
	struct sdunit *unit;
	struct sdev *sdev;
	int rv;

	sdev = sdgetdev(DEV(c->qid));
	assert(sdev);
	unit = sdev->unit[UNIT(c->qid)];

	rv = -1;
	switch(i){
	case Qctl:
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qctl), 
			   unit->vers, QTFILE);
		perm = &unit->ctlperm;
		if(emptystr(perm->user)){
			kstrdup(&perm->user, eve);
			perm->perm = 0640;
		}
		devdir(c, q, "ctl", 0, perm->user, perm->perm, dp);
		rv = 1;
		break;

	case Qraw:
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qraw), 
			   unit->vers, QTFILE);
		perm = &unit->rawperm;
		if(emptystr(perm->user)){
			kstrdup(&perm->user, eve);
			perm->perm = DMEXCL|0600;
		}
		devdir(c, q, "raw", 0, perm->user, perm->perm, dp);
		rv = 1;
		break;

	case Qpart:
		pp = &unit->part[PART(c->qid)];
		l = (pp->end - pp->start) * (int64_t)unit->secsize;
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), PART(c->qid), Qpart), 
			   unit->vers+pp->vers, QTFILE);
		if(emptystr(pp->sdperm.user))
			kstrdup(&pp->sdperm.user, eve);
		devdir(c, q, pp->sdperm.name, l, pp->sdperm.user, pp->sdperm.perm, dp);
		rv = 1;
		break;
	}
	
	kref_put(&sdev->r);
	return rv;
}

static int
sd1gen(struct chan* c, int i, struct dir* dp)
{
	struct qid q;

	switch(i){
	case Qtopctl:
		mkqid(&q, QID(0, 0, 0, Qtopctl), 0, QTFILE);
		devdir(c, q, "sdctl", 0, eve, 0640, dp);
		return 1;
	case Qtopstat:
		mkqid(&q, QID(0, 0, 0, Qtopstat), 0, QTFILE);
		devdir(c, q, "sdstat", 0, eve, 0640, dp);
		return 1;
	}
	return -1;
}

static int
sdgen(struct chan* c, char *unused_char_p_t, struct dirtab*unused, int unused_int, int s, struct dir* dp)
{
	struct qid q;
	int64_t l;
	int i, r;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;

	switch(TYPE(c->qid)){
	case Qtopdir:
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, 0, 0, Qtopdir), 0, QTDIR);
			snprintf(get_cur_genbuf(), 2,
				 "#%C", sddevtab.dc);
			devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
			return 1;
		}

		if(s == 0 || s == 1)
			return sd1gen(c, s + Qtopbase, dp);
		s -= 2;

		qlock(&devslock);
		for(i = 0; i != ndevs; i++){
			if(s < devs[i].nunits)
				break;
			s -= devs[i].nunits;
		}
		
		if(i == ndevs){
			/* Run of the end of the list */
			qunlock(&devslock);
			return -1;
		}

		if((sdev = devs[i].dev) == NULL){
			qunlock(&devslock);
			return 0;
		}

		kref_get(&sdev->r, 1);
		qunlock(&devslock);

		if((unit = sdev->unit[s]) == NULL)
			if((unit = sdgetunit(sdev, s)) == NULL){
				kref_put(&sdev->r);
				return 0;
			}

		mkqid(&q, QID(sdev->idno, s, 0, Qunitdir), 0, QTDIR);
		if(emptystr(unit->sdperm.user))
			kstrdup(&unit->sdperm.user, eve);
		devdir(c, q, unit->sdperm.name, 0, unit->sdperm.user, unit->sdperm.perm, dp);
		kref_put(&sdev->r);
		return 1;

	case Qunitdir:
		if(s == DEVDOTDOT){
			mkqid(&q, QID(0, 0, 0, Qtopdir), 0, QTDIR);
			snprintf(get_cur_genbuf(), 2, "#%C", sddevtab.dc);
			devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
			return 1;
		}
		
		if((sdev = sdgetdev(DEV(c->qid))) == NULL){
			devdir(c, q, "unavailable", 0, eve, 0, dp);
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
		if(unit->sectors == 0
		|| (unit->dev->ifc->online && unit->dev->ifc->online(unit) > 1))
			sdinitpart(unit);

		i = s+Qunitbase;
		if(i < Qpart){
			r = sd2gen(c, i, dp);
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			return r;
		}
		i -= Qpart;
		if(unit->part == NULL || i >= unit->npart){
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			break;
		}
		pp = &unit->part[i];
		if(!pp->valid){
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			return 0;
		}
		l = (pp->end - pp->start) * (int64_t)unit->secsize;
		mkqid(&q, QID(DEV(c->qid), UNIT(c->qid), i, Qpart), 
			    unit->vers+pp->vers, QTFILE);
		if(emptystr(pp->sdperm.user))
			kstrdup(&pp->sdperm.user, eve);
		devdir(c, q, pp->sdperm.name, l, pp->sdperm.user, pp->sdperm.perm, dp);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		return 1;
	case Qraw:
	case Qctl:
	case Qpart:
		if((sdev = sdgetdev(DEV(c->qid))) == NULL){
			devdir(c, q, "unavailable", 0, eve, 0, dp);
			return 1;
		}
		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->ctl);
		r = sd2gen(c, TYPE(c->qid), dp);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		return r;
	case Qtopctl:
	case Qtopstat:
		return sd1gen(c, TYPE(c->qid), dp);
	default:
		break;
	}

	return -1;
}

static struct chan*
sdattach(char* spec)
{
	struct chan *c;
	char *p;
	struct sdev *sdev;
	int idno, subno, i;

	if(ndevs == 0 || *spec == '\0'){
		c = devattach(sddevtab.dc, spec);
		mkqid(&c->qid, QID(0, 0, 0, Qtopdir), 0, QTDIR);
		return c;
	}

	if(spec[0] != 's' || spec[1] != 'd')
		error(Ebadspec);
	idno = spec[2];
	subno = strtol(&spec[3], &p, 0);
	if(p == &spec[3])
		error(Ebadspec);

	qlock(&devslock);
	for (sdev = NULL, i = 0; i != ndevs; i++)
		if((sdev = devs[i].dev) != NULL && sdev->idno == idno)
			break;

	if(i == ndevs || subno >= sdev->nunit || sdgetunit(sdev, subno) == NULL){
		qunlock(&devslock);
		error(Enonexist);
	}
	kref_get(&sdev->r, 1);
	qunlock(&devslock);

	c = devattach(sddevtab.dc, spec);
	mkqid(&c->qid, QID(sdev->idno, subno, 0, Qunitdir), 0, QTDIR);
#warning "how do we handle c->dev? Inferno is different?"
	//c->dev = (sdev->idno << UnitLOG) + subno;
	kref_put(&sdev->r);
	return c;
}

static struct walkqid*
sdwalk(struct chan* c, struct chan* nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, NULL, 0, sdgen);
}

static long
sdstat(struct chan* c, uint8_t* db, long n)
{
	return devstat(c, db, n, NULL, 0, sdgen);
}

static struct chan*
sdopen(struct chan* c, int omode)
{
	ERRSTACK(1);
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	uint8_t tp;

	c = devopen(c, omode, 0, 0, sdgen);
	if((tp = TYPE(c->qid)) != Qctl && tp != Qraw && tp != Qpart)
		return c;

	sdev = sdgetdev(DEV(c->qid));
	if(sdev == NULL)
		error(Enonexist);
	unit = sdev->unit[UNIT(c->qid)];

	switch(TYPE(c->qid)){
	case Qctl:
		c->qid.vers = unit->vers;
		break;
	case Qraw:
		c->qid.vers = unit->vers;
#warning "replace tas with a kref?"
#if 0
		if(_tas(&unit->rawinuse) != 0){
			c->flag &= ~COPEN;
			error(Einuse);
		}
#endif
		unit->state = Rawcmd;
		break;
	case Qpart:
		qlock(&unit->ctl);
		if(waserror()){
			qunlock(&unit->ctl);
			c->flag &= ~COPEN;
			nexterror();
		}
		pp = &unit->part[PART(c->qid)];
		c->qid.vers = unit->vers+pp->vers;
		qunlock(&unit->ctl);
		poperror();
		break;
	}
	kref_put(&sdev->r);
	return c;
}

static void
sdclose(struct chan* c)
{
	struct sdunit *unit;
	struct sdev *sdev;

	if(c->qid.type & QTDIR)
		return;
	if(!(c->flag & COPEN))
		return;

	switch(TYPE(c->qid)){
	default:
		break;
	case Qraw:
		sdev = sdgetdev(DEV(c->qid));
		if(sdev){
			unit = sdev->unit[UNIT(c->qid)];
			unit->rawinuse = 0;
			kref_put(&sdev->r);
		}
		break;
	}
}

static long
sdbio(struct chan* c, int write, char* a, long len, int64_t off)
{
	ERRSTACK(1);
	int nchange;
	long l;
	uint8_t *b;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	uint32_t bno, max, nb, offset;

	sdev = sdgetdev(DEV(c->qid));
	if(sdev == NULL)
		error(Enonexist);
	unit = sdev->unit[UNIT(c->qid)];
	if(unit == NULL)
		error(Enonexist);

	nchange = 0;
	qlock(&unit->ctl);
	while(waserror()){
#warning "Skipping Eio handling (and note an errno issue")
#if 0
		/* notification of media change; go around again */
		if(strcmp(current->env->errstr, Eio) == 0 && unit->sectors == 0 && nchange++ == 0){
			sdinitpart(unit);
			continue;
		}
#endif
		/* other errors; give up */
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		nexterror();
	}
	pp = &unit->part[PART(c->qid)];
	if(unit->vers+pp->vers != c->qid.vers)
		error(Eio);

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
	bno = (off/unit->secsize) + pp->start;
	nb = ((off+len+unit->secsize-1)/unit->secsize) + pp->start - bno;
	max = SDmaxio/unit->secsize;
	if(nb > max)
		nb = max;
	if(bno+nb > pp->end)
		nb = pp->end - bno;
	if(bno >= pp->end || nb == 0){
		if(write)
			error(Eio);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		poperror();
		return 0;
	}
	if(!(unit->inquiry[1] & 0x80)){
		qunlock(&unit->ctl);
		poperror();
	}

	b = sdmalloc(nb*unit->secsize);
	if(b == NULL)
		error(Enomem);
	if(waserror()){
		sdfree(b);
		if(!(unit->inquiry[1] & 0x80))
			kref_put(&sdev->r);		/* gadverdamme! */
		nexterror();
	}

	offset = off%unit->secsize;
	if(offset+len > nb*unit->secsize)
		len = nb*unit->secsize - offset;
	if(write){
		if(offset || (len%unit->secsize)){
			l = unit->dev->ifc->bio(unit, 0, 0, b, nb, bno);
			if(l < 0)
				error(Eio);
			if(l < (nb*unit->secsize)){
				nb = l/unit->secsize;
				l = nb*unit->secsize - offset;
				if(len > l)
					len = l;
			}
		}
		memmove(b+offset, a, len);
		l = unit->dev->ifc->bio(unit, 0, 1, b, nb, bno);
		if(l < 0)
			error(Eio);
		if(l < offset)
			len = 0;
		else if(len > l - offset)
			len = l - offset;
	}
	else{
		l = unit->dev->ifc->bio(unit, 0, 0, b, nb, bno);
		if(l < 0)
			error(Eio);
		if(l < offset)
			len = 0;
		else if(len > l - offset)
			len = l - offset;
		memmove(a, b+offset, len);
	}
	sdfree(b);
	poperror();

	if(unit->inquiry[1] & 0x80){
		qunlock(&unit->ctl);
		poperror();
	}

	kref_put(&sdev->r);
	return len;
}

static long
sdrio(struct sdreq* r, void* a, long n)
{
	ERRSTACK(2);
	void *data;

	if(n >= SDmaxio || n < 0)
		error(Etoobig);

	data = NULL;
	if(n){
		if((data = sdmalloc(n)) == NULL)
			error(Enomem);
		if(r->write)
			memmove(data, a, n);
	}
	r->data = data;
	r->dlen = n;

	if(waserror()){
		if(data != NULL){
			sdfree(data);
			r->data = NULL;
		}
		nexterror();
	}

	if(r->unit->dev->ifc->rio(r) != SDok)
		error(Eio);

	if(!r->write && r->rlen > 0)
		memmove(a, data, r->rlen);
	if(data != NULL){
		sdfree(data);
		r->data = NULL;
	}
	poperror();

	return r->rlen;
}

static long
sdread(struct chan *c, void *a, long n, int64_t off)
{
	ERRSTACK(1);
	char *p, *e, *buf;
	struct sdpart *pp;
	struct sdunit *unit;
	struct sdev *sdev;
	uint32_t offset;
	int i, l, status;

	offset = off;
	switch(TYPE(c->qid)){
	default:
		error(Eperm);
	case Qtopstat:
		p = buf = kzmalloc(READSTR, 0);
		assert(p);
		e = p + READSTR;
		qlock(&devslock);
		for(i = 0; i != ndevs; i++){
			struct sdev *sdev = devs[i].dev;

			if(sdev->ifc->stat)
				p = sdev->ifc->stat(sdev, p, e);
			else
				p = seprintf(e, "%s; no statistics available\n", sdev->name);
		}
		qunlock(&devslock);
		n = readstr(off, a, n, buf);
		kfree(buf);
		return n;

	case Qtopdir:
	case Qunitdir:
		return devdirread(c, a, n, 0, 0, sdgen);

	case Qctl:
		sdev = sdgetdev(DEV(c->qid));
		if(sdev == NULL)
			error(Enonexist);

		unit = sdev->unit[UNIT(c->qid)];
		p = kzmalloc(READSTR, 0);
		l = snprintf(p, READSTR, "inquiry %.48s\n",
			( char *)unit->inquiry+8);
		qlock(&unit->ctl);
		/*
		 * If there's a device specific routine it must
		 * provide all information pertaining to night geometry
		 * and the garscadden trains.
		 */
		if(unit->dev->ifc->rctl)
			l += unit->dev->ifc->rctl(unit, p+l, READSTR-l);
		if(unit->sectors == 0)
			sdinitpart(unit);
		if(unit->sectors){
			if(unit->dev->ifc->rctl == NULL)
				l += snprintf(p+l, READSTR-l,
					"geometry %ld %ld\n",
					unit->sectors, unit->secsize);
			pp = unit->part;
			for(i = 0; i < unit->npart; i++){
				if(pp->valid)
					l += snprintf(p+l, READSTR-l,
						"part %s %lud %lud\n",
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
		if(sdev == NULL)
			error(Enonexist);

		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->raw);
		if(waserror()){
			qunlock(&unit->raw);
			kref_put(&sdev->r);
			nexterror();
		}
		if(unit->state == Rawdata){
			unit->state = Rawstatus;
			i = sdrio(unit->req, a, n);
		}
		else if(unit->state == Rawstatus){
			status = unit->req->status;
			unit->state = Rawcmd;
			kfree(unit->req);
			unit->req = NULL;
			i = readnum(0, a, n, status, NUMSIZE);
		} else
			i = 0;
		qunlock(&unit->raw);
		kref_put(&sdev->r);
		poperror();
		return i;

	case Qpart:
		return sdbio(c, 0, a, n, off);
	}

	return 0;
}

typedef struct Confdata Confdata;
struct Confdata {
	int	on;
	char*	spec;
	struct DevConf	cf;
};



static void
parseswitch(Confdata* cd, char* option)
{
	if(!strcmp("on", option))
		cd->on = 1;
	else if(!strcmp("off", option))
		cd->on = 0;
	else
		error(Ebadarg);
}

static void
parsespec(Confdata* cd, char* option)
{
	if(strlen(option) > 1) 
		error(Ebadarg);
	cd->spec = option;
}

static struct devport*
getnewport(struct DevConf* dc)
{
	struct devport *p;

	p = (struct devport *)kzmalloc((dc->nports + 1) *
				       sizeof(struct devport), 0);
	if(dc->nports > 0){
		memmove(p, dc->ports, dc->nports * sizeof(struct devport));
		kfree(dc->ports);
	}
	dc->ports = p;
	p = &dc->ports[dc->nports++];
	p->size = -1;
	p->port = (uint32_t)-1;
	return p;
}

static void
parseport(Confdata* cd, char* option)
{
	char *e;
	struct devport *p;

	if(cd->cf.nports == 0 || cd->cf.ports[cd->cf.nports-1].port != (uint32_t)-1)
		p = getnewport(&cd->cf);
	else
		p = &cd->cf.ports[cd->cf.nports-1];
	p->port = strtol(option, &e, 0);
	if(e == NULL || *e != '\0')
		error(Ebadarg);
}

static void
parsesize(Confdata* cd, char* option)
{
	char *e;
	struct devport *p;

	if(cd->cf.nports == 0 || cd->cf.ports[cd->cf.nports-1].size != -1)
		p = getnewport(&cd->cf);
	else
		p = &cd->cf.ports[cd->cf.nports-1];
	p->size = (int)strtol(option, &e, 0);
	if(e == NULL || *e != '\0')
		error(Ebadarg);
}

static void
parseirq(Confdata* cd, char* option)
{
	char *e;

	cd->cf.intnum = strtoul(option, &e, 0);
	if(e == NULL || *e != '\0')
		error(Ebadarg);
}

static void
parsetype(Confdata* cd, char* option)
{
	cd->cf.type = option;
}

static struct {
	char	*option;
	void	(*parse)(Confdata*, char *unused_char_p_t);
} options[] = {
	{ 	"switch",	parseswitch,	},
	{	"spec",		parsespec,	},
	{	"port",		parseport,	},
	{	"size",		parsesize,	},
	{	"irq",		parseirq,	},
	{	"type",		parsetype,	},
};

static long
sdwrite(struct chan* c, void* a, long n, int64_t off)
{
	ERRSTACK(1);
	struct cmdbuf *cb;
	struct sdreq *req;
	struct sdunit *unit;
	struct sdev *sdev;
	uint32_t end, start;

	switch(TYPE(c->qid)){
	default:
		error(Eperm);
	case Qtopctl: {
		Confdata cd;
		char buf[256], *field[Ncmd];
		int nf, i, j;

		memset(&cd, 0, sizeof(Confdata));
		if(n > sizeof(buf)-1) n = sizeof(buf)-1;
		memmove(buf, a, n);
		buf[n] = '\0';

		cd.on = -1;
		cd.spec = '\0';
		memset(&cd.cf, 0, sizeof(struct DevConf));

		nf = tokenize(buf, field, Ncmd);
		for(i = 0; i < nf; i++){
			char *opt = field[i++];
			if(i >= nf)
				error(Ebadarg);
			for(j = 0; j != ARRAY_SIZE(options); j++)
				if(!strcmp(opt, options[j].option))
					break;
					
			if(j == ARRAY_SIZE(options))
				error(Ebadarg);
			options[j].parse(&cd, field[i]);
		}

		if(cd.on < 0) 
			error(Ebadarg);

		if(cd.on){
			if(cd.spec == '\0' || cd.cf.nports == 0 || 
			     cd.cf.intnum == 0 || cd.cf.type == NULL)
				error(Ebadarg);
		}
		else{
			if(cd.spec == '\0')
				error(Ebadarg);
		}

		if(sddevtab.config == NULL)
			error("No configuration function");
		sddevtab.config(cd.on, cd.spec, &cd.cf);
		break;
	}
	case Qctl:
		cb = parsecmd(a, n);
		sdev = sdgetdev(DEV(c->qid));
		if(sdev == NULL)
			error(Enonexist);
		unit = sdev->unit[UNIT(c->qid)];

		qlock(&unit->ctl);
		if(waserror()){
			qunlock(&unit->ctl);
			kref_put(&sdev->r);
			kfree(cb);
			nexterror();
		}
		if(unit->vers != c->qid.vers)
			error(Eio);

		if(cb->nf < 1)
			error(Ebadctl);
		if(strcmp(cb->f[0], "part") == 0){
			if(cb->nf != 4)
				error(Ebadctl);
			if(unit->sectors == 0 && !sdinitpart(unit))
				error(Eio);
			start = strtoul(cb->f[2], 0, 0);
			end = strtoul(cb->f[3], 0, 0);
			sdaddpart(unit, cb->f[1], start, end);
		}
		else if(strcmp(cb->f[0], "delpart") == 0){
			if(cb->nf != 2 || unit->part == NULL)
				error(Ebadctl);
			sddelpart(unit, cb->f[1]);
		}
		else if(unit->dev->ifc->wctl)
			unit->dev->ifc->wctl(unit, cb);
		else
			error(Ebadctl);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		poperror();
		kfree(cb);
		break;

	case Qraw:
		sdev = sdgetdev(DEV(c->qid));
		if(sdev == NULL)
			error(Enonexist);
		unit = sdev->unit[UNIT(c->qid)];
		qlock(&unit->raw);
		if(waserror()){
			qunlock(&unit->raw);
			kref_put(&sdev->r);
			nexterror();
		}
		switch(unit->state){
		case Rawcmd:
			if(n < 6 || n > sizeof(req->cmd))
				error(Ebadarg);
			if((req = kzmalloc(sizeof(struct sdreq), 0)) == NULL)
				error(Enomem);
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
			error(Ebadusefd);

		case Rawdata:
			if(unit->state != Rawdata)
				error(Ebadusefd);
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

static long
sdwstat(struct chan* c, uint8_t* dp, long n)
{
	ERRSTACK(2);
	struct dir *d;
	struct sdpart *pp;
	struct sdperm *perm;
	struct sdunit *unit;
	struct sdev *sdev;

	if(c->qid.type & QTDIR)
		error(Eperm); 

	sdev = sdgetdev(DEV(c->qid));
	if(sdev == NULL)
		error(Enonexist);
	unit = sdev->unit[UNIT(c->qid)];
	qlock(&unit->ctl);
	d = NULL;
	if(waserror()){
		kfree(d);
		qunlock(&unit->ctl);
		kref_put(&sdev->r);
		nexterror();
	}

	switch(TYPE(c->qid)){
	default:
		error(Eperm);
	case Qctl:
		perm = &unit->ctlperm;
		break;
	case Qraw:
		perm = &unit->rawperm;
		break;
	case Qpart:
		pp = &unit->part[PART(c->qid)];
		if(unit->vers+pp->vers != c->qid.vers)
			error(Enonexist);
		perm = &pp->sdperm;
		break;
	}
#warning "permission checking disabled"
#if 0
	if(strcmp(current->env->sdperm.user, perm->sdperm.user) && !iseve())
		error(Eperm);
#endif

	d = kzmalloc(sizeof(struct dir) + n, 0);
	n = convM2D(dp, n, &d[0], ( char *)&d[1]);
	if(n == 0)
		error(Eshortstat);
	if(!emptystr(d[0].uid))
		kstrdup(&perm->user, d[0].uid);
	if(d[0].mode != ~0UL)
		perm->perm = (perm->perm & ~0777) | (d[0].mode & 0777);

	kfree(d);
	qunlock(&unit->ctl);
	kref_put(&sdev->r);
	poperror();
	return n;
}

static char
getspec(char base)
{
	while(1){
		int i;
		struct sdev *sdev;

		for(i = 0; i != ndevs; i++)
			if((sdev = devs[i].dev) != NULL && (char)sdev->idno == base)
				break;

		if(i == ndevs)
			return base;
		base++;
	}
	return '\0';
}

/* no ISA support ever, we hope */
static int
configure(char* spec, struct DevConf* cf)
{
	ERRSTACK(2);
	//ISAConf isa;
	SDevgrp *tmpdevs;
	struct sdev *tail, *sdev, *(*probe)(struct DevConf*);
	char *p, name[32];
	int i, nnew;

	if((p = strchr(cf->type, '/')) != NULL)
		*p++ = '\0';

	for(i = 0; sdifc[i] != NULL; i++)
		if(!strcmp(sdifc[i]->name, cf->type))
			break;

	if(sdifc[i] == NULL)
		error("type not found");
	
	if((probe = sdifc[i]->probe) == NULL)
		error("No probe function");

#if 0
	if(p){
		/* Try to find the card on the ISA bus.  This code really belongs
		     in sdata and I'll move it later.  Really! */
		memset(&isa, 0, sizeof(isa));
		isa.port = cf->ports[0].port;
		isa.irq = cf->intnum;

		if(pcmspecial(p, &isa) < 0)
			error("Cannot find controller");
	}
#endif
	qlock(&devslock);
	if(waserror()){
		qunlock(&devslock);
		nexterror();
	}
	
	for(i = 0; i != ndevs; i++)
		if((sdev = devs[i].dev) != NULL && sdev->idno == *spec)
			break;
	if(i != ndevs)
		error(Eexist);

	if((sdev = (*probe)(cf)) == NULL)
		error("Cannot probe controller");
	poperror();

	nnew = 0;
	tail = sdev;
	while(tail){
		nnew++;
		tail = tail->next;
	}
	
	tmpdevs = (SDevgrp*)kzmalloc((ndevs + nnew) * sizeof(SDevgrp), 0);
	memmove(tmpdevs, devs, ndevs * sizeof(SDevgrp));
	kfree(devs);
	devs = tmpdevs;

	while(sdev){
		/* Assign `spec' to the device */
		*spec = getspec(*spec);
		snprintf(name, sizeof(name), "sd%c", *spec);
		kstrdup(&sdev->name, name);
		sdev->idno = *spec;
		sdev->unit = (struct sdunit **)kzmalloc(sdev->nunit * sizeof(struct sdunit *), 0);
		sdev->unitflg = (int *)kzmalloc(sdev->nunit * sizeof(int), 0);
		assert(sdev->unit && sdev->unitflg);

		devs[ndevs].dev = sdev;
		devs[ndevs].nunits = sdev->nunit;
		sdev = sdev->next;
		devs[ndevs].dev->next = NULL;
		ndevs++;
	}

	qunlock(&devslock);
	return 0;
}

static int
unconfigure(char* spec)
{
	ERRSTACK(2);
	int i;	
	struct sdev *sdev;

	qlock(&devslock);
	if(waserror()){
		qunlock(&devslock);
		nexterror();
	}

	sdev = NULL;
	for(i = 0; i != ndevs; i++)
		if((sdev = devs[i].dev) != NULL && sdev->idno == *spec)
			break;

	if(i == ndevs)
		error(Enonexist);

	
	if(kref_refcnt(&sdev->r))
		error(Einuse);

	/* make sure no interrupts arrive anymore before removing resources */
	if(sdev->enabled && sdev->ifc->disable)
		sdev->ifc->disable(sdev);

	/* we're alone and the device tab is locked; make the device unavailable */
	memmove(&devs[i], &devs[ndevs - 1], sizeof(SDevgrp));
	memset(&devs[ndevs - 1], 0, sizeof(SDevgrp));
	ndevs--;

	qunlock(&devslock);
	poperror();

	for(i = 0; i != sdev->nunit; i++)
		if(sdev->unit[i]){
			struct sdunit *unit = sdev->unit[i];

			kfree(unit->sdperm.name);
			kfree(unit->sdperm.user);
			kfree(unit);
		}

	if(sdev->ifc->clear)
		sdev->ifc->clear(sdev);
	return 0;
}

static int
sdconfig(int on, char* spec, void *vconf)
{
	struct DevConf* cf = vconf;

	if(on)
		return configure(spec, cf);
	return unconfigure(spec);
}

struct dev sddevtab = {
	'S',
	"sd",

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
	.config = sdconfig,
};
