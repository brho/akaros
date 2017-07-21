/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

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
#include <net/ip.h>

#define DPRINT if(0)print

enum {
	Maxmedia = 32,
	Nself = Maxmedia * 5,
	NHASH = (1 << 6),
	NCACHE = 256,
	QMAX = 64 * 1024 - 1,
};

struct medium *media[Maxmedia] = {
	0
};

/*
 *  cache of local addresses (addresses we answer to)
 */
struct Ipself {
	uint8_t a[IPaddrlen];
	struct Ipself *hnext;		/* next address in the hash table */
	struct Iplink *link;		/* binding twixt Ipself and Ipifc */
	uint32_t expire;
	uint8_t type;				/* type of address */
	int ref;
	struct Ipself *next;		/* free list */
};

struct Ipselftab {
	qlock_t qlock;
	int inited;
	int acceptall;				/* true if an interface has the null address */
	struct Ipself *hash[NHASH];	/* hash chains */
};

/*
 *  Multicast addresses are chained onto a Chan so that
 *  we can remove them when the Chan is closed.
 */
typedef struct Ipmcast Ipmcast;
struct Ipmcast {
	Ipmcast *next;
	uint8_t ma[IPaddrlen];		/* multicast address */
	uint8_t ia[IPaddrlen];		/* interface address */
};

/* quick hash for ip addresses */
#define hashipa(a) ( ( ((a)[IPaddrlen-2]<<8) | (a)[IPaddrlen-1] )%NHASH )

static char tifc[] = "ifc ";

static void addselfcache(struct Fs *f, struct Ipifc *ifc, struct Iplifc *lifc,
						 uint8_t * a, int type);
static void remselfcache(struct Fs *f,
						 struct Ipifc *ifc, struct Iplifc *lifc, uint8_t * a);
static void ipifcjoinmulti(struct Ipifc *ifc, char **argv, int argc);
static void ipifcleavemulti(struct Ipifc *ifc, char **argv, int argc);
static void ipifcregisterproxy(struct Fs *, struct Ipifc *,
							   uint8_t * unused_uint8_p_t);
static void ipifcremlifc(struct Ipifc *, struct Iplifc *);
static void ipifcaddpref6(struct Ipifc *ifc, char **argv, int argc);

/*
 *  link in a new medium
 */
void addipmedium(struct medium *med)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(media) - 1; i++)
		if (media[i] == NULL) {
			media[i] = med;
			break;
		}
}

/*
 *  find the medium with this name
 */
struct medium *ipfindmedium(char *name)
{
	struct medium **mp;

	for (mp = media; *mp != NULL; mp++)
		if (strcmp((*mp)->name, name) == 0)
			break;
	return *mp;
}

/*
 *  attach a device (or pkt driver) to the interface.
 *  called with c locked
 */
static void ipifcbind(struct conv *c, char **argv, int argc)
{
	ERRSTACK(1);
	struct Ipifc *ifc;
	struct medium *m;

	if (argc < 2)
		error(EINVAL, "Too few args (%d) to %s", argc, __func__);

	ifc = (struct Ipifc *)c->ptcl;

	/* bind the device to the interface */
	m = ipfindmedium(argv[1]);
	if (m == NULL)
		error(EFAIL, "unknown interface type");

	wlock(&ifc->rwlock);
	if (ifc->m != NULL) {
		wunlock(&ifc->rwlock);
		error(EFAIL, "interfacr already bound");
	}
	if (waserror()) {
		wunlock(&ifc->rwlock);
		nexterror();
	}

	/* do medium specific binding */
	(*m->bind) (ifc, argc, argv);

	/* set the bound device name */
	if (argc > 2)
		strlcpy(ifc->dev, argv[2], sizeof(ifc->dev));
	else
		snprintf(ifc->dev, sizeof(ifc->dev), "%s%d", m->name, c->x);

	/* set up parameters */
	ifc->m = m;
	ifc->mintu = ifc->m->mintu;
	ifc->maxtu = ifc->m->maxtu;
	if (ifc->m->unbindonclose == 0)
		ifc->conv->inuse++;
	ifc->rp.mflag = 0;	// default not managed
	ifc->rp.oflag = 0;
	ifc->rp.maxraint = 600000;	// millisecs
	ifc->rp.minraint = 200000;
	ifc->rp.linkmtu = 0;	// no mtu sent
	ifc->rp.reachtime = 0;
	ifc->rp.rxmitra = 0;
	ifc->rp.ttl = MAXTTL;
	ifc->rp.routerlt = 3 * (ifc->rp.maxraint);

	/* any ancillary structures (like routes) no longer pertain */
	ifc->ifcid++;

	/* reopen all the queues closed by a previous unbind */
	qreopen(c->rq);
	qreopen(c->eq);
	qreopen(c->sq);

	wunlock(&ifc->rwlock);
	poperror();
}

/*
 *  detach a device from an interface, close the interface
 *  called with ifc->conv closed
 */
static void ipifcunbind(struct Ipifc *ifc)
{
	ERRSTACK(1);
	char *err;

	wlock(&ifc->rwlock);
	if (waserror()) {
		wunlock(&ifc->rwlock);
		nexterror();
	}

	/* dissociate routes */
	if (ifc->m != NULL && ifc->m->unbindonclose == 0)
		ifc->conv->inuse--;
	ifc->ifcid++;

	/* disassociate device */
	if (ifc->m != NULL && ifc->m->unbind)
		(*ifc->m->unbind) (ifc);
	memset(ifc->dev, 0, sizeof(ifc->dev));
	ifc->arg = NULL;
	ifc->reassemble = 0;

	/* close queues to stop queuing of packets */
	qclose(ifc->conv->rq);
	qclose(ifc->conv->wq);
	qclose(ifc->conv->sq);

	/* disassociate logical interfaces */
	while (ifc->lifc)
		ipifcremlifc(ifc, ifc->lifc);

	ifc->m = NULL;
	wunlock(&ifc->rwlock);
	poperror();
}

char sfixedformat[] =
	"device %s maxtu %d sendra %d recvra %d mflag %d oflag %d maxraint %d minraint %d linkmtu %d reachtime %d rxmitra %d ttl %d routerlt %d pktin %lu pktout %lu errin %lu errout %lu tracedrop %lu\n";

char slineformat[] = "	%-40I %-10M %-40I %-12lu %-12lu\n";

static int ipifcstate(struct conv *c, char *state, int n)
{
	struct Ipifc *ifc;
	struct Iplifc *lifc;
	int m;

	ifc = (struct Ipifc *)c->ptcl;

	m = snprintf(state, n, sfixedformat,
				 ifc->dev, ifc->maxtu, ifc->sendra6, ifc->recvra6,
				 ifc->rp.mflag, ifc->rp.oflag, ifc->rp.maxraint,
				 ifc->rp.minraint, ifc->rp.linkmtu, ifc->rp.reachtime,
				 ifc->rp.rxmitra, ifc->rp.ttl, ifc->rp.routerlt,
				 ifc->in, ifc->out, ifc->inerr, ifc->outerr, ifc->tracedrop);

	rlock(&ifc->rwlock);
	for (lifc = ifc->lifc; lifc && n > m; lifc = lifc->next)
		m += snprintf(state + m, n - m, slineformat,
					  lifc->local, lifc->mask, lifc->remote,
					  lifc->validlt, lifc->preflt);
	if (ifc->lifc == NULL)
		m += snprintf(state + m, n - m, "\n");
	runlock(&ifc->rwlock);
	return m;
}

static int ipifclocal(struct conv *c, char *state, int n)
{
	struct Ipifc *ifc;
	struct Iplifc *lifc;
	struct Iplink *link;
	int m;

	ifc = (struct Ipifc *)c->ptcl;

	m = 0;

	rlock(&ifc->rwlock);
	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		m += snprintf(state + m, n - m, "%-40.40I ->", lifc->local);
		for (link = lifc->link; link; link = link->lifclink)
			m += snprintf(state + m, n - m, " %-40.40I", link->self->a);
		m += snprintf(state + m, n - m, "\n");
	}
	runlock(&ifc->rwlock);
	return m;
}

static int ipifcinuse(struct conv *c)
{
	struct Ipifc *ifc;

	ifc = (struct Ipifc *)c->ptcl;
	return ifc->m != NULL;
}

/*
 *  called when a process writes to an interface's 'data'
 */
static void ipifckick(void *x)
{
	ERRSTACK(1);
	struct conv *c = x;
	struct block *bp;
	struct Ipifc *ifc;

	bp = qget(c->wq);
	if (bp == NULL)
		return;

	ifc = (struct Ipifc *)c->ptcl;
	if (!canrlock(&ifc->rwlock)) {
		freeb(bp);
		return;
	}
	if (waserror()) {
		runlock(&ifc->rwlock);
		nexterror();
	}
	if (ifc->m == NULL || ifc->m->pktin == NULL)
		freeb(bp);
	else
		(*ifc->m->pktin) (c->p->f, ifc, bp);
	runlock(&ifc->rwlock);
	poperror();
}

/*
 *  called when a new ipifc structure is created
 */
static void ipifccreate(struct conv *c)
{
	struct Ipifc *ifc;

	c->rq = qopen(QMAX, 0, 0, 0);
	c->sq = qopen(2 * QMAX, Qmsg | Qcoalesce, 0, 0);
	c->wq = qopen(QMAX, Qkick, ipifckick, c);
	ifc = (struct Ipifc *)c->ptcl;
	ifc->conv = c;
	ifc->unbinding = 0;
	ifc->m = NULL;
	ifc->reassemble = 0;
	rwinit(&ifc->rwlock);
	/* These are never used, but we might need them if we ever do "unbind on the
	 * fly" (see ip.h).  Not sure where the code went that used these vars. */
	spinlock_init(&ifc->idlock);
	rendez_init(&ifc->wait);
}

/*
 *  called after last close of ipifc data or ctl
 *  called with c locked, we must unlock
 */
static void ipifcclose(struct conv *c)
{
	struct Ipifc *ifc;
	struct medium *m;

	ifc = (struct Ipifc *)c->ptcl;
	m = ifc->m;
	if (m != NULL && m->unbindonclose)
		ipifcunbind(ifc);
}

/*
 *  change an interface's mtu
 */
static void ipifcsetmtu(struct Ipifc *ifc, char **argv, int argc)
{
	int mtu;

	if (argc < 2)
		error(EINVAL, "Too few args (%d) to %s", argc, __func__);
	if (ifc->m == NULL)
		error(EFAIL, "No medium on IFC");
	mtu = strtoul(argv[1], 0, 0);
	if (mtu < ifc->m->mintu || mtu > ifc->m->maxtu)
		error(EFAIL, "Bad MTU size %d (%d, %d)", mtu, ifc->m->mintu,
		      ifc->m->maxtu);
	ifc->maxtu = mtu;
}

/*
 *  add an address to an interface.
 */
static void ipifcadd(struct Ipifc *ifc, char **argv, int argc, int tentative,
                     struct Iplifc *lifcp)
{
	ERRSTACK(1);
	uint8_t ip[IPaddrlen], mask[IPaddrlen], rem[IPaddrlen];
	uint8_t bcast[IPaddrlen], net[IPaddrlen];
	struct Iplifc *lifc, **l;
	int i, type, mtu;
	struct Fs *f;
	int sendnbrdisc = 0;

	if (ifc->m == NULL)
		error(EFAIL, "ipifc not yet bound to device");

	f = ifc->conv->p->f;

	type = Rifc;
	memset(ip, 0, IPaddrlen);
	memset(mask, 0, IPaddrlen);
	memset(rem, 0, IPaddrlen);
	switch (argc) {
		case 6:
			if (strcmp(argv[5], "proxy") == 0)
				type |= Rproxy;
			/* fall through */
		case 5:
			mtu = strtoul(argv[4], 0, 0);
			if (mtu >= ifc->m->mintu && mtu <= ifc->m->maxtu)
				ifc->maxtu = mtu;
			/* fall through */
		case 4:
			parseip(ip, argv[1]);
			parseipmask(mask, argv[2]);
			parseip(rem, argv[3]);
			maskip(rem, mask, net);
			break;
		case 3:
			parseip(ip, argv[1]);
			parseipmask(mask, argv[2]);
			maskip(ip, mask, rem);
			maskip(rem, mask, net);
			break;
		case 2:
			parseip(ip, argv[1]);
			memmove(mask, defmask(ip), IPaddrlen);
			maskip(ip, mask, rem);
			maskip(rem, mask, net);
			break;
		default:
			error(EINVAL, "Bad arg num to %s", __func__);
	}
	if (isv4(ip))
		tentative = 0;
	wlock(&ifc->rwlock);
	if (waserror()) {
		warn("Unexpected error thrown: %s", current_errstr());
		wunlock(&ifc->rwlock);
		nexterror();
	}

	/* ignore if this is already a local address for this ifc */
	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		if (ipcmp(lifc->local, ip) == 0) {
			if (lifc->tentative != tentative)
				lifc->tentative = tentative;
			if (lifcp != NULL) {
				lifc->onlink = lifcp->onlink;
				lifc->autoflag = lifcp->autoflag;
				lifc->validlt = lifcp->validlt;
				lifc->preflt = lifcp->preflt;
				lifc->origint = lifcp->origint;
			}
			goto out;
		}
	}

	/* add the address to the list of logical ifc's for this ifc */
	lifc = kzmalloc(sizeof(struct Iplifc), 0);
	ipmove(lifc->local, ip);
	ipmove(lifc->mask, mask);
	ipmove(lifc->remote, rem);
	ipmove(lifc->net, net);
	lifc->tentative = tentative;
	if (lifcp != NULL) {
		lifc->onlink = lifcp->onlink;
		lifc->autoflag = lifcp->autoflag;
		lifc->validlt = lifcp->validlt;
		lifc->preflt = lifcp->preflt;
		lifc->origint = lifcp->origint;
	} else {	// default values
		lifc->onlink = 1;
		lifc->autoflag = 1;
		lifc->validlt = UINT64_MAX;
		lifc->preflt = UINT64_MAX;
		lifc->origint = NOW / 10 ^ 3;
	}
	lifc->next = NULL;

	for (l = &ifc->lifc; *l; l = &(*l)->next) ;
	*l = lifc;

	/* check for point-to-point interface */
	if (ipcmp(ip, v6loopback))	/* skip v6 loopback, it's a special address */
		if (ipcmp(mask, IPallbits) == 0)
			type |= Rptpt;

	/* add local routes */
	if (isv4(ip))
		v4addroute(f, tifc, rem + IPv4off, mask + IPv4off, rem + IPv4off, type);
	else
		v6addroute(f, tifc, rem, mask, rem, type);

	addselfcache(f, ifc, lifc, ip, Runi);

	if ((type & (Rproxy | Rptpt)) == (Rproxy | Rptpt)) {
		ipifcregisterproxy(f, ifc, rem);
		goto out;
	}

	if (isv4(ip) || ipcmp(ip, IPnoaddr) == 0) {
		/* add subnet directed broadcast address to the self cache */
		for (i = 0; i < IPaddrlen; i++)
			bcast[i] = (ip[i] & mask[i]) | ~mask[i];
		addselfcache(f, ifc, lifc, bcast, Rbcast);

		/* add subnet directed network address to the self cache */
		for (i = 0; i < IPaddrlen; i++)
			bcast[i] = (ip[i] & mask[i]) & mask[i];
		addselfcache(f, ifc, lifc, bcast, Rbcast);

		/* add network directed broadcast address to the self cache */
		memmove(mask, defmask(ip), IPaddrlen);
		for (i = 0; i < IPaddrlen; i++)
			bcast[i] = (ip[i] & mask[i]) | ~mask[i];
		addselfcache(f, ifc, lifc, bcast, Rbcast);

		/* add network directed network address to the self cache */
		memmove(mask, defmask(ip), IPaddrlen);
		for (i = 0; i < IPaddrlen; i++)
			bcast[i] = (ip[i] & mask[i]) & mask[i];
		addselfcache(f, ifc, lifc, bcast, Rbcast);

		addselfcache(f, ifc, lifc, IPv4bcast, Rbcast);
	} else {
		if (ipcmp(ip, v6loopback) == 0) {
			/* add node-local mcast address */
			addselfcache(f, ifc, lifc, v6allnodesN, Rmulti);

			/* add route for all node multicast */
			v6addroute(f, tifc, v6allnodesN, v6allnodesNmask, v6allnodesN,
					   Rmulti);
		}

		/* add all nodes multicast address */
		addselfcache(f, ifc, lifc, v6allnodesL, Rmulti);

		/* add route for all nodes multicast */
		v6addroute(f, tifc, v6allnodesL, v6allnodesLmask, v6allnodesL, Rmulti);

		/* add solicited-node multicast address */
		ipv62smcast(bcast, ip);
		addselfcache(f, ifc, lifc, bcast, Rmulti);

		sendnbrdisc = 1;
	}

	/* register the address on this network for address resolution */
	if (isv4(ip) && ifc->m->areg != NULL)
		(*ifc->m->areg) (ifc, ip);

out:
	wunlock(&ifc->rwlock);
	if (tentative && sendnbrdisc)
		icmpns(f, 0, SRC_UNSPEC, ip, TARG_MULTI, ifc->mac);
	poperror();
}

/*
 *  remove a logical interface from an ifc
 *  always called with ifc wlock'd
 */
static void ipifcremlifc(struct Ipifc *ifc, struct Iplifc *lifc)
{
	struct Iplifc **l;
	struct Fs *f;

	f = ifc->conv->p->f;

	/*
	 *  find address on this interface and remove from chain.
	 *  for pt to pt we actually specify the remote address as the
	 *  addresss to remove.
	 */
	for (l = &ifc->lifc; *l != NULL && *l != lifc; l = &(*l)->next) ;
	if (*l == NULL)
		error(EFAIL, "address not on this interface");
	*l = lifc->next;

	/* disassociate any addresses */
	while (lifc->link)
		remselfcache(f, ifc, lifc, lifc->link->self->a);

	/* remove the route for this logical interface */
	if (isv4(lifc->local))
		v4delroute(f, lifc->remote + IPv4off, lifc->mask + IPv4off, 1);
	else {
		v6delroute(f, lifc->remote, lifc->mask, 1);
		if (ipcmp(lifc->local, v6loopback) == 0)
			/* remove route for all node multicast */
			v6delroute(f, v6allnodesN, v6allnodesNmask, 1);
		else if (memcmp(lifc->local, v6linklocal, v6llpreflen) == 0)
			/* remove route for all link multicast */
			v6delroute(f, v6allnodesL, v6allnodesLmask, 1);
	}

	kfree(lifc);
}

/*
 *  remove an address from an interface.
 *  called with c locked
 */
static void ipifcrem(struct Ipifc *ifc, char **argv, int argc)
{
	ERRSTACK(1);
	uint8_t ip[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t rem[IPaddrlen];
	struct Iplifc *lifc;

	if (argc < 3)
		error(EINVAL, "Too few args (%d) to %s", argc, __func__);

	parseip(ip, argv[1]);
	parseipmask(mask, argv[2]);
	if (argc < 4)
		maskip(ip, mask, rem);
	else
		parseip(rem, argv[3]);

	wlock(&ifc->rwlock);
	if (waserror()) {
		wunlock(&ifc->rwlock);
		nexterror();
	}

	/*
	 *  find address on this interface and remove from chain.
	 *  for pt to pt we actually specify the remote address as the
	 *  addresss to remove.
	 */
	for (lifc = ifc->lifc; lifc != NULL; lifc = lifc->next) {
		if (memcmp(ip, lifc->local, IPaddrlen) == 0
			&& memcmp(mask, lifc->mask, IPaddrlen) == 0
			&& memcmp(rem, lifc->remote, IPaddrlen) == 0)
			break;
	}

	ipifcremlifc(ifc, lifc);
	poperror();
	wunlock(&ifc->rwlock);
}

/*
 * distribute routes to active interfaces like the
 * TRIP linecards
 */
void
ipifcaddroute(struct Fs *f, int vers, uint8_t * addr, uint8_t * mask,
			  uint8_t * gate, int type)
{
	struct medium *m;
	struct conv **cp, **e;
	struct Ipifc *ifc;

	e = &f->ipifc->conv[f->ipifc->nc];
	for (cp = f->ipifc->conv; cp < e; cp++) {
		if (*cp != NULL) {
			ifc = (struct Ipifc *)(*cp)->ptcl;
			m = ifc->m;
			if (m == NULL)
				continue;
			if (m->addroute != NULL)
				m->addroute(ifc, vers, addr, mask, gate, type);
		}
	}
}

void ipifcremroute(struct Fs *f, int vers, uint8_t * addr, uint8_t * mask)
{
	struct medium *m;
	struct conv **cp, **e;
	struct Ipifc *ifc;

	e = &f->ipifc->conv[f->ipifc->nc];
	for (cp = f->ipifc->conv; cp < e; cp++) {
		if (*cp != NULL) {
			ifc = (struct Ipifc *)(*cp)->ptcl;
			m = ifc->m;
			if (m == NULL)
				continue;
			if (m->remroute != NULL)
				m->remroute(ifc, vers, addr, mask);
		}
	}
}

/*
 *  associate an address with the interface.  This wipes out any previous
 *  addresses.  This is a macro that means, remove all the old interfaces
 *  and add a new one.
 */
static void ipifcconnect(struct conv *c, char **argv, int argc)
{
	ERRSTACK(1);
	char *err;
	struct Ipifc *ifc;

	ifc = (struct Ipifc *)c->ptcl;

	if (ifc->m == NULL)
		error(EFAIL, "ipifc not yet bound to device");

	wlock(&ifc->rwlock);
	if (waserror()) {
		wunlock(&ifc->rwlock);
		nexterror();
	}
	while (ifc->lifc)
		ipifcremlifc(ifc, ifc->lifc);
	wunlock(&ifc->rwlock);
	poperror();

	ipifcadd(ifc, argv, argc, 0, NULL);

	Fsconnected(c, NULL);
}

static void ipifcsetpar6(struct Ipifc *ifc, char **argv, int argc)
{
	int i, argsleft, vmax = ifc->rp.maxraint, vmin = ifc->rp.minraint;

	argsleft = argc - 1;
	i = 1;

	if (argsleft % 2 != 0)
		error(EINVAL, "Non-even number of args (%d) to %s", argc, __func__);

	while (argsleft > 1) {
		if (strcmp(argv[i], "recvra") == 0)
			ifc->recvra6 = (atoi(argv[i + 1]) != 0);
		else if (strcmp(argv[i], "sendra") == 0)
			ifc->sendra6 = (atoi(argv[i + 1]) != 0);
		else if (strcmp(argv[i], "mflag") == 0)
			ifc->rp.mflag = (atoi(argv[i + 1]) != 0);
		else if (strcmp(argv[i], "oflag") == 0)
			ifc->rp.oflag = (atoi(argv[i + 1]) != 0);
		else if (strcmp(argv[i], "maxraint") == 0)
			ifc->rp.maxraint = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "minraint") == 0)
			ifc->rp.minraint = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "linkmtu") == 0)
			ifc->rp.linkmtu = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "reachtime") == 0)
			ifc->rp.reachtime = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "rxmitra") == 0)
			ifc->rp.rxmitra = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "ttl") == 0)
			ifc->rp.ttl = atoi(argv[i + 1]);
		else if (strcmp(argv[i], "routerlt") == 0)
			ifc->rp.routerlt = atoi(argv[i + 1]);
		else
			error(EINVAL, "unknown command to %s", __func__);

		argsleft -= 2;
		i += 2;
	}

	// consistency check
	if (ifc->rp.maxraint < ifc->rp.minraint) {
		ifc->rp.maxraint = vmax;
		ifc->rp.minraint = vmin;
		error(EINVAL, "inconsistent ifc->rp 'raint'");
	}
}

static void ipifcsendra6(struct Ipifc *ifc, char **argv, int argc)
{
	int i;

	i = 0;
	if (argc > 1)
		i = atoi(argv[1]);
	ifc->sendra6 = (i != 0);
}

static void ipifcrecvra6(struct Ipifc *ifc, char **argv, int argc)
{
	int i;

	i = 0;
	if (argc > 1)
		i = atoi(argv[1]);
	ifc->recvra6 = (i != 0);
}

static void ipifc_iprouting(struct Fs *f, char **argv, int argc)
{
	int i = 1;

	if (argc > 1)
		i = atoi(argv[1]);
	iprouting(f, i);
}

/*
 *  non-standard control messages.
 *  called with c locked.
 */
static void ipifcctl(struct conv *c, char **argv, int argc)
{
	struct Ipifc *ifc;
	int i;

	ifc = (struct Ipifc *)c->ptcl;
	if (strcmp(argv[0], "add") == 0)
		ipifcadd(ifc, argv, argc, 0, NULL);
	else if (strcmp(argv[0], "try") == 0)
		ipifcadd(ifc, argv, argc, 1, NULL);
	else if (strcmp(argv[0], "remove") == 0)
		ipifcrem(ifc, argv, argc);
	else if (strcmp(argv[0], "unbind") == 0)
		ipifcunbind(ifc);
	else if (strcmp(argv[0], "joinmulti") == 0)
		ipifcjoinmulti(ifc, argv, argc);
	else if (strcmp(argv[0], "leavemulti") == 0)
		ipifcleavemulti(ifc, argv, argc);
	else if (strcmp(argv[0], "mtu") == 0)
		ipifcsetmtu(ifc, argv, argc);
	else if (strcmp(argv[0], "reassemble") == 0)
		ifc->reassemble = 1;
	else if (strcmp(argv[0], "iprouting") == 0)
		ipifc_iprouting(c->p->f, argv, argc);
	else if (strcmp(argv[0], "addpref6") == 0)
		ipifcaddpref6(ifc, argv, argc);
	else if (strcmp(argv[0], "setpar6") == 0)
		ipifcsetpar6(ifc, argv, argc);
	else if (strcmp(argv[0], "sendra6") == 0)
		ipifcsendra6(ifc, argv, argc);
	else if (strcmp(argv[0], "recvra6") == 0)
		ipifcrecvra6(ifc, argv, argc);
	else
		error(EINVAL, "unknown command to %s", __func__);
}

int ipifcstats(struct Proto *ipifc, char *buf, int len)
{
	return ipstats(ipifc->f, buf, len);
}

void ipifcinit(struct Fs *f)
{
	struct Proto *ipifc;

	ipifc = kzmalloc(sizeof(struct Proto), 0);
	ipifc->name = "ipifc";
	ipifc->connect = ipifcconnect;
	ipifc->announce = NULL;
	ipifc->bind = ipifcbind;
	ipifc->state = ipifcstate;
	ipifc->create = ipifccreate;
	ipifc->close = ipifcclose;
	ipifc->rcv = NULL;
	ipifc->ctl = ipifcctl;
	ipifc->advise = NULL;
	ipifc->stats = ipifcstats;
	ipifc->inuse = ipifcinuse;
	ipifc->local = ipifclocal;
	ipifc->ipproto = -1;
	ipifc->nc = Maxmedia;
	ipifc->ptclsize = sizeof(struct Ipifc);

	f->ipifc = ipifc;	/* hack for ipifcremroute, findipifc, ... */
	f->self = kzmalloc(sizeof(struct Ipselftab), 0);	/* hack for ipforme */
	qlock_init(&f->self->qlock);

	Fsproto(f, ipifc);
}

/*
 * TODO: Change this to a proper release.
 * At the moment, this is difficult since link removal
 * requires access to more than just the kref/struct Iplink.
 * E.g., the self and interface pointers.
 */
static void link_release(struct kref *kref)
{
	(void)kref;
}

/*
 *  add to self routing cache
 *	called with c locked
 */
static void
addselfcache(struct Fs *f, struct Ipifc *ifc,
			 struct Iplifc *lifc, uint8_t * a, int type)
{
	struct Ipself *p;
	struct Iplink *lp;
	int h;

	qlock(&f->self->qlock);

	/* see if the address already exists */
	h = hashipa(a);
	for (p = f->self->hash[h]; p; p = p->next)
		if (memcmp(a, p->a, IPaddrlen) == 0)
			break;

	/* allocate a local address and add to hash chain */
	if (p == NULL) {
		p = kzmalloc(sizeof(*p), 0);
		ipmove(p->a, a);
		p->type = type;
		p->next = f->self->hash[h];
		f->self->hash[h] = p;

		/* if the null address, accept all packets */
		if (ipcmp(a, v4prefix) == 0 || ipcmp(a, IPnoaddr) == 0)
			f->self->acceptall = 1;
	}

	/* look for a link for this lifc */
	for (lp = p->link; lp; lp = lp->selflink)
		if (lp->lifc == lifc)
			break;

	/* allocate a lifc-to-local link and link to both */
	if (lp == NULL) {
		lp = kzmalloc(sizeof(*lp), 0);
		kref_init(&lp->ref, link_release, 1);
		lp->lifc = lifc;
		lp->self = p;
		lp->selflink = p->link;
		p->link = lp;
		lp->lifclink = lifc->link;
		lifc->link = lp;

		/* add to routing table */
		if (isv4(a))
			v4addroute(f, tifc, a + IPv4off, IPallbits + IPv4off, a + IPv4off,
					   type);
		else
			v6addroute(f, tifc, a, IPallbits, a, type);

		if ((type & Rmulti) && ifc->m->addmulti != NULL)
			(*ifc->m->addmulti) (ifc, a, lifc->local);
	} else {
		kref_get(&lp->ref, 1);
	}

	qunlock(&f->self->qlock);
}

/*
 *  These structures are unlinked from their chains while
 *  other threads may be using them.  To avoid excessive locking,
 *  just put them aside for a while before freeing them.
 *	called with f->self locked
 */
static struct Iplink *freeiplink;
static struct Ipself *freeipself;

static void iplinkfree(struct Iplink *p)
{
	struct Iplink **l, *np;
	uint64_t now = NOW;

	l = &freeiplink;
	for (np = *l; np; np = *l) {
		if (np->expire > now) {
			*l = np->next;
			kfree(np);
			continue;
		}
		l = &np->next;
	}
	p->expire = now + 5000;	/* give other threads 5 secs to get out */
	p->next = NULL;
	*l = p;
}

static void ipselffree(struct Ipself *p)
{
	struct Ipself **l, *np;
	uint64_t now = NOW;

	l = &freeipself;
	for (np = *l; np; np = *l) {
		if (np->expire > now) {
			*l = np->next;
			kfree(np);
			continue;
		}
		l = &np->next;
	}
	p->expire = now + 5000;	/* give other threads 5 secs to get out */
	p->next = NULL;
	*l = p;
}

/*
 *  Decrement reference for this address on this link.
 *  Unlink from selftab if this is the last ref.
 *	called with c locked
 */
static void
remselfcache(struct Fs *f, struct Ipifc *ifc, struct Iplifc *lifc, uint8_t *a)
{
	struct Ipself *p, **l;
	struct Iplink *link, **l_self, **l_lifc;

	qlock(&f->self->qlock);

	/* find the unique selftab entry */
	l = &f->self->hash[hashipa(a)];
	for (p = *l; p; p = *l) {
		if (ipcmp(p->a, a) == 0)
			break;
		l = &p->next;
	}

	if (p == NULL)
		goto out;

	/*
	 *  walk down links from an ifc looking for one
	 *  that matches the selftab entry
	 */
	l_lifc = &lifc->link;
	for (link = *l_lifc; link; link = *l_lifc) {
		if (link->self == p)
			break;
		l_lifc = &link->lifclink;
	}

	if (link == NULL)
		goto out;

	/*
	 *  walk down the links from the selftab looking for
	 *  the one we just found
	 */
	l_self = &p->link;
	for (link = *l_self; link; link = *l_self) {
		if (link == *(l_lifc))
			break;
		l_self = &link->selflink;
	}

	if (link == NULL)
		panic("remselfcache");

	/*
	 * TODO: The check for 'refcnt > 0' here is awkward.  It
	 * exists so that 'remselfcache' can be called concurrently.
	 * In the original plan 9 code, the 'goto out' branch was
	 * taken if the decremented reference count was exactly zero.
	 * In other threads this could become -1, which plan 9 didn't
	 * care about since the logic would be skipped over, and since
	 * 'iplinkfree' won't _actually_ free the link for five seconds
	 * (see comments in that function), and since all of the actual
	 * link manipulation happens in the thread where the reference
	 * is exactly equal to zero.  But on Akaros, a negative kref
	 * will panic; hence checking for a positive ref count before
	 * decrementing.  This entire mechanism is dubious.  But Since
	 * this function is protected by a lock this is probably OK for
	 * the time being.
	 *
	 * However, it is a terrible design and we should fix it.
	 */
	if (kref_refcnt(&link->ref) > 0 && kref_put(&link->ref) != 0)
		goto out;

	if ((p->type & Rmulti) && ifc->m->remmulti != NULL)
		(*ifc->m->remmulti) (ifc, a, lifc->local);

	/* ref == 0, remove from both chains and free the link */
	*l_lifc = link->lifclink;
	*l_self = link->selflink;
	iplinkfree(link);

	if (p->link != NULL)
		goto out;

	/* remove from routing table */
	if (isv4(a))
		v4delroute(f, a + IPv4off, IPallbits + IPv4off, 1);
	else
		v6delroute(f, a, IPallbits, 1);

	/* no more links, remove from hash and free */
	*l = p->next;
	ipselffree(p);

	/* if IPnoaddr, forget */
	if (ipcmp(a, v4prefix) == 0 || ipcmp(a, IPnoaddr) == 0)
		f->self->acceptall = 0;

out:
	qunlock(&f->self->qlock);
}

static char *stformat = "%-44.44I %2.2d %4.4s\n";
enum {
	Nstformat = 41,
};

long ipselftabread(struct Fs *f, char *cp, uint32_t offset, int n)
{
	int i, m, nifc, off;
	struct Ipself *p;
	struct Iplink *link;
	char state[8];

	m = 0;
	off = offset;
	qlock(&f->self->qlock);
	for (i = 0; i < NHASH && m < n; i++) {
		for (p = f->self->hash[i]; p != NULL && m < n; p = p->next) {
			nifc = 0;
			for (link = p->link; link; link = link->selflink)
				nifc++;
			routetype(p->type, state);
			m += snprintf(cp + m, n - m, stformat, p->a, nifc, state);
			if (off > 0) {
				off -= m;
				m = 0;
			}
		}
	}
	qunlock(&f->self->qlock);
	return m;
}

int iptentative(struct Fs *f, uint8_t * addr)
{
	struct Ipself *p;

	p = f->self->hash[hashipa(addr)];
	for (; p; p = p->next) {
		if (ipcmp(addr, p->a) == 0) {
			return p->link->lifc->tentative;
		}
	}
	return 0;
}

/*
 *  returns
 *	0		- no match
 *	Runi
 *	Rbcast
 *	Rmcast
 */
int ipforme(struct Fs *f, uint8_t * addr)
{
	struct Ipself *p;

	p = f->self->hash[hashipa(addr)];
	for (; p; p = p->next) {
		if (ipcmp(addr, p->a) == 0)
			return p->type;
	}

	/* hack to say accept anything */
	if (f->self->acceptall)
		return Runi;

	return 0;
}

/*
 *  find the ifc on same net as the remote system.  If none,
 *  return NULL.
 */
struct Ipifc *findipifc(struct Fs *f, uint8_t * remote, int type)
{
	struct Ipifc *ifc, *x;
	struct Iplifc *lifc;
	struct conv **cp, **e;
	uint8_t gnet[IPaddrlen];
	uint8_t xmask[IPaddrlen];

	x = NULL;
	memset(xmask, 0, IPaddrlen);

	/* find most specific match */
	e = &f->ipifc->conv[f->ipifc->nc];
	for (cp = f->ipifc->conv; cp < e; cp++) {
		if (*cp == 0)
			continue;

		ifc = (struct Ipifc *)(*cp)->ptcl;

		for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
			maskip(remote, lifc->mask, gnet);
			if (ipcmp(gnet, lifc->net) == 0) {
				if (x == NULL || ipcmp(lifc->mask, xmask) > 0) {
					x = ifc;
					ipmove(xmask, lifc->mask);
				}
			}
		}
	}
	if (x != NULL)
		return x;

	/* for now for broadcast and multicast, just use first interface */
	if (type & (Rbcast | Rmulti)) {
		for (cp = f->ipifc->conv; cp < e; cp++) {
			if (*cp == 0)
				continue;
			ifc = (struct Ipifc *)(*cp)->ptcl;
			if (ifc->lifc != NULL)
				return ifc;
		}
	}

	return NULL;
}

enum {
	unknownv6,
	multicastv6,
	unspecifiedv6,
	linklocalv6,
	sitelocalv6,
	globalv6,
};

int v6addrtype(uint8_t * addr)
{
	if (isv6global(addr))
		return globalv6;
	if (islinklocal(addr))
		return linklocalv6;
	if (isv6mcast(addr))
		return multicastv6;
	if (issitelocal(addr))
		return sitelocalv6;
	return unknownv6;
}

#define v6addrcurr(lifc) (( (lifc)->origint + (lifc)->preflt >= (NOW/10^3) ) || ( (lifc)->preflt == UINT64_MAX ))

static void findprimaryipv6(struct Fs *f, uint8_t * local)
{
	struct conv **cp, **e;
	struct Ipifc *ifc;
	struct Iplifc *lifc;
	int atype, atypel;

	ipmove(local, v6Unspecified);
	atype = unspecifiedv6;

	/* find "best" (global > sitelocal > link local > unspecified)
	 * local address; address must be current */

	e = &f->ipifc->conv[f->ipifc->nc];
	for (cp = f->ipifc->conv; cp < e; cp++) {
		if (*cp == 0)
			continue;
		ifc = (struct Ipifc *)(*cp)->ptcl;
		for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
			atypel = v6addrtype(lifc->local);
			if (atypel > atype)
				if (v6addrcurr(lifc)) {
					ipmove(local, lifc->local);
					atype = atypel;
					if (atype == globalv6)
						return;
				}
		}
	}
}

/*
 *  returns first ip address configured
 */
static void findprimaryipv4(struct Fs *f, uint8_t * local)
{
	struct conv **cp, **e;
	struct Ipifc *ifc;
	struct Iplifc *lifc;

	/* find first ifc local address */
	e = &f->ipifc->conv[f->ipifc->nc];
	for (cp = f->ipifc->conv; cp < e; cp++) {
		if (*cp == 0)
			continue;
		ifc = (struct Ipifc *)(*cp)->ptcl;
		if ((lifc = ifc->lifc) != NULL) {
			ipmove(local, lifc->local);
			return;
		}
	}
}

/*
 *  find the local address 'closest' to the remote system, copy it to
 *  local and return the ifc for that address
 */
void findlocalip(struct Fs *f, uint8_t * local, uint8_t * remote)
{
	struct Ipifc *ifc;
	struct Iplifc *lifc;
	struct route *r;
	uint8_t gate[IPaddrlen];
	uint8_t gnet[IPaddrlen];
	int version;
	int atype = unspecifiedv6, atypel = unknownv6;

	qlock(&f->ipifc->qlock);
	r = v6lookup(f, remote, NULL);
	version = isv4(remote) ? V4 : V6;

	if (r != NULL) {
		ifc = r->rt.ifc;
		if (r->rt.type & Rv4)
			v4tov6(gate, r->v4.gate);
		else {
			ipmove(gate, r->v6.gate);
			ipmove(local, v6Unspecified);
		}

		/* find ifc address closest to the gateway to use */
		switch (version) {
			case V4:
				for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
					maskip(gate, lifc->mask, gnet);
					if (ipcmp(gnet, lifc->net) == 0) {
						ipmove(local, lifc->local);
						goto out;
					}
				}
				break;
			case V6:
				for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
					atypel = v6addrtype(lifc->local);
					maskip(gate, lifc->mask, gnet);
					if (ipcmp(gnet, lifc->net) == 0)
						if (atypel > atype)
							if (v6addrcurr(lifc)) {
								ipmove(local, lifc->local);
								atype = atypel;
								if (atype == globalv6)
									break;
							}
				}
				if (atype > unspecifiedv6)
					goto out;
				break;
			default:
				panic("findlocalip: version %d", version);
		}
	}

	switch (version) {
		case V4:
			findprimaryipv4(f, local);
			break;
		case V6:
			findprimaryipv6(f, local);
			break;
		default:
			panic("findlocalip2: version %d", version);
	}

out:
	qunlock(&f->ipifc->qlock);
}

/*
 *  return first v4 address associated with an interface
 */
int ipv4local(struct Ipifc *ifc, uint8_t * addr)
{
	struct Iplifc *lifc;

	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		if (isv4(lifc->local)) {
			memmove(addr, lifc->local + IPv4off, IPv4addrlen);
			return 1;
		}
	}
	return 0;
}

/*
 *  return first v6 address associated with an interface
 */
int ipv6local(struct Ipifc *ifc, uint8_t * addr)
{
	struct Iplifc *lifc;

	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		if (!isv4(lifc->local) && !(lifc->tentative)) {
			ipmove(addr, lifc->local);
			return 1;
		}
	}
	return 0;
}

int ipv6anylocal(struct Ipifc *ifc, uint8_t * addr)
{
	struct Iplifc *lifc;

	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		if (!isv4(lifc->local)) {
			ipmove(addr, lifc->local);
			return SRC_UNI;
		}
	}
	return SRC_UNSPEC;
}

/*
 *  see if this address is bound to the interface
 */
struct Iplifc *iplocalonifc(struct Ipifc *ifc, uint8_t * ip)
{
	struct Iplifc *lifc;

	for (lifc = ifc->lifc; lifc; lifc = lifc->next)
		if (ipcmp(ip, lifc->local) == 0)
			return lifc;
	return NULL;
}

/*
 *  See if we're proxying for this address on this interface
 */
int ipproxyifc(struct Fs *f, struct Ipifc *ifc, uint8_t * ip)
{
	struct route *r;
	uint8_t net[IPaddrlen];
	struct Iplifc *lifc;

	/* see if this is a direct connected pt to pt address */
	r = v6lookup(f, ip, NULL);
	if (r == NULL)
		return 0;
	if ((r->rt.type & (Rifc | Rproxy)) != (Rifc | Rproxy))
		return 0;

	/* see if this is on the right interface */
	for (lifc = ifc->lifc; lifc; lifc = lifc->next) {
		maskip(ip, lifc->mask, net);
		if (ipcmp(net, lifc->remote) == 0)
			return 1;
	}

	return 0;
}

/*
 *  return multicast version if any
 */
int ipismulticast(uint8_t * ip)
{
	if (isv4(ip)) {
		if (ip[IPv4off] >= 0xe0 && ip[IPv4off] < 0xf0)
			return V4;
	} else {
		if (ip[0] == 0xff)
			return V6;
	}
	return 0;
}

int ipisbm(uint8_t * ip)
{
	if (isv4(ip)) {
		if (ip[IPv4off] >= 0xe0 && ip[IPv4off] < 0xf0)
			return V4;
		if (ipcmp(ip, IPv4bcast) == 0)
			return V4;
	} else {
		if (ip[0] == 0xff)
			return V6;
	}
	return 0;
}

/*
 *  add a multicast address to an interface, called with c locked
 */
void ipifcaddmulti(struct conv *c, uint8_t * ma, uint8_t * ia)
{
	ERRSTACK(1);
	struct Ipifc *ifc;
	struct Iplifc *lifc;
	struct conv **p;
	struct Ipmulti *multi, **l;
	struct Fs *f;

	f = c->p->f;

	for (l = &c->multi; *l; l = &(*l)->next)
		if (ipcmp(ma, (*l)->ma) == 0)
			if (ipcmp(ia, (*l)->ia) == 0)
				return;	/* it's already there */

	multi = *l = kzmalloc(sizeof(*multi), 0);
	ipmove(multi->ma, ma);
	ipmove(multi->ia, ia);
	multi->next = NULL;

	for (p = f->ipifc->conv; *p; p++) {
		if ((*p)->inuse == 0)
			continue;
		ifc = (struct Ipifc *)(*p)->ptcl;
		wlock(&ifc->rwlock);
		if (waserror()) {
			wunlock(&ifc->rwlock);
			nexterror();
		}
		for (lifc = ifc->lifc; lifc; lifc = lifc->next)
			if (ipcmp(ia, lifc->local) == 0)
				addselfcache(f, ifc, lifc, ma, Rmulti);
		wunlock(&ifc->rwlock);
		poperror();
	}
}

/* Trace a block @b that crosses the interface @ifc. */
void ipifc_trace_block(struct Ipifc *ifc, struct block *bp)
{
	struct block *newb;

	if (!atomic_read(&ifc->conv->snoopers))
		return;
	newb = copyblock(bp, MEM_ATOMIC);
	if (!newb) {
		ifc->tracedrop++;
		return;
	}
	if (qpass(ifc->conv->sq, newb) < 0)
		ifc->tracedrop++;
}

/*
 *  remove a multicast address from an interface, called with c locked
 */
void ipifcremmulti(struct conv *c, uint8_t * ma, uint8_t * ia)
{
	ERRSTACK(1);
	struct Ipmulti *multi, **l;
	struct Iplifc *lifc;
	struct conv **p;
	struct Ipifc *ifc;
	struct Fs *f;

	f = c->p->f;

	for (l = &c->multi; *l; l = &(*l)->next)
		if (ipcmp(ma, (*l)->ma) == 0)
			if (ipcmp(ia, (*l)->ia) == 0)
				break;

	multi = *l;
	if (multi == NULL)
		return;	/* we don't have it open */

	*l = multi->next;

	for (p = f->ipifc->conv; *p; p++) {
		if ((*p)->inuse == 0)
			continue;

		ifc = (struct Ipifc *)(*p)->ptcl;
		wlock(&ifc->rwlock);
		if (waserror()) {
			wunlock(&ifc->rwlock);
			nexterror();
		}
		for (lifc = ifc->lifc; lifc; lifc = lifc->next)
			if (ipcmp(ia, lifc->local) == 0)
				remselfcache(f, ifc, lifc, ma);
		wunlock(&ifc->rwlock);
		poperror();
	}

	kfree(multi);
}

/*
 *  make lifc's join and leave multicast groups
 */
static void ipifcjoinmulti(struct Ipifc *ifc, char **argv, int argc)
{
	warn_once("Not implemented, should it be?");
}

static void ipifcleavemulti(struct Ipifc *ifc, char **argv, int argc)
{
	warn_once("Not implemented, should it be?");
}

static void ipifcregisterproxy(struct Fs *f, struct Ipifc *ifc, uint8_t * ip)
{
	struct conv **cp, **e;
	struct Ipifc *nifc;
	struct Iplifc *lifc;
	struct medium *m;
	uint8_t net[IPaddrlen];

	/* register the address on any network that will proxy for us */
	e = &f->ipifc->conv[f->ipifc->nc];

	if (!isv4(ip)) {	// V6
		for (cp = f->ipifc->conv; cp < e; cp++) {
			if (*cp == NULL)
				continue;
			nifc = (struct Ipifc *)(*cp)->ptcl;
			if (nifc == ifc)
				continue;

			rlock(&nifc->rwlock);
			m = nifc->m;
			if (m == NULL || m->addmulti == NULL) {
				runlock(&nifc->rwlock);
				continue;
			}
			for (lifc = nifc->lifc; lifc; lifc = lifc->next) {
				maskip(ip, lifc->mask, net);
				if (ipcmp(net, lifc->remote) == 0) {	/* add solicited-node multicast address */
					ipv62smcast(net, ip);
					addselfcache(f, nifc, lifc, net, Rmulti);
					arpenter(f, V6, ip, nifc->mac, 6, 0);
					//(*m->addmulti)(nifc, net, ip);
					break;
				}
			}
			runlock(&nifc->rwlock);
		}
		return;
	} else {	// V4
		for (cp = f->ipifc->conv; cp < e; cp++) {
			if (*cp == NULL)
				continue;
			nifc = (struct Ipifc *)(*cp)->ptcl;
			if (nifc == ifc)
				continue;

			rlock(&nifc->rwlock);
			m = nifc->m;
			if (m == NULL || m->areg == NULL) {
				runlock(&nifc->rwlock);
				continue;
			}
			for (lifc = nifc->lifc; lifc; lifc = lifc->next) {
				maskip(ip, lifc->mask, net);
				if (ipcmp(net, lifc->remote) == 0) {
					(*m->areg) (nifc, ip);
					break;
				}
			}
			runlock(&nifc->rwlock);
		}
	}
}

// added for new v6 mesg types
static void adddefroute6(struct Fs *f, uint8_t * gate, int force)
{
	struct route *r;

	r = v6lookup(f, v6Unspecified, NULL);
	if (r != NULL)
		if (!(force) && (strcmp(r->rt.tag, "ra") != 0))	// route entries generated
			return;	// by all other means take
	// precedence over router annc

	v6delroute(f, v6Unspecified, v6Unspecified, 1);
	v6addroute(f, "ra", v6Unspecified, v6Unspecified, gate, 0);
}

enum {
	Ngates = 3,
};

static void ipifcaddpref6(struct Ipifc *ifc, char **argv, int argc)
{
	uint8_t onlink = 1;
	uint8_t autoflag = 1;
	uint64_t validlt = UINT64_MAX;
	uint64_t preflt = UINT64_MAX;
	uint64_t origint = NOW / 10 ^ 3;
	uint8_t prefix[IPaddrlen];
	int plen = 64;
	struct Iplifc *lifc;
	char addr[40], preflen[6];
	char *params[3];

	switch (argc) {
		case 7:
			preflt = atoi(argv[6]);
			/* fall through */
		case 6:
			validlt = atoi(argv[5]);
			/* fall through */
		case 5:
			autoflag = atoi(argv[4]);
			/* fall through */
		case 4:
			onlink = atoi(argv[3]);
			/* fall through */
		case 3:
			plen = atoi(argv[2]);
		case 2:
			break;
		default:
			error(EINVAL, "Bad arg num to %s", __func__);
	}

	if ((parseip(prefix, argv[1]) != 6) ||
		(validlt < preflt) || (plen < 0) || (plen > 64) || (islinklocal(prefix))
		)
		error(EFAIL, "IP parsing failed");

	lifc = kzmalloc(sizeof(struct Iplifc), 0);
	lifc->onlink = (onlink != 0);
	lifc->autoflag = (autoflag != 0);
	lifc->validlt = validlt;
	lifc->preflt = preflt;
	lifc->origint = origint;

	if (ifc->m->pref2addr != NULL)
		ifc->m->pref2addr(prefix, ifc->mac);
	else
		error(EFAIL, "Null IFC pref");

	snprintf(addr, sizeof(addr), "%I", prefix);
	snprintf(preflen, sizeof(preflen), "/%d", plen);
	params[0] = "add";
	params[1] = addr;
	params[2] = preflen;

	ipifcadd(ifc, params, 3, 0, lifc);
}
