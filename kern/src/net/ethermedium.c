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
#include <ip.h>

typedef struct Etherhdr Etherhdr;
struct Etherhdr {
	uint8_t d[6];
	uint8_t s[6];
	uint8_t t[2];
};

static uint8_t ipbroadcast[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
};

static uint8_t etherbroadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void etherread4(void *a);
static void etherread6(void *a);
static void etherbind(struct Ipifc *ifc, int argc, char **argv);
static void etherunbind(struct Ipifc *ifc);
static void etherbwrite(struct Ipifc *ifc, struct block *bp, int version,
						uint8_t * ip);
static void etheraddmulti(struct Ipifc *ifc, uint8_t * a, uint8_t * ia);
static void etherremmulti(struct Ipifc *ifc, uint8_t * a, uint8_t * ia);
static struct block *multicastarp(struct Fs *f, struct arpent *a,
								  struct medium *, uint8_t * mac);
static void sendarp(struct Ipifc *ifc, struct arpent *a);
static void sendgarp(struct Ipifc *ifc, uint8_t * unused_uint8_p_t);
static int multicastea(uint8_t * ea, uint8_t * ip);
static void recvarpproc(void *);
static void resolveaddr6(struct Ipifc *ifc, struct arpent *a);
static void etherpref2addr(uint8_t * pref, uint8_t * ea);

struct medium ethermedium = {
	.name = "ether",
	.hsize = 14,
	.mintu = 60,
	.maxtu = 1514,
	.maclen = 6,
	.bind = etherbind,
	.unbind = etherunbind,
	.bwrite = etherbwrite,
	.addmulti = etheraddmulti,
	.remmulti = etherremmulti,
	.ares = arpenter,
	.areg = sendgarp,
	.pref2addr = etherpref2addr,
};

struct medium trexmedium = {
	.name = "trex",
	.hsize = 14,
	.mintu = 60,
	.maxtu = 1514,
	.maclen = 6,
	.bind = etherbind,
	.unbind = etherunbind,
	.bwrite = etherbwrite,
	.addmulti = etheraddmulti,
	.remmulti = etherremmulti,
	.ares = arpenter,
	.areg = sendgarp,
	.pref2addr = etherpref2addr,
};

typedef struct Etherrock Etherrock;
struct Etherrock {
	struct Fs *f;				/* file system we belong to */
	struct proc *arpp;			/* arp process */
	struct proc *read4p;		/* reading process (v4) */
	struct proc *read6p;		/* reading process (v6) */
	struct chan *mchan4;		/* Data channel for v4 */
	struct chan *achan;			/* Arp channel */
	struct chan *cchan4;		/* Control channel for v4 */
	struct chan *mchan6;		/* Data channel for v6 */
	struct chan *cchan6;		/* Control channel for v6 */
};

/*
 *  ethernet arp request
 */
enum {
	ETARP = 0x0806,
	ETIP4 = 0x0800,
	ETIP6 = 0x86DD,
	ARPREQUEST = 1,
	ARPREPLY = 2,
};

typedef struct Etherarp Etherarp;
struct Etherarp {
	uint8_t d[6];
	uint8_t s[6];
	uint8_t type[2];
	uint8_t hrd[2];
	uint8_t pro[2];
	uint8_t hln;
	uint8_t pln;
	uint8_t op[2];
	uint8_t sha[6];
	uint8_t spa[4];
	uint8_t tha[6];
	uint8_t tpa[4];
};

static char *nbmsg = "nonblocking";

static unsigned int parsefeat(char *ptr)
{
	unsigned int feat = 0;

	if (strstr(ptr, "ipck"))
		feat |= NETF_IPCK;
	if (strstr(ptr, "udpck"))
		feat |= NETF_UDPCK;
	if (strstr(ptr, "tcpck"))
		feat |= NETF_TCPCK;
	if (strstr(ptr, "padmin"))
		feat |= NETF_PADMIN;
	if (strstr(ptr, "sg"))
		feat |= NETF_SG;
	if (strstr(ptr, "tso"))
		feat |= NETF_TSO;
	return feat;
}

/*
 *  called to bind an IP ifc to an ethernet device
 *  called with ifc wlock'd
 */
static void etherbind(struct Ipifc *ifc, int argc, char **argv)
{
	ERRSTACK(1);
	struct chan *mchan4, *cchan4, *achan, *mchan6, *cchan6;
	char *addr, *dir, *buf;
	int fd, cfd, n;
	char *ptr;
	Etherrock *er;

	if (argc < 2)
		error(EINVAL, ERROR_FIXME);

	addr = kmalloc(Maxpath, MEM_WAIT);	//char addr[2*KNAMELEN];
	dir = kmalloc(Maxpath, MEM_WAIT);	//char addr[2*KNAMELEN];
	mchan4 = cchan4 = achan = mchan6 = cchan6 = NULL;
	buf = NULL;
	if (waserror()) {
		if (mchan4 != NULL)
			cclose(mchan4);
		if (cchan4 != NULL)
			cclose(cchan4);
		if (achan != NULL)
			cclose(achan);
		if (mchan6 != NULL)
			cclose(mchan6);
		if (cchan6 != NULL)
			cclose(cchan6);
		if (buf != NULL)
			kfree(buf);
		kfree(addr);
		kfree(dir);
		nexterror();
	}

	/*
	 *  open ip converstation
	 *
	 *  the dial will fail if the type is already open on
	 *  this device.
	 */
	snprintf(addr, Maxpath, "%s!0x800", argv[2]);
	fd = kdial(addr, NULL, dir, &cfd);
	if (fd < 0)
		error(EFAIL, "dial 0x800 failed: %s", get_cur_errbuf());
	mchan4 = commonfdtochan(fd, O_RDWR, 0, 1);
	cchan4 = commonfdtochan(cfd, O_RDWR, 0, 1);
	sysclose(fd);
	sysclose(cfd);

	/*
	 *  make it non-blocking
	 */
	devtab[cchan4->type].write(cchan4, nbmsg, strlen(nbmsg), 0);

	/*
	 *  get mac address and speed
	 */
	snprintf(addr, Maxpath, "%s/stats", dir);
	fd = sysopen(addr, O_READ);
	if (fd < 0)
		error(EFAIL, "can't open ether stats: %s", get_cur_errbuf());

	buf = kzmalloc(512, 0);
	n = sysread(fd, buf, 511);
	sysclose(fd);
	if (n <= 0)
		error(EIO, ERROR_FIXME);
	buf[n] = 0;

	ptr = strstr(buf, "addr: ");
	if (!ptr)
		error(EIO, ERROR_FIXME);
	ptr += 6;
	parsemac(ifc->mac, ptr, 6);

	ptr = strstr(buf, "mbps: ");
	if (ptr) {
		ptr += 6;
		ifc->mbps = atoi(ptr);
	} else
		ifc->mbps = 100;


	ptr = strstr(buf, "feat: ");
	if (ptr) {
		ptr += 6;
		ifc->feat = parsefeat(ptr);
	} else {
		ifc->feat = 0;
	}
	/*
	 *  open arp conversation
	 */
	snprintf(addr, Maxpath, "%s!0x806", argv[2]);
	fd = kdial(addr, NULL, NULL, NULL);
	if (fd < 0)
		error(EFAIL, "dial 0x806 failed: %s", get_cur_errbuf());
	achan = commonfdtochan(fd, O_RDWR, 0, 1);
	sysclose(fd);

	/*
	 *  open ip conversation
	 *
	 *  the dial will fail if the type is already open on
	 *  this device.
	 */
	snprintf(addr, Maxpath, "%s!0x86DD", argv[2]);
	fd = kdial(addr, NULL, dir, &cfd);
	if (fd < 0)
		error(EFAIL, "dial 0x86DD failed: %s", get_cur_errbuf());
	mchan6 = commonfdtochan(fd, O_RDWR, 0, 1);
	cchan6 = commonfdtochan(cfd, O_RDWR, 0, 1);
	sysclose(fd);
	sysclose(cfd);

	/*
	 *  make it non-blocking
	 */
	devtab[cchan6->type].write(cchan6, nbmsg, strlen(nbmsg), 0);

	er = kzmalloc(sizeof(*er), 0);
	er->mchan4 = mchan4;
	er->cchan4 = cchan4;
	er->achan = achan;
	er->mchan6 = mchan6;
	er->cchan6 = cchan6;
	er->f = ifc->conv->p->f;
	ifc->arg = er;

	kfree(buf);
	kfree(addr);
	kfree(dir);
	poperror();

	ktask("etherread4", etherread4, ifc);
	ktask("recvarpproc", recvarpproc, ifc);
	ktask("etherread6", etherread6, ifc);
}

/*
 *  called with ifc wlock'd
 */
static void etherunbind(struct Ipifc *ifc)
{
	Etherrock *er = ifc->arg;
	printk("[kernel] etherunbind not supported yet!\n");

	// we'll need to tell the ktasks to exit, maybe via flags and a wakeup
#if 0
	if (er->read4p)
		postnote(er->read4p, 1, "unbind", 0);
	if (er->read6p)
		postnote(er->read6p, 1, "unbind", 0);
	if (er->arpp)
		postnote(er->arpp, 1, "unbind", 0);
#endif

	/* wait for readers to die */
	while (er->arpp != 0 || er->read4p != 0 || er->read6p != 0)
		cpu_relax();
	kthread_usleep(300 * 1000);

	if (er->mchan4 != NULL)
		cclose(er->mchan4);
	if (er->achan != NULL)
		cclose(er->achan);
	if (er->cchan4 != NULL)
		cclose(er->cchan4);
	if (er->mchan6 != NULL)
		cclose(er->mchan6);
	if (er->cchan6 != NULL)
		cclose(er->cchan6);

	kfree(er);
}

/*
 * copy ethernet address
 */
static inline void etherfilladdr(uint16_t *pkt, uint16_t *dst, uint16_t *src)
{
	*pkt++ = *dst++;
	*pkt++ = *dst++;
	*pkt++ = *dst++;
	*pkt++ = *src++;
	*pkt++ = *src++;
	*pkt = *src;
}

/*
 *  called by ipoput with a single block to write with ifc rlock'd
 */
static void
etherbwrite(struct Ipifc *ifc, struct block *bp, int version, uint8_t * ip)
{
	Etherhdr *eh;
	struct arpent *a;
	uint8_t mac[6];
	Etherrock *er = ifc->arg;

	ipifc_trace_block(ifc, bp);
	/* get mac address of destination.
	 *
	 * Locking is tricky here.  If we get arpent 'a' back, the f->arp is
	 * qlocked.  if multicastarp returns bp, then it unlocked it for us.  if
	 * not, sendarp or resolveaddr6 unlocked it for us.  yikes. */
	a = arpget(er->f->arp, bp, version, ifc, ip, mac);
	if (a) {
		/* check for broadcast or multicast.  if it is either, this sorts that
		 * out and returns the bp for the first packet on the arp's hold list.*/
		bp = multicastarp(er->f, a, ifc->m, mac);
		if (bp == NULL) {
			switch (version) {
				case V4:
					sendarp(ifc, a);
					break;
				case V6:
					resolveaddr6(ifc, a);
					break;
				default:
					panic("etherbwrite: version %d", version);
			}
			return;
		}
	}

	/* make it a single block with space for the ether header */
	bp = padblock(bp, ifc->m->hsize);
	if (bp->next)
		bp = concatblock(bp);
	eh = (Etherhdr *) bp->rp;

	/* copy in mac addresses and ether type */
	etherfilladdr((uint16_t *)bp->rp, (uint16_t *)mac, (uint16_t *)ifc->mac);

	switch (version) {
		case V4:
			eh->t[0] = 0x08;
			eh->t[1] = 0x00;
			devtab[er->mchan4->type].bwrite(er->mchan4, bp, 0);
			break;
		case V6:
			eh->t[0] = 0x86;
			eh->t[1] = 0xDD;
			devtab[er->mchan6->type].bwrite(er->mchan6, bp, 0);
			break;
		default:
			panic("etherbwrite2: version %d", version);
	}
	ifc->out++;
}

/*
 *  process to read from the ethernet
 */
static void etherread4(void *a)
{
	ERRSTACK(2);
	struct Ipifc *ifc;
	struct block *bp;
	Etherrock *er;

	ifc = a;
	er = ifc->arg;
	er->read4p = current;	/* hide identity under a rock for unbind */
	if (waserror()) {
		er->read4p = 0;
		poperror();
		warn("etherread4 returns, probably unexpectedly\n");
		return;
	}
	for (;;) {
		bp = devtab[er->mchan4->type].bread(er->mchan4, 128 * 1024, 0);
		if (!canrlock(&ifc->rwlock)) {
			freeb(bp);
			continue;
		}
		if (waserror()) {
			runlock(&ifc->rwlock);
			nexterror();
		}
		ifc->in++;
		bp->rp += ifc->m->hsize;
		if (ifc->lifc == NULL) {
			freeb(bp);
		} else {
			ipifc_trace_block(ifc, bp);
			ipiput4(er->f, ifc, bp);
		}
		runlock(&ifc->rwlock);
		poperror();
	}
	poperror();
}

/*
 *  process to read from the ethernet, IPv6
 */
static void etherread6(void *a)
{
	ERRSTACK(2);
	struct Ipifc *ifc;
	struct block *bp;
	Etherrock *er;

	ifc = a;
	er = ifc->arg;
	er->read6p = current;	/* hide identity under a rock for unbind */
	if (waserror()) {
		er->read6p = 0;
		warn("etherread6 returns, probably unexpectedly\n");
		poperror();
		return;
	}
	for (;;) {
		bp = devtab[er->mchan6->type].bread(er->mchan6, ifc->maxtu, 0);
		if (!canrlock(&ifc->rwlock)) {
			freeb(bp);
			continue;
		}
		if (waserror()) {
			runlock(&ifc->rwlock);
			nexterror();
		}
		ifc->in++;
		bp->rp += ifc->m->hsize;
		if (ifc->lifc == NULL) {
			freeb(bp);
		} else {
			ipifc_trace_block(ifc, bp);
			ipiput6(er->f, ifc, bp);
		}
		runlock(&ifc->rwlock);
		poperror();
	}
	poperror();
}

static void etheraddmulti(struct Ipifc *ifc, uint8_t * a, uint8_t * unused)
{
	uint8_t mac[6];
	char buf[64];
	Etherrock *er = ifc->arg;
	int version;

	version = multicastea(mac, a);
	snprintf(buf, sizeof(buf), "addmulti %E", mac);
	switch (version) {
		case V4:
			devtab[er->cchan4->type].write(er->cchan4, buf, strlen(buf), 0);
			break;
		case V6:
			devtab[er->cchan6->type].write(er->cchan6, buf, strlen(buf), 0);
			break;
		default:
			panic("etheraddmulti: version %d", version);
	}
}

static void etherremmulti(struct Ipifc *ifc, uint8_t * a, uint8_t * unused)
{
	uint8_t mac[6];
	char buf[64];
	Etherrock *er = ifc->arg;
	int version;

	version = multicastea(mac, a);
	snprintf(buf, sizeof(buf), "remmulti %E", mac);
	switch (version) {
		case V4:
			devtab[er->cchan4->type].write(er->cchan4, buf, strlen(buf), 0);
			break;
		case V6:
			devtab[er->cchan6->type].write(er->cchan6, buf, strlen(buf), 0);
			break;
		default:
			panic("etherremmulti: version %d", version);
	}
}

/*
 *  send an ethernet arp
 *  (only v4, v6 uses the neighbor discovery, rfc1970)
 *
 * May drop packets on stale arps. */
static void sendarp(struct Ipifc *ifc, struct arpent *a)
{
	int n;
	struct block *bp;
	Etherarp *e;
	Etherrock *er = ifc->arg;

	/* don't do anything if it's been less than a second since the last.  ctime
	 * is set to 0 for the first time through.  we hold the f->arp qlock, so
	 * there shouldn't be a problem with another arp request for this same
	 * arpent coming down til we update ctime again. */
	if (NOW - a->ctime < 1000) {
		arprelease(er->f->arp, a);
		return;
	}

	/* remove all but the last message.  brho: this might be unnecessary.  we'll
	 * eventually send them.  but they should be quite stale at this point. */
	while ((bp = a->hold) != NULL) {
		if (bp == a->last)
			break;
		a->hold = bp->list;
		freeblist(bp);
	}

	/* update last sent time */
	a->ctime = NOW;
	arprelease(er->f->arp, a);

	n = sizeof(Etherarp);
	if (n < a->type->mintu)
		n = a->type->mintu;
	bp = block_alloc(n, MEM_WAIT);
	memset(bp->rp, 0, n);
	e = (Etherarp *) bp->rp;
	memmove(e->tpa, a->ip + IPv4off, sizeof(e->tpa));
	ipv4local(ifc, e->spa);
	memmove(e->sha, ifc->mac, sizeof(e->sha));
	memset(e->d, 0xff, sizeof(e->d));	/* ethernet broadcast */
	memmove(e->s, ifc->mac, sizeof(e->s));

	hnputs(e->type, ETARP);
	hnputs(e->hrd, 1);
	hnputs(e->pro, ETIP4);
	e->hln = sizeof(e->sha);
	e->pln = sizeof(e->spa);
	hnputs(e->op, ARPREQUEST);
	bp->wp += n;

	n = devtab[er->achan->type].bwrite(er->achan, bp, 0);
	if (n < 0)
		printd("arp: send: %r\n");
}

static void resolveaddr6(struct Ipifc *ifc, struct arpent *a)
{
	int sflag;
	struct block *bp;
	Etherrock *er = ifc->arg;
	uint8_t ipsrc[IPaddrlen];

	/* don't do anything if it's been less than a second since the last */
	if (NOW - a->ctime < ReTransTimer) {
		arprelease(er->f->arp, a);
		return;
	}

	/* remove all but the last message */
	while ((bp = a->hold) != NULL) {
		if (bp == a->last)
			break;
		a->hold = bp->list;
		freeblist(bp);
	}

	/* try to keep it around for a second more */
	a->ctime = NOW;
	a->rtime = NOW + ReTransTimer;
	if (a->rxtsrem <= 0) {
		arprelease(er->f->arp, a);
		return;
	}

	a->rxtsrem--;
	arprelease(er->f->arp, a);

	if ((sflag = ipv6anylocal(ifc, ipsrc)))
		icmpns(er->f, ipsrc, sflag, a->ip, TARG_MULTI, ifc->mac);
}

/*
 *  send a gratuitous arp to refresh arp caches
 */
static void sendgarp(struct Ipifc *ifc, uint8_t * ip)
{
	int n;
	struct block *bp;
	Etherarp *e;
	Etherrock *er = ifc->arg;

	/* don't arp for our initial non address */
	if (ipcmp(ip, IPnoaddr) == 0)
		return;

	n = sizeof(Etherarp);
	if (n < ifc->m->mintu)
		n = ifc->m->mintu;
	bp = block_alloc(n, MEM_WAIT);
	memset(bp->rp, 0, n);
	e = (Etherarp *) bp->rp;
	memmove(e->tpa, ip + IPv4off, sizeof(e->tpa));
	memmove(e->spa, ip + IPv4off, sizeof(e->spa));
	memmove(e->sha, ifc->mac, sizeof(e->sha));
	memset(e->d, 0xff, sizeof(e->d));	/* ethernet broadcast */
	memmove(e->s, ifc->mac, sizeof(e->s));

	hnputs(e->type, ETARP);
	hnputs(e->hrd, 1);
	hnputs(e->pro, ETIP4);
	e->hln = sizeof(e->sha);
	e->pln = sizeof(e->spa);
	hnputs(e->op, ARPREQUEST);
	bp->wp += n;

	n = devtab[er->achan->type].bwrite(er->achan, bp, 0);
	if (n < 0)
		printd("garp: send: %r\n");
}

static void recvarp(struct Ipifc *ifc)
{
	int n;
	struct block *ebp, *rbp;
	Etherarp *e, *r;
	uint8_t ip[IPaddrlen];
	static uint8_t eprinted[4];
	Etherrock *er = ifc->arg;

	ebp = devtab[er->achan->type].bread(er->achan, ifc->maxtu, 0);
	if (ebp == NULL) {
		printd("arp: rcv: %r\n");
		return;
	}

	e = (Etherarp *) ebp->rp;
	switch (nhgets(e->op)) {
		default:
			break;

		case ARPREPLY:
			/* check for machine using my ip address */
			v4tov6(ip, e->spa);
			if (iplocalonifc(ifc, ip) || ipproxyifc(er->f, ifc, ip)) {
				if (memcmp(e->sha, ifc->mac, sizeof(e->sha)) != 0) {
					printd("arprep: 0x%E/0x%E also has ip addr %V\n",
						   e->s, e->sha, e->spa);
					break;
				}
			}

			/* make sure we're not entering broadcast addresses */
			if (ipcmp(ip, ipbroadcast) == 0 ||
				!memcmp(e->sha, etherbroadcast, sizeof(e->sha))) {
				printd
					("arprep: 0x%E/0x%E cannot register broadcast address %I\n",
					 e->s, e->sha, e->spa);
				break;
			}

			arpenter(er->f, V4, e->spa, e->sha, sizeof(e->sha), 0);
			break;

		case ARPREQUEST:
			/* don't answer arps till we know who we are */
			if (ifc->lifc == 0)
				break;

			/* check for machine using my ip or ether address */
			v4tov6(ip, e->spa);
			if (iplocalonifc(ifc, ip) || ipproxyifc(er->f, ifc, ip)) {
				if (memcmp(e->sha, ifc->mac, sizeof(e->sha)) != 0) {
					if (memcmp(eprinted, e->spa, sizeof(e->spa))) {
						/* print only once */
						printd("arpreq: 0x%E also has ip addr %V\n", e->sha,
							   e->spa);
						memmove(eprinted, e->spa, sizeof(e->spa));
					}
				}
			} else {
				if (memcmp(e->sha, ifc->mac, sizeof(e->sha)) == 0) {
					printd("arpreq: %V also has ether addr %E\n", e->spa,
						   e->sha);
					break;
				}
			}

			/* refresh what we know about sender */
			arpenter(er->f, V4, e->spa, e->sha, sizeof(e->sha), 1);

			/* answer only requests for our address or systems we're proxying for */
			v4tov6(ip, e->tpa);
			if (!iplocalonifc(ifc, ip))
				if (!ipproxyifc(er->f, ifc, ip))
					break;

			n = sizeof(Etherarp);
			if (n < ifc->mintu)
				n = ifc->mintu;
			rbp = block_alloc(n, MEM_WAIT);
			r = (Etherarp *) rbp->rp;
			memset(r, 0, sizeof(Etherarp));
			hnputs(r->type, ETARP);
			hnputs(r->hrd, 1);
			hnputs(r->pro, ETIP4);
			r->hln = sizeof(r->sha);
			r->pln = sizeof(r->spa);
			hnputs(r->op, ARPREPLY);
			memmove(r->tha, e->sha, sizeof(r->tha));
			memmove(r->tpa, e->spa, sizeof(r->tpa));
			memmove(r->sha, ifc->mac, sizeof(r->sha));
			memmove(r->spa, e->tpa, sizeof(r->spa));
			memmove(r->d, e->sha, sizeof(r->d));
			memmove(r->s, ifc->mac, sizeof(r->s));
			rbp->wp += n;

			n = devtab[er->achan->type].bwrite(er->achan, rbp, 0);
			if (n < 0)
				printd("arp: write: %r\n");
	}
	freeb(ebp);
}

static void recvarpproc(void *v)
{
	ERRSTACK(1);
	struct Ipifc *ifc = v;
	Etherrock *er = ifc->arg;

	er->arpp = current;
	if (waserror()) {
		er->arpp = 0;
		warn("recvarpproc returns, probably unexpectedly\n");
		poperror();
		return;
	}
	for (;;)
		recvarp(ifc);
	poperror();
}

static int multicastea(uint8_t * ea, uint8_t * ip)
{
	int x;

	switch (x = ipismulticast(ip)) {
		case V4:
			ea[0] = 0x01;
			ea[1] = 0x00;
			ea[2] = 0x5e;
			ea[3] = ip[13] & 0x7f;
			ea[4] = ip[14];
			ea[5] = ip[15];
			break;
		case V6:
			ea[0] = 0x33;
			ea[1] = 0x33;
			ea[2] = ip[12];
			ea[3] = ip[13];
			ea[4] = ip[14];
			ea[5] = ip[15];
			break;
	}
	return x;
}

/*
 *  fill in an arp entry for broadcast or multicast
 *  addresses.  Return the first queued packet for the
 *  IP address.
 */
static struct block *multicastarp(struct Fs *f,
								  struct arpent *a, struct medium *medium,
								  uint8_t * mac)
{
	/* is it broadcast? */
	switch (ipforme(f, a->ip)) {
		case Runi:
			return NULL;
		case Rbcast:
			memset(mac, 0xff, 6);
			return arpresolve(f->arp, a, medium, mac);
		default:
			break;
	}

	/* if multicast, fill in mac */
	switch (multicastea(mac, a->ip)) {
		case V4:
		case V6:
			return arpresolve(f->arp, a, medium, mac);
	}

	/* let arp take care of it */
	return NULL;
}

linker_func_4(ethermediumlink)
{
	addipmedium(&ethermedium);
	addipmedium(&trexmedium);
}

static void etherpref2addr(uint8_t * pref, uint8_t * ea)
{
	pref[8] = ea[0] | 0x2;
	pref[9] = ea[1];
	pref[10] = ea[2];
	pref[11] = 0xFF;
	pref[12] = 0xFE;
	pref[13] = ea[3];
	pref[14] = ea[4];
	pref[15] = ea[5];
}
