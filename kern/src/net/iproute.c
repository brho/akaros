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

static void walkadd(struct Fs *, struct route **, struct route *);
static void addnode(struct Fs *, struct route **, struct route *);
static void calcd(struct route *);

/* these are used for all instances of IP */
struct route *v4freelist;
struct route *v6freelist;
rwlock_t routelock;
uint32_t v4routegeneration, v6routegeneration;

/*
 * TODO: Change this to a proper release.
 * At the moment this is difficult to do since deleting
 * a route involves manipulating more data structures than
 * a kref/struct route.  E.g., unlinking from the route tree
 * requires access to a parent pointer, which doesn't exist
 * in the route structure itself.
 */
static void route_release(struct kref *kref)
{
	(void)kref;
}

static void freeroute(struct route *r)
{
	struct route **l;

	r->rt.left = NULL;
	r->rt.right = NULL;
	if (r->rt.type & Rv4)
		l = &v4freelist;
	else
		l = &v6freelist;
	r->rt.mid = *l;
	*l = r;
}

static struct route *allocroute(int type)
{
	struct route *r;
	int n;
	struct route **l;

	if (type & Rv4) {
		n = sizeof(struct RouteTree) + sizeof(struct V4route);
		l = &v4freelist;
	} else {
		n = sizeof(struct RouteTree) + sizeof(struct V6route);
		l = &v6freelist;
	}

	r = *l;
	if (r != NULL) {
		*l = r->rt.mid;
	} else {
		r = kzmalloc(n, 0);
		if (r == NULL)
			panic("out of routing nodes");
	}
	memset(r, 0, n);
	r->rt.type = type;
	r->rt.ifc = NULL;
	kref_init(&r->rt.kref, route_release, 1);

	return r;
}

static void addqueue(struct route **q, struct route *r)
{
	struct route *l;

	if (r == NULL)
		return;

	l = allocroute(r->rt.type);
	l->rt.mid = *q;
	*q = l;
	l->rt.left = r;
}

/*
 *   compare 2 v6 addresses
 */
static int lcmp(uint32_t * a, uint32_t * b)
{
	int i;

	for (i = 0; i < IPllen; i++) {
		if (a[i] > b[i])
			return 1;
		if (a[i] < b[i])
			return -1;
	}
	return 0;
}

/*
 *  compare 2 v4 or v6 ranges
 */
enum {
	Rpreceeds,
	Rfollows,
	Requals,
	Rcontains,
	Rcontained,
};

static int rangecompare(struct route *a, struct route *b)
{
	if (a->rt.type & Rv4) {
		if (a->v4.endaddress < b->v4.address)
			return Rpreceeds;

		if (a->v4.address > b->v4.endaddress)
			return Rfollows;

		if (a->v4.address <= b->v4.address
			&& a->v4.endaddress >= b->v4.endaddress) {
			if (a->v4.address == b->v4.address
				&& a->v4.endaddress == b->v4.endaddress)
				return Requals;
			return Rcontains;
		}
		return Rcontained;
	}

	if (lcmp(a->v6.endaddress, b->v6.address) < 0)
		return Rpreceeds;

	if (lcmp(a->v6.address, b->v6.endaddress) > 0)
		return Rfollows;

	if (lcmp(a->v6.address, b->v6.address) <= 0
		&& lcmp(a->v6.endaddress, b->v6.endaddress) >= 0) {
		if (lcmp(a->v6.address, b->v6.address) == 0
			&& lcmp(a->v6.endaddress, b->v6.endaddress) == 0)
			return Requals;
		return Rcontains;
	}

	return Rcontained;
}

static void copygate(struct route *old, struct route *new)
{
	if (new->rt.type & Rv4)
		memmove(old->v4.gate, new->v4.gate, IPv4addrlen);
	else
		memmove(old->v6.gate, new->v6.gate, IPaddrlen);
}

/*
 *  walk down a tree adding nodes back in
 */
static void walkadd(struct Fs *f, struct route **root, struct route *p)
{
	struct route *l, *r;

	l = p->rt.left;
	r = p->rt.right;
	p->rt.left = 0;
	p->rt.right = 0;
	addnode(f, root, p);
	if (l)
		walkadd(f, root, l);
	if (r)
		walkadd(f, root, r);
}

/*
 *  calculate depth
 */
static void calcd(struct route *p)
{
	struct route *q;
	int d;

	if (p) {
		d = 0;
		q = p->rt.left;
		if (q)
			d = q->rt.depth;
		q = p->rt.right;
		if (q && q->rt.depth > d)
			d = q->rt.depth;
		q = p->rt.mid;
		if (q && q->rt.depth > d)
			d = q->rt.depth;
		p->rt.depth = d + 1;
	}
}

/*
 *  balance the tree at the current node
 */
static void balancetree(struct route **cur)
{
	struct route *p, *l, *r;
	int dl, dr;

	/*
	 * if left and right are
	 * too out of balance,
	 * rotate tree node
	 */
	p = *cur;
	dl = 0;
	if ((l = p->rt.left))
		dl = l->rt.depth;
	dr = 0;
	if ((r = p->rt.right))
		dr = r->rt.depth;

	if (dl > dr + 1) {
		p->rt.left = l->rt.right;
		l->rt.right = p;
		*cur = l;
		calcd(p);
		calcd(l);
	} else if (dr > dl + 1) {
		p->rt.right = r->rt.left;
		r->rt.left = p;
		*cur = r;
		calcd(p);
		calcd(r);
	} else
		calcd(p);
}

/*
 *  add a new node to the tree
 */
static void addnode(struct Fs *f, struct route **cur, struct route *new)
{
	struct route *p;

	p = *cur;
	if (p == 0) {
		*cur = new;
		new->rt.depth = 1;
		return;
	}

	switch (rangecompare(new, p)) {
		case Rpreceeds:
			addnode(f, &p->rt.left, new);
			break;
		case Rfollows:
			addnode(f, &p->rt.right, new);
			break;
		case Rcontains:
			/*
			 *  if new node is superset
			 *  of tree node,
			 *  replace tree node and
			 *  queue tree node to be
			 *  merged into root.
			 */
			*cur = new;
			new->rt.depth = 1;
			addqueue(&f->queue, p);
			break;
		case Requals:
			/*
			 *  supercede the old entry if the old one isn't
			 *  a local interface.
			 */
			if ((p->rt.type & Rifc) == 0) {
				p->rt.type = new->rt.type;
				p->rt.ifcid = -1;
				copygate(p, new);
			} else if (new->rt.type & Rifc)
				kref_get(&p->rt.kref, 1);
			freeroute(new);
			break;
		case Rcontained:
			addnode(f, &p->rt.mid, new);
			break;
	}

	balancetree(cur);
}

#define	V4H(a)	((a&0x07ffffff)>>(32-Lroot-5))

void
v4addroute(struct Fs *f, char *tag, uint8_t * a, uint8_t * mask,
		   uint8_t * gate, int type)
{
	struct route *p;
	uint32_t sa;
	uint32_t m;
	uint32_t ea;
	int h, eh;

	m = nhgetl(mask);
	sa = nhgetl(a) & m;
	ea = sa | ~m;

	eh = V4H(ea);
	for (h = V4H(sa); h <= eh; h++) {
		p = allocroute(Rv4 | type);
		p->v4.address = sa;
		p->v4.endaddress = ea;
		memmove(p->v4.gate, gate, sizeof(p->v4.gate));
		memmove(p->rt.tag, tag, sizeof(p->rt.tag));

		wlock(&routelock);
		addnode(f, &f->v4root[h], p);
		while ((p = f->queue)) {
			f->queue = p->rt.mid;
			walkadd(f, &f->v4root[h], p->rt.left);
			freeroute(p);
		}
		wunlock(&routelock);
	}
	v4routegeneration++;

	ipifcaddroute(f, Rv4, a, mask, gate, type);
}

#define	V6H(a)	(((a)[IPllen-1] & 0x07ffffff)>>(32-Lroot-5))
#define ISDFLT(a, mask, tag) ((ipcmp((a),v6Unspecified)==0) && (ipcmp((mask),v6Unspecified)==0) && (strcmp((tag), "ra")!=0))

void
v6addroute(struct Fs *f, char *tag, uint8_t * a, uint8_t * mask,
		   uint8_t * gate, int type)
{
	struct route *p;
	uint32_t sa[IPllen], ea[IPllen];
	uint32_t x, y;
	int h, eh;

	/*
	   if(ISDFLT(a, mask, tag))
	   f->v6p->cdrouter = -1;
	 */

	for (h = 0; h < IPllen; h++) {
		x = nhgetl(a + 4 * h);
		y = nhgetl(mask + 4 * h);
		sa[h] = x & y;
		ea[h] = x | ~y;
	}

	eh = V6H(ea);
	for (h = V6H(sa); h <= eh; h++) {
		p = allocroute(type);
		memmove(p->v6.address, sa, IPaddrlen);
		memmove(p->v6.endaddress, ea, IPaddrlen);
		memmove(p->v6.gate, gate, IPaddrlen);
		memmove(p->rt.tag, tag, sizeof(p->rt.tag));

		wlock(&routelock);
		addnode(f, &f->v6root[h], p);
		while ((p = f->queue)) {
			f->queue = p->rt.mid;
			walkadd(f, &f->v6root[h], p->rt.left);
			freeroute(p);
		}
		wunlock(&routelock);
	}
	v6routegeneration++;

	ipifcaddroute(f, 0, a, mask, gate, type);
}

struct route **looknode(struct route **cur, struct route *r)
{
	struct route *p;

	for (;;) {
		p = *cur;
		if (p == 0)
			return 0;

		switch (rangecompare(r, p)) {
			case Rcontains:
				return 0;
			case Rpreceeds:
				cur = &p->rt.left;
				break;
			case Rfollows:
				cur = &p->rt.right;
				break;
			case Rcontained:
				cur = &p->rt.mid;
				break;
			case Requals:
				return cur;
		}
	}
}

void v4delroute(struct Fs *f, uint8_t * a, uint8_t * mask, int dolock)
{
	struct route **r, *p;
	struct route rt;
	int h, eh;
	uint32_t m;

	m = nhgetl(mask);
	rt.v4.address = nhgetl(a) & m;
	rt.v4.endaddress = rt.v4.address | ~m;
	rt.rt.type = Rv4;

	eh = V4H(rt.v4.endaddress);
	for (h = V4H(rt.v4.address); h <= eh; h++) {
		if (dolock)
			wlock(&routelock);
		r = looknode(&f->v4root[h], &rt);
		if (r) {
			p = *r;
			/* TODO: bad usage of kref (maybe use a release).  I didn't change
			 * this one, since it looks like the if code is when we want to
			 * release.  btw, use better code reuse btw v4 and v6... */
			if (kref_put(&p->rt.kref)) {
				*r = 0;
				addqueue(&f->queue, p->rt.left);
				addqueue(&f->queue, p->rt.mid);
				addqueue(&f->queue, p->rt.right);
				freeroute(p);
				while ((p = f->queue)) {
					f->queue = p->rt.mid;
					walkadd(f, &f->v4root[h], p->rt.left);
					freeroute(p);
				}
			}
		}
		if (dolock)
			wunlock(&routelock);
	}
	v4routegeneration++;

	ipifcremroute(f, Rv4, a, mask);
}

void v6delroute(struct Fs *f, uint8_t * a, uint8_t * mask, int dolock)
{
	struct route **r, *p;
	struct route rt;
	int h, eh;
	uint32_t x, y;

	for (h = 0; h < IPllen; h++) {
		x = nhgetl(a + 4 * h);
		y = nhgetl(mask + 4 * h);
		rt.v6.address[h] = x & y;
		rt.v6.endaddress[h] = x | ~y;
	}
	rt.rt.type = 0;

	eh = V6H(rt.v6.endaddress);
	for (h = V6H(rt.v6.address); h <= eh; h++) {
		if (dolock)
			wlock(&routelock);
		r = looknode(&f->v6root[h], &rt);
		if (r) {
			p = *r;
			/* TODO: bad usage of kref (maybe use a release).  I didn't change
			 * this one, since it looks like the if code is when we want to
			 * release.  btw, use better code reuse btw v4 and v6... */
			if (kref_put(&p->rt.kref)) {
				*r = 0;
				addqueue(&f->queue, p->rt.left);
				addqueue(&f->queue, p->rt.mid);
				addqueue(&f->queue, p->rt.right);
				freeroute(p);
				while ((p = f->queue)) {
					f->queue = p->rt.mid;
					walkadd(f, &f->v6root[h], p->rt.left);
					freeroute(p);
				}
			}
		}
		if (dolock)
			wunlock(&routelock);
	}
	v6routegeneration++;

	ipifcremroute(f, 0, a, mask);
}

struct route *v4lookup(struct Fs *f, uint8_t * a, struct conv *c)
{
	struct route *p, *q;
	uint32_t la;
	uint8_t gate[IPaddrlen];
	struct Ipifc *ifc;

	if (c != NULL && c->r != NULL && c->r->rt.ifc != NULL
		&& c->rgen == v4routegeneration)
		return c->r;

	la = nhgetl(a);
	q = NULL;
	for (p = f->v4root[V4H(la)]; p;)
		if (la >= p->v4.address) {
			if (la <= p->v4.endaddress) {
				q = p;
				p = p->rt.mid;
			} else
				p = p->rt.right;
		} else
			p = p->rt.left;

	if (q && (q->rt.ifc == NULL || q->rt.ifcid != q->rt.ifc->ifcid)) {
		if (q->rt.type & Rifc) {
			hnputl(gate + IPv4off, q->v4.address);
			memmove(gate, v4prefix, IPv4off);
		} else
			v4tov6(gate, q->v4.gate);
		ifc = findipifc(f, gate, q->rt.type);
		if (ifc == NULL)
			return NULL;
		q->rt.ifc = ifc;
		q->rt.ifcid = ifc->ifcid;
	}

	if (c != NULL) {
		c->r = q;
		c->rgen = v4routegeneration;
	}

	return q;
}

struct route *v6lookup(struct Fs *f, uint8_t * a, struct conv *c)
{
	struct route *p, *q;
	uint32_t la[IPllen];
	int h;
	uint32_t x, y;
	uint8_t gate[IPaddrlen];
	struct Ipifc *ifc;

	if (memcmp(a, v4prefix, IPv4off) == 0) {
		q = v4lookup(f, a + IPv4off, c);
		if (q != NULL)
			return q;
	}

	if (c != NULL && c->r != NULL && c->r->rt.ifc != NULL
		&& c->rgen == v6routegeneration)
		return c->r;

	for (h = 0; h < IPllen; h++)
		la[h] = nhgetl(a + 4 * h);

	q = 0;
	for (p = f->v6root[V6H(la)]; p;) {
		for (h = 0; h < IPllen; h++) {
			x = la[h];
			y = p->v6.address[h];
			if (x == y)
				continue;
			if (x < y) {
				p = p->rt.left;
				goto next;
			}
			break;
		}
		for (h = 0; h < IPllen; h++) {
			x = la[h];
			y = p->v6.endaddress[h];
			if (x == y)
				continue;
			if (x > y) {
				p = p->rt.right;
				goto next;
			}
			break;
		}
		q = p;
		p = p->rt.mid;
next:	;
	}

	if (q && (q->rt.ifc == NULL || q->rt.ifcid != q->rt.ifc->ifcid)) {
		if (q->rt.type & Rifc) {
			for (h = 0; h < IPllen; h++)
				hnputl(gate + 4 * h, q->v6.address[h]);
			ifc = findipifc(f, gate, q->rt.type);
		} else
			ifc = findipifc(f, q->v6.gate, q->rt.type);
		if (ifc == NULL)
			return NULL;
		q->rt.ifc = ifc;
		q->rt.ifcid = ifc->ifcid;
	}
	if (c != NULL) {
		c->r = q;
		c->rgen = v6routegeneration;
	}

	return q;
}

void routetype(int type, char *p)
{
	memset(p, ' ', 4);
	p[4] = 0;
	if (type & Rv4)
		*p++ = '4';
	else
		*p++ = '6';
	if (type & Rifc)
		*p++ = 'i';
	if (type & Runi)
		*p++ = 'u';
	else if (type & Rbcast)
		*p++ = 'b';
	else if (type & Rmulti)
		*p++ = 'm';
	if (type & Rptpt)
		*p = 'p';
}

char *rformat = "%-15I %-4M %-15I %4.4s %4.4s %3s\n";

void
convroute(struct route *r, uint8_t * addr, uint8_t * mask,
		  uint8_t * gate, char *t, int *nifc)
{
	int i;

	if (r->rt.type & Rv4) {
		memmove(addr, v4prefix, IPv4off);
		hnputl(addr + IPv4off, r->v4.address);
		memset(mask, 0xff, IPv4off);
		hnputl(mask + IPv4off, ~(r->v4.endaddress ^ r->v4.address));
		memmove(gate, v4prefix, IPv4off);
		memmove(gate + IPv4off, r->v4.gate, IPv4addrlen);
	} else {
		for (i = 0; i < IPllen; i++) {
			hnputl(addr + 4 * i, r->v6.address[i]);
			hnputl(mask + 4 * i, ~(r->v6.endaddress[i] ^ r->v6.address[i]));
		}
		memmove(gate, r->v6.gate, IPaddrlen);
	}

	routetype(r->rt.type, t);

	if (r->rt.ifc)
		*nifc = r->rt.ifc->conv->x;
	else
		*nifc = -1;
}

/*
 *  this code is not in rr to reduce stack size
 */
static void sprintroute(struct route *r, struct routewalk *rw)
{
	int nifc, n;
	char t[5], *iname, ifbuf[5];
	uint8_t addr[IPaddrlen], mask[IPaddrlen], gate[IPaddrlen];
	char *p;

	convroute(r, addr, mask, gate, t, &nifc);
	iname = "-";
	if (nifc != -1) {
		iname = ifbuf;
		snprintf(ifbuf, sizeof ifbuf, "%d", nifc);
	}
	p = seprintf(rw->p, rw->e, rformat, addr, mask, gate, t, r->rt.tag, iname);
	if (rw->o < 0) {
		n = p - rw->p;
		if (n > -rw->o) {
			memmove(rw->p, rw->p - rw->o, n + rw->o);
			rw->p = p + rw->o;
		}
		rw->o += n;
	} else
		rw->p = p;
}

/*
 *  recurse descending tree, applying the function in Routewalk
 */
static int rr(struct route *r, struct routewalk *rw)
{
	int h;

	if (rw->e <= rw->p)
		return 0;
	if (r == NULL)
		return 1;

	if (rr(r->rt.left, rw) == 0)
		return 0;

	if (r->rt.type & Rv4)
		h = V4H(r->v4.address);
	else
		h = V6H(r->v6.address);

	if (h == rw->h)
		rw->walk(r, rw);

	if (rr(r->rt.mid, rw) == 0)
		return 0;

	return rr(r->rt.right, rw);
}

void ipwalkroutes(struct Fs *f, struct routewalk *rw)
{
	rlock(&routelock);
	if (rw->e > rw->p) {
		for (rw->h = 0; rw->h < ARRAY_SIZE(f->v4root); rw->h++)
			if (rr(f->v4root[rw->h], rw) == 0)
				break;
	}
	if (rw->e > rw->p) {
		for (rw->h = 0; rw->h < ARRAY_SIZE(f->v6root); rw->h++)
			if (rr(f->v6root[rw->h], rw) == 0)
				break;
	}
	runlock(&routelock);
}

long routeread(struct Fs *f, char *p, uint32_t offset, int n)
{
	struct routewalk rw;

	rw.p = p;
	rw.e = p + n;
	rw.o = -offset;
	rw.walk = sprintroute;

	ipwalkroutes(f, &rw);

	return rw.p - p;
}

/*
 *  this code is not in routeflush to reduce stack size
 */
void delroute(struct Fs *f, struct route *r, int dolock)
{
	uint8_t addr[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t gate[IPaddrlen];
	char t[5];
	int nifc;

	convroute(r, addr, mask, gate, t, &nifc);
	if (r->rt.type & Rv4)
		v4delroute(f, addr + IPv4off, mask + IPv4off, dolock);
	else
		v6delroute(f, addr, mask, dolock);
}

/*
 *  recurse until one route is deleted
 *    returns 0 if nothing is deleted, 1 otherwise
 */
int routeflush(struct Fs *f, struct route *r, char *tag)
{
	if (r == NULL)
		return 0;
	if (routeflush(f, r->rt.mid, tag))
		return 1;
	if (routeflush(f, r->rt.left, tag))
		return 1;
	if (routeflush(f, r->rt.right, tag))
		return 1;
	if ((r->rt.type & Rifc) == 0) {
		if (tag == NULL || strncmp(tag, r->rt.tag, sizeof(r->rt.tag)) == 0) {
			delroute(f, r, 0);
			return 1;
		}
	}
	return 0;
}

long routewrite(struct Fs *f, struct chan *c, char *p, int n)
{
	ERRSTACK(1);
	int h, changed;
	char *tag;
	struct cmdbuf *cb;
	uint8_t addr[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t gate[IPaddrlen];
	struct IPaux *a, *na;

	cb = parsecmd(p, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	if (cb->nf < 1)
		error(EINVAL, "short control request");

	if (strcmp(cb->f[0], "flush") == 0) {
		tag = cb->f[1];
		for (h = 0; h < ARRAY_SIZE(f->v4root); h++)
			for (changed = 1; changed;) {
				wlock(&routelock);
				changed = routeflush(f, f->v4root[h], tag);
				wunlock(&routelock);
			}
		for (h = 0; h < ARRAY_SIZE(f->v6root); h++)
			for (changed = 1; changed;) {
				wlock(&routelock);
				changed = routeflush(f, f->v6root[h], tag);
				wunlock(&routelock);
			}
	} else if (strcmp(cb->f[0], "remove") == 0) {
		if (cb->nf < 3)
			error(EINVAL, ERROR_FIXME);
		parseip(addr, cb->f[1]);
		parseipmask(mask, cb->f[2]);
		if (memcmp(addr, v4prefix, IPv4off) == 0)
			v4delroute(f, addr + IPv4off, mask + IPv4off, 1);
		else
			v6delroute(f, addr, mask, 1);
	} else if (strcmp(cb->f[0], "add") == 0) {
		if (cb->nf < 4)
			error(EINVAL, ERROR_FIXME);
		parseip(addr, cb->f[1]);
		parseipmask(mask, cb->f[2]);
		parseip(gate, cb->f[3]);
		tag = "none";
		if (c != NULL) {
			a = c->aux;
			tag = a->tag;
		}
		if (memcmp(addr, v4prefix, IPv4off) == 0)
			v4addroute(f, tag, addr + IPv4off, mask + IPv4off, gate + IPv4off,
					   0);
		else
			v6addroute(f, tag, addr, mask, gate, 0);
	} else if (strcmp(cb->f[0], "tag") == 0) {
		if (cb->nf < 2)
			error(EINVAL, ERROR_FIXME);

		a = c->aux;
		na = newipaux(a->owner, cb->f[1]);
		c->aux = na;
		kfree(a);
	}

	poperror();
	kfree(cb);
	return n;
}
