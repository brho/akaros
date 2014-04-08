/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

/*
 * Realtek RTL8110S/8169S.
 * Mostly there. There are some magic register values used
 * which are not described in any datasheet or driver but seem
 * to be necessary.
 * No tuning has been done. Only tested on an RTL8110S, there
 * are slight differences between the chips in the series so some
 * tweaks may be needed.
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
#include <arch/pci.h>
#include <assert.h>
#include <ip.h>
#include <ns.h>

#define ilock(x) spin_lock_irqsave(x)
#define iunlock(x) spin_unlock_irqsave(x)

#include "ethermii.h"

#define HOWMANY(x, y)	(((x)+((y)-1))/(y))
enum {					/* registers */
	Idr0		= 0x00,		/* MAC address */
	Mar0		= 0x08,		/* Multicast address */
	Dtccr		= 0x10,		/* Dump Tally Counter Command */
	Tnpds		= 0x20,		/* Transmit Normal Priority Descriptors */
	Thpds		= 0x28,		/* Transmit High Priority Descriptors */
	Flash		= 0x30,		/* Flash Memory Read/Write */
	Erbcr		= 0x34,		/* Early Receive Byte Count */
	Ersr		= 0x36,		/* Early Receive Status */
	Cr		= 0x37,		/* Command Register */
	Tppoll		= 0x38,		/* Transmit Priority Polling */
	Imr		= 0x3C,		/* Interrupt Mask */
	Isr		= 0x3E,		/* Interrupt Status */
	Tcr		= 0x40,		/* Transmit Configuration */
	Rcr		= 0x44,		/* Receive Configuration */
	Tctr		= 0x48,		/* Timer Count */
	Mpc		= 0x4C,		/* Missed Packet Counter */
	Cr9346		= 0x50,		/* 9346 Command Register */
	Config0		= 0x51,		/* Configuration Register 0 */
	Config1		= 0x52,		/* Configuration Register 1 */
	Config2		= 0x53,		/* Configuration Register 2 */
	Config3		= 0x54,		/* Configuration Register 3 */
	Config4		= 0x55,		/* Configuration Register 4 */
	Config5		= 0x56,		/* Configuration Register 5 */
	Timerint	= 0x58,		/* Timer Interrupt */
	Mulint		= 0x5C,		/* Multiple Interrupt Select */
	Phyar		= 0x60,		/* PHY Access */
	Tbicsr0		= 0x64,		/* TBI Control and Status */
	Tbianar		= 0x68,		/* TBI Auto-Negotiation Advertisment */
	Tbilpar		= 0x6A,		/* TBI Auto-Negotiation Link Partner */

	Rms		= 0xDA,		/* Receive Packet Maximum Size */
	Cplusc		= 0xE0,		/* C+ Command */
	Rdsar		= 0xE4,		/* Receive Descriptor Start Address */
	Mtps		= 0xEC,		/* Max. Transmit Packet Size */
};

enum {					/* Dtccr */
	Cmd		= 0x00000008,	/* Command */
};

enum {					/* Cr */
	Te		= 0x04,		/* Transmitter Enable */
	Re		= 0x08,		/* Receiver Enable */
	Rst		= 0x10,		/* Software Reset */
};

enum {					/* Tppoll */
	Fswint		= 0x01,		/* Forced Software Interrupt */
	Npq		= 0x40,		/* Normal Priority Queue polling */
	Hpq		= 0x80,		/* High Priority Queue polling */
};

enum {					/* Imr/Isr */
	Rok		= 0x0001,	/* Receive OK */
	Rer		= 0x0002,	/* Receive Error */
	Tok		= 0x0004,	/* Transmit OK */
	Ter		= 0x0008,	/* Transmit Error */
	Rdu		= 0x0010,	/* Receive Descriptor Unavailable */
	Punlc		= 0x0020,	/* Packet Underrun or Link Change */
	Fovw		= 0x0040,	/* Receive FIFO Overflow */
	Tdu		= 0x0080,	/* Transmit Descriptor Unavailable */
	Swint		= 0x0100,	/* Software Interrupt */
	Timeout		= 0x4000,	/* Timer */
	Serr		= 0x8000,	/* System Error */
};

enum {					/* Tcr */
	MtxdmaSHIFT	= 8,		/* Max. DMA Burst Size */
	MtxdmaMASK	= 0x00000700,
	Mtxdmaunlimited	= 0x00000700,
	Acrc		= 0x00010000,	/* Append CRC (not) */
	Lbk0		= 0x00020000,	/* Loopback Test 0 */
	Lbk1		= 0x00040000,	/* Loopback Test 1 */
	Ifg2		= 0x00080000,	/* Interframe Gap 2 */
	HwveridSHIFT	= 23,		/* Hardware Version ID */
	HwveridMASK	= 0x7C800000,
	Macv01		= 0x00000000,	/* RTL8169 */
	Macv02		= 0x00800000,	/* RTL8169S/8110S */
	Macv03		= 0x04000000,	/* RTL8169S/8110S */
	Macv04		= 0x10000000,	/* RTL8169SB/8110SB */
	Macv05		= 0x18000000,	/* RTL8169SC/8110SC */
	Macv11		= 0x30000000,	/* RTL8168B/8111B */
	Macv12		= 0x38000000,	/* RTL8169B/8111B */
	Macv13		= 0x34000000,	/* RTL8101E */
	Macv14		= 0x30800000,	/* RTL8100E */
	Macv15		= 0x38800000,	/* RTL8100E */
	Ifg0		= 0x01000000,	/* Interframe Gap 0 */
	Ifg1		= 0x02000000,	/* Interframe Gap 1 */
};

enum {					/* Rcr */
	Aap		= 0x00000001,	/* Accept All Packets */
	Apm		= 0x00000002,	/* Accept Physical Match */
	Am		= 0x00000004,	/* Accept Multicast */
	Ab		= 0x00000008,	/* Accept Broadcast */
	Ar		= 0x00000010,	/* Accept Runt */
	Aer		= 0x00000020,	/* Accept Error */
	Sel9356		= 0x00000040,	/* 9356 EEPROM used */
	MrxdmaSHIFT	= 8,		/* Max. DMA Burst Size */
	MrxdmaMASK	= 0x00000700,
	Mrxdmaunlimited	= 0x00000700,
	RxfthSHIFT	= 13,		/* Receive Buffer Length */
	RxfthMASK	= 0x0000E000,
	Rxfth256	= 0x00008000,
	Rxfthnone	= 0x0000E000,
	Rer8		= 0x00010000,	/* Accept Error Packets > 8 bytes */
	MulERINT	= 0x01000000,	/* Multiple Early Interrupt Select */
};

enum {					/* Cr9346 */
	Eedo		= 0x01,		/* */
	Eedi		= 0x02,		/* */
	Eesk		= 0x04,		/* */
	Eecs		= 0x08,		/* */
	Eem0		= 0x40,		/* Operating Mode */
	Eem1		= 0x80,
};

enum {					/* Phyar */
	DataMASK	= 0x0000FFFF,	/* 16-bit GMII/MII Register Data */
	DataSHIFT	= 0,
	RegaddrMASK	= 0x001F0000,	/* 5-bit GMII/MII Register Address */
	RegaddrSHIFT	= 16,
	Flag		= 0x80000000,	/* */
};

enum {					/* Cplusc */
	Mulrw		= 0x0008,	/* PCI Multiple R/W Enable */
	Dac		= 0x0010,	/* PCI Dual Address Cycle Enable */
	Rxchksum	= 0x0020,	/* Receive Checksum Offload Enable */
	Rxvlan		= 0x0040,	/* Receive VLAN De-tagging Enable */
	Endian		= 0x0200,	/* Endian Mode */
};

typedef struct D D;			/* Transmit/Receive Descriptor */
struct D {
	uint32_t	control;
	uint32_t	vlan;
	uint32_t	addrlo;
	uint32_t	addrhi;
};

enum {					/* Transmit Descriptor control */
	TxflMASK	= 0x0000FFFF,	/* Transmit Frame Length */
	TxflSHIFT	= 0,
	Tcps		= 0x00010000,	/* TCP Checksum Offload */
	Udpcs		= 0x00020000,	/* UDP Checksum Offload */
	Ipcs		= 0x00040000,	/* IP Checksum Offload */
	Lgsen		= 0x08000000,	/* Large Send */
};

enum {					/* Receive Descriptor control */
	RxflMASK	= 0x00003FFF,	/* Receive Frame Length */
	RxflSHIFT	= 0,
	Tcpf		= 0x00004000,	/* TCP Checksum Failure */
	Udpf		= 0x00008000,	/* UDP Checksum Failure */
	Ipf		= 0x00010000,	/* IP Checksum Failure */
	Pid0		= 0x00020000,	/* Protocol ID0 */
	Pid1		= 0x00040000,	/* Protocol ID1 */
	Crce		= 0x00080000,	/* CRC Error */
	Runt		= 0x00100000,	/* Runt Packet */
	Res		= 0x00200000,	/* Receive Error Summary */
	Rwt		= 0x00400000,	/* Receive Watchdog Timer Expired */
	Fovf		= 0x00800000,	/* FIFO Overflow */
	Bovf		= 0x01000000,	/* Buffer Overflow */
	Bar		= 0x02000000,	/* Broadcast Address Received */
	Pam		= 0x04000000,	/* Physical Address Matched */
	Mar		= 0x08000000,	/* Multicast Address Received */
};

enum {					/* General Descriptor control */
	Ls		= 0x10000000,	/* Last Segment Descriptor */
	Fs		= 0x20000000,	/* First Segment Descriptor */
	Eor		= 0x40000000,	/* End of Descriptor Ring */
	Own		= 0x80000000,	/* Ownership */
};

/*
 */
enum {					/* Ring sizes  (<= 1024) */
	Ntd		= 32,		/* Transmit Ring */
	Nrd		= 128,		/* Receive Ring */
};

#define Mps ROUNDUP(ETHERMAXTU + 4, 128)

typedef struct Dtcc Dtcc;
struct Dtcc {
	uint64_t	txok;
	uint64_t	rxok;
	uint64_t	txer;
	uint32_t	rxer;
	uint16_t	misspkt;
	uint16_t	fae;
	uint32_t	tx1col;
	uint32_t	txmcol;
	uint64_t	rxokph;
	uint64_t	rxokbrd;
	uint32_t	rxokmu;
	uint16_t	txabt;
	uint16_t	txundrn;
};

enum {						/* Variants */
	Rtl8100e	= (0x8136<<16)|0x10EC,	/* RTL810[01]E: pci -e */
	Rtl8169c	= (0x0116<<16)|0x16EC,	/* RTL8169C+ (USR997902) */
	Rtl8169sc	= (0x8167<<16)|0x10EC,	/* RTL8169SC */
	Rtl8168b	= (0x8168<<16)|0x10EC,	/* RTL8168B: pci-e */
	Rtl8169		= (0x8169<<16)|0x10EC,	/* RTL8169 */
};

struct ctlr {
	int	port;
	struct pci_device *pci;
	struct ctlr*	next;
	int	active;

	qlock_t	alock;			/* attach */
	spinlock_t	ilock;			/* init */
	int	init;			/*  */

	int	pciv;			/*  */
	int	macv;			/* MAC version */
	int	phyv;			/* PHY version */
	int	pcie;			/* flag: pci-express device? */

	uint64_t	mchash;			/* multicast hash */

	struct mii*	mii;

	spinlock_t	tlock;			/* transmit */
	D*	td;			/* descriptor ring */
	struct block**	tb;			/* transmit buffers */
	int	ntd;

	int	tdh;			/* head - producer index (host) */
	int	tdt;			/* tail - consumer index (NIC) */
	int	ntdfree;
	int	ntq;

	int	mtps;			/* Max. Transmit Packet Size */

	spinlock_t	rlock;			/* receive */
	D*	rd;			/* descriptor ring */
	struct block**	rb;			/* receive buffers */
	int	nrd;

	int	rdh;			/* head - producer index (NIC) */
	int	rdt;			/* tail - consumer index (host) */
	int	nrdfree;

	int	tcr;			/* transmit configuration register */
	int	rcr;			/* receive configuration register */
	int	imr;

	qlock_t	slock;			/* statistics */
	Dtcc*	dtcc;
	unsigned int	txdu;
	unsigned int	tcpf;
	unsigned int	udpf;
	unsigned int	ipf;
	unsigned int	fovf;
	unsigned int	ierrs;
	unsigned int	rer;
	unsigned int	rdu;
	unsigned int	punlc;
	unsigned int	fovw;
	unsigned int	mcast;
};

static struct ctlr* rtl8169ctlrhead;
static struct ctlr* rtl8169ctlrtail;

#define csr8r(c, r)	(inb((c)->port+(r)))
#define csr16r(c, r)	(inw((c)->port+(r)))
#define csr32r(c, r)	(inl((c)->port+(r)))
#define csr8w(c, r, b)	(outb((c)->port+(r), (uint8_t)(b)))
#define csr16w(c, r, w)	(outw((c)->port+(r), (uint16_t)(w)))
#define csr32w(c, r, l)	(outl((c)->port+(r), (uint32_t)(l)))

static int
rtl8169miimir(struct ctlr* ctlr, int pa, int ra)
{
	unsigned int r;
	int timeo;

	if(pa != 1)
		return -1;

	r = (ra<<16) & RegaddrMASK;
	csr32w(ctlr, Phyar, r);
	udelay(1000*1);
	for(timeo = 0; timeo < 2000; timeo++){
		if((r = csr32r(ctlr, Phyar)) & Flag)
			break;
		udelay(100);
	}
	if(!(r & Flag))
		return -1;

	return (r & DataMASK)>>DataSHIFT;
}

static int
rtl8169miimiw(struct ctlr* ctlr, int pa, int ra, int data)
{
	unsigned int r;
	int timeo;

	if(pa != 1)
		return -1;

	r = Flag|((ra<<16) & RegaddrMASK)|((data<<DataSHIFT) & DataMASK);
	csr32w(ctlr, Phyar, r);
	udelay(1000*1);
	for(timeo = 0; timeo < 2000; timeo++){
		if(!((r = csr32r(ctlr, Phyar)) & Flag))
			break;
		udelay(100);
	}
	if(r & Flag)
		return -1;

	return 0;
}

static int
rtl8169miirw(struct mii* mii, int write, int pa, int ra, int data)
{
	if(write)
		return rtl8169miimiw(mii->ctlr, pa, ra, data);

	return rtl8169miimir(mii->ctlr, pa, ra);
}

static struct mii*
rtl8169mii(struct ctlr* ctlr)
{
	struct mii* mii;
	struct miiphy *phy;

	/*
	 * Link management.
	 *
	 * Get rev number out of Phyidr2 so can config properly.
	 * There's probably more special stuff for Macv0[234] needed here.
	 */
	ctlr->phyv = rtl8169miimir(ctlr, 1, Phyidr2) & 0x0F;
	if(ctlr->macv == Macv02){
		csr8w(ctlr, 0x82, 1);				/* magic */
		rtl8169miimiw(ctlr, 1, 0x0B, 0x0000);		/* magic */
	}
	if((mii = miiattach(ctlr, (1<<1), rtl8169miirw)) == NULL)
		return NULL;

	phy = mii->curphy;
	printd("oui %#ux phyno %d, macv = %#8.8ux phyv = %#4.4ux\n",
		phy->oui, phy->phyno, ctlr->macv, ctlr->phyv);

	if(miistatus(mii) < 0){
		miireset(mii);
		miiane(mii, ~0, ~0, ~0);
	}

	return mii;
}

static void
rtl8169promiscuous(void* arg, int on)
{
	struct ether *edev;
	struct ctlr * ctlr;

	edev = arg;
	ctlr = edev->ctlr;
	ilock(&ctlr->ilock);

	if(on)
		ctlr->rcr |= Aap;
	else
		ctlr->rcr &= ~Aap;
	csr32w(ctlr, Rcr, ctlr->rcr);
	iunlock(&ctlr->ilock);
}

enum {
	/* everyone else uses 0x04c11db7, but they both produce the same crc */
	Etherpolybe = 0x04c11db6,
	Bytemask = (1<<8) - 1,
};

static uint32_t
ethercrcbe(uint8_t *addr, long len)
{
	int i, j;
	uint32_t c, crc, carry;

	crc = ~0U;
	for (i = 0; i < len; i++) {
		c = addr[i];
		for (j = 0; j < 8; j++) {
			carry = ((crc & (1UL << 31))? 1: 0) ^ (c & 1);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ Etherpolybe) | carry;
		}
	}
	return crc;
}

static uint32_t
swabl(uint32_t l)
{
	return (l>>24) | ((l>>8) & (Bytemask<<8)) |
		((l<<8) & (Bytemask<<16)) | (l<<24);
}

static void
rtl8169multicast(void* ether, uint8_t *eaddr, int add)
{
	struct ether *edev;
	struct ctlr *ctlr;

	if (!add)
		return;	/* ok to keep receiving on old mcast addrs */

	edev = ether;
	ctlr = edev->ctlr;
	ilock(&ctlr->ilock);

	ctlr->mchash |= 1ULL << (ethercrcbe(eaddr, Eaddrlen) >> 26);

	ctlr->rcr |= Am;
	csr32w(ctlr, Rcr, ctlr->rcr);

	/* pci-e variants reverse the order of the hash byte registers */
	if (ctlr->pcie) {
		csr32w(ctlr, Mar0,   swabl(ctlr->mchash>>32));
		csr32w(ctlr, Mar0+4, swabl(ctlr->mchash));
	} else {
		csr32w(ctlr, Mar0,   ctlr->mchash);
		csr32w(ctlr, Mar0+4, ctlr->mchash>>32);
	}

	iunlock(&ctlr->ilock);
}

static long
rtl8169ifstat(struct ether* edev, void* a, long n, uint32_t offset)
{
	ERRSTACK(2);
	struct ctlr *ctlr;
	Dtcc *dtcc;
	int timeo;
	char *alloc, *e, *p;

	ctlr = edev->ctlr;
	qlock(&ctlr->slock);

	alloc = NULL;
	if(waserror()){
		qunlock(&ctlr->slock);
		kfree(alloc);
		nexterror();
	}

	csr32w(ctlr, Dtccr+4, 0);
	csr32w(ctlr, Dtccr, paddr_low32(ctlr->dtcc)|Cmd);
	for(timeo = 0; timeo < 1000; timeo++){
		if(!(csr32r(ctlr, Dtccr) & Cmd))
			break;
		udelay(1000*1);
	}
	if(csr32r(ctlr, Dtccr) & Cmd)
		error(Eio);
	dtcc = ctlr->dtcc;

	edev->netif.oerrs = dtcc->txer;
	edev->netif.crcs = dtcc->rxer;
	edev->netif.frames = dtcc->fae;
	edev->netif.buffs = dtcc->misspkt;
	edev->netif.overflows = ctlr->txdu+ctlr->rdu;

	if(n == 0){
		qunlock(&ctlr->slock);
		poperror();
		return 0;
	}

	if((alloc = kzmalloc(READSTR, 0)) == NULL)
		error(Enomem);
	e = alloc+READSTR;

	p = seprintf(alloc, e, "TxOk: %llu\n", dtcc->txok);
	p = seprintf(p, e, "RxOk: %llu\n", dtcc->rxok);
	p = seprintf(p, e, "TxEr: %llu\n", dtcc->txer);
	p = seprintf(p, e, "RxEr: %u\n", dtcc->rxer);
	p = seprintf(p, e, "MissPkt: %u\n", dtcc->misspkt);
	p = seprintf(p, e, "FAE: %u\n", dtcc->fae);
	p = seprintf(p, e, "Tx1Col: %u\n", dtcc->tx1col);
	p = seprintf(p, e, "TxMCol: %u\n", dtcc->txmcol);
	p = seprintf(p, e, "RxOkPh: %llu\n", dtcc->rxokph);
	p = seprintf(p, e, "RxOkBrd: %llu\n", dtcc->rxokbrd);
	p = seprintf(p, e, "RxOkMu: %u\n", dtcc->rxokmu);
	p = seprintf(p, e, "TxAbt: %u\n", dtcc->txabt);
	p = seprintf(p, e, "TxUndrn: %u\n", dtcc->txundrn);

	p = seprintf(p, e, "txdu: %u\n", ctlr->txdu);
	p = seprintf(p, e, "tcpf: %u\n", ctlr->tcpf);
	p = seprintf(p, e, "udpf: %u\n", ctlr->udpf);
	p = seprintf(p, e, "ipf: %u\n", ctlr->ipf);
	p = seprintf(p, e, "fovf: %u\n", ctlr->fovf);
	p = seprintf(p, e, "ierrs: %u\n", ctlr->ierrs);
	p = seprintf(p, e, "rer: %u\n", ctlr->rer);
	p = seprintf(p, e, "rdu: %u\n", ctlr->rdu);
	p = seprintf(p, e, "punlc: %u\n", ctlr->punlc);
	p = seprintf(p, e, "fovw: %u\n", ctlr->fovw);

	p = seprintf(p, e, "tcr: 0x%#8.8u\n", ctlr->tcr);
	p = seprintf(p, e, "rcr: 0x%#8.8u\n", ctlr->rcr);
	p = seprintf(p, e, "multicast: %u\n", ctlr->mcast);

	if(ctlr->mii != NULL && ctlr->mii->curphy != NULL)
		miidumpphy(ctlr->mii, p, e);

	n = readstr(offset, a, n, alloc);

	qunlock(&ctlr->slock);
	poperror();
	kfree(alloc);

	return n;
}

static void
rtl8169halt(struct ctlr* ctlr)
{
	csr8w(ctlr, Cr, 0);
	csr16w(ctlr, Imr, 0);
	csr16w(ctlr, Isr, ~0);
}

static int
rtl8169reset(struct ctlr* ctlr)
{
	uint32_t r;
	int timeo;

	/*
	 * Soft reset the controller.
	 */
	csr8w(ctlr, Cr, Rst);
	for(r = timeo = 0; timeo < 1000; timeo++){
		r = csr8r(ctlr, Cr);
		if(!(r & Rst))
			break;
		udelay(1000*1);
	}
	rtl8169halt(ctlr);

	if(r & Rst)
		return -1;
	return 0;
}

static void
rtl8169replenish(struct ctlr* ctlr)
{
	D *d;
	int rdt;
	struct block *bp;

	rdt = ctlr->rdt;
	while(NEXT_RING(rdt, ctlr->nrd) != ctlr->rdh){
		d = &ctlr->rd[rdt];
		if(ctlr->rb[rdt] == NULL){
			/*
			 * Simple allocation for now.
			 * This better be aligned on 8.
			 */
			bp = iallocb(Mps);
			if(bp == NULL){
				printk("no available buffers\n");
				break;
			}
			ctlr->rb[rdt] = bp;
			d->addrlo = paddr_low32(bp->rp);
			d->addrhi = paddr_high32(bp->rp);
		}
		wmb();
		d->control |= Own|Mps;
		rdt = NEXT_RING(rdt, ctlr->nrd);
		ctlr->nrdfree++;
	}
	ctlr->rdt = rdt;
}

static int
rtl8169init(struct ether* edev)
{
	int i;
	uint32_t r;
	struct block *bp;
	struct ctlr *ctlr;
	uint8_t cplusc;

	ctlr = edev->ctlr;
	ilock(&ctlr->ilock);

	rtl8169halt(ctlr);

	/*
	 * MAC Address.
	 * Must put chip into config register write enable mode.
	 */
	csr8w(ctlr, Cr9346, Eem1|Eem0);
	r = (edev->ea[3]<<24)|(edev->ea[2]<<16)|(edev->ea[1]<<8)|edev->ea[0];
	csr32w(ctlr, Idr0, r);
	r = (edev->ea[5]<<8)|edev->ea[4];
	csr32w(ctlr, Idr0+4, r);

	/*
	 * Transmitter.
	 */
	memset(ctlr->td, 0, sizeof(D)*ctlr->ntd);
	ctlr->tdh = ctlr->tdt = 0;
	ctlr->td[ctlr->ntd-1].control = Eor;

	/*
	 * Receiver.
	 * Need to do something here about the multicast filter.
	 */
	memset(ctlr->rd, 0, sizeof(D)*ctlr->nrd);
	ctlr->nrdfree = ctlr->rdh = ctlr->rdt = 0;
	ctlr->rd[ctlr->nrd-1].control = Eor;

	for(i = 0; i < ctlr->nrd; i++){
		if((bp = ctlr->rb[i]) != NULL){
			ctlr->rb[i] = NULL;
			freeb(bp);
		}
	}
	rtl8169replenish(ctlr);
	ctlr->rcr = Rxfthnone|Mrxdmaunlimited|Ab|Am|Apm;

	/*
	 * Mtps is in units of 128 except for the RTL8169
	 * where is is 32. If using jumbo frames should be
	 * set to 0x3F.
	 * Setting Mulrw in Cplusc disables the Tx/Rx DMA burst
	 * settings in Tcr/Rcr; the (1<<14) is magic.
	 */
	ctlr->mtps = HOWMANY(Mps, 128);
	cplusc = csr16r(ctlr, Cplusc) & ~(1<<14);
	cplusc |= /*Rxchksum|*/Mulrw;
	switch(ctlr->macv){
	default:
		printd("rtl8169: unsupported macv %#ux\n", ctlr->macv);
		break;	/* perhaps it just works */
	case Macv01:
		ctlr->mtps = HOWMANY(Mps, 32);
		break;
	case Macv02:
	case Macv03:
		cplusc |= (1<<14);			/* magic */
		break;
	case Macv05:
		/*
		 * This is interpreted from clearly bogus code
		 * in the manufacturer-supplied driver, it could
		 * be wrong. Untested.
		 */
		printk("untested\n");
		break;
#if 0	      
		r = csr8r(ctlr, Config2) & 0x07;
		if(r == 0x01)				/* 66MHz PCI */
			csr32w(ctlr, 0x7C, 0x0007FFFF);	/* magic */
		else
			csr32w(ctlr, 0x7C, 0x0007FF00);	/* magic */
		pciclrmwi(ctlr->pcidev);
#endif
		break;
	case Macv13:
		printk("untested macv13 write\n");
		break;
#if 0
		/*
		 * This is interpreted from clearly bogus code
		 * in the manufacturer-supplied driver, it could
		 * be wrong. Untested.
		 */
		pcicfgw8(ctlr->pcidev, 0x68, 0x00);	/* magic */
		pcicfgw8(ctlr->pcidev, 0x69, 0x08);	/* magic */
		break;
#endif
	case Macv04:
	case Macv11:
	case Macv12:
	case Macv14:
	case Macv15:
		break;
	}

	/*
	 * Enable receiver/transmitter.
	 * Need to do this first or some of the settings below
	 * won't take.
	 */
	switch(ctlr->pciv){
	default:
		csr8w(ctlr, Cr, Te|Re);
		csr32w(ctlr, Tcr, Ifg1|Ifg0|Mtxdmaunlimited);
		csr32w(ctlr, Rcr, ctlr->rcr);
		csr32w(ctlr, Mar0,   0);
		csr32w(ctlr, Mar0+4, 0);
		ctlr->mchash = 0;
	case Rtl8169sc:
	case Rtl8168b:
		break;
	}

	/*
	 * Interrupts.
	 * Disable Tdu|Tok for now, the transmit routine will tidy.
	 * Tdu means the NIC ran out of descriptors to send, so it
	 * doesn't really need to ever be on.
	 */
	csr32w(ctlr, Timerint, 0);
	ctlr->imr = Serr|Timeout|Fovw|Punlc|Rdu|Ter|Rer|Rok;
	csr16w(ctlr, Imr, ctlr->imr);

	/*
	 * Clear missed-packet counter;
	 * initial early transmit threshold value;
	 * set the descriptor ring base addresses;
	 * set the maximum receive packet size;
	 * no early-receive interrupts.
	 */
	csr32w(ctlr, Mpc, 0);
	csr8w(ctlr, Mtps, ctlr->mtps);
	csr32w(ctlr, Tnpds + 4, paddr_high32(ctlr->td));
	csr32w(ctlr, Tnpds, paddr_low32(ctlr->td));
	csr32w(ctlr, Rdsar + 4, paddr_high32(ctlr->rd));
	csr32w(ctlr, Rdsar, paddr_low32(ctlr->rd));
	csr16w(ctlr, Rms, Mps);
	r = csr16r(ctlr, Mulint) & 0xF000;
	csr16w(ctlr, Mulint, r);
	csr16w(ctlr, Cplusc, cplusc);

	/*
	 * Set configuration.
	 */
	switch(ctlr->pciv){
	default:
		break;
	case Rtl8169sc:
		csr16w(ctlr, 0xE2, 0);			/* magic */
		csr8w(ctlr, Cr, Te|Re);
		csr32w(ctlr, Tcr, Ifg1|Ifg0|Mtxdmaunlimited);
		csr32w(ctlr, Rcr, ctlr->rcr);
		break;
	case Rtl8168b:
	case Rtl8169c:
		csr16w(ctlr, 0xE2, 0);			/* magic */
		csr16w(ctlr, Cplusc, 0x2000);		/* magic */
		csr8w(ctlr, Cr, Te|Re);
		csr32w(ctlr, Tcr, Ifg1|Ifg0|Mtxdmaunlimited);
		csr32w(ctlr, Rcr, ctlr->rcr);
		csr16w(ctlr, Rms, 0x0800);
		csr8w(ctlr, Mtps, 0x3F);
		break;
	}
	ctlr->tcr = csr32r(ctlr, Tcr);
	csr8w(ctlr, Cr9346, 0);

	iunlock(&ctlr->ilock);

//	rtl8169mii(ctlr);

	return 0;
}

static void
rtl8169attach(struct ether* edev)
{
	int timeo;
	struct ctlr *ctlr;
	struct miiphy *phy;

	ctlr = edev->ctlr;
	qlock(&ctlr->alock);
	if(ctlr->init == 0){
		/*
		 * Handle allocation/init errors here.
		 */
		ctlr->td = kzmalloc_align(sizeof(D) * Ntd, KMALLOC_WAIT, 256);
		ctlr->tb = kzmalloc(Ntd * sizeof(struct block *), KMALLOC_WAIT);
		ctlr->ntd = Ntd;
		ctlr->rd = kzmalloc_align(sizeof(D) * Nrd, KMALLOC_WAIT, 256);
		ctlr->rb = kzmalloc(Nrd * sizeof(struct block *), KMALLOC_WAIT);
		ctlr->nrd = Nrd;
		ctlr->dtcc = kzmalloc_align(sizeof(Dtcc), KMALLOC_WAIT, 64);
		rtl8169init(edev);
		ctlr->init = 1;
	}
	qunlock(&ctlr->alock);

	/*
	 * Wait for link to be ready.
	 */
	for(timeo = 0; timeo < 350; timeo++){
		if(miistatus(ctlr->mii) == 0)
			break;
		udelay_sched(10000);
	}
	phy = ctlr->mii->curphy;
	printd("%s: speed %d fd %d link %d rfc %d tfc %d\n",
		edev->netif.name, phy->speed, phy->fd, phy->link, phy->rfc, phy->tfc);
}

static void
rtl8169link(struct ether* edev)
{
	int limit;
	struct ctlr *ctlr;
	struct miiphy *phy;

	ctlr = edev->ctlr;

	/*
	 * Maybe the link changed - do we care very much?
	 * Could stall transmits if no link, maybe?
	 */
	if(ctlr->mii == NULL || ctlr->mii->curphy == NULL)
		return;

	phy = ctlr->mii->curphy;
	if(miistatus(ctlr->mii) < 0){
		// TODO : no name here
		printk("%slink n: speed %d fd %d link %d rfc %d tfc %d\n",
			edev->netif.name, phy->speed, phy->fd, phy->link,
			phy->rfc, phy->tfc);
		edev->netif.link = 0;
		return;
	}
	edev->netif.link = 1;

	limit = 256*1024;
	if(phy->speed == 10){
		edev->netif.mbps = 10;
		limit = 65*1024;
	}
	else if(phy->speed == 100)
		edev->netif.mbps = 100;
	else if(phy->speed == 1000)
		edev->netif.mbps = 1000;
	printk("%slink y: speed %d fd %d link %d rfc %d tfc %d\n",
		edev->netif.name, phy->speed, phy->fd, phy->link,
		phy->rfc, phy->tfc);

	if(edev->oq != NULL)
		qsetlimit(edev->oq, limit);
}

static void
rtl8169transmit(struct ether* edev)
{
	D *d;
	struct block *bp;
	struct ctlr *ctlr;
	int control, x;

	ctlr = edev->ctlr;

	ilock(&ctlr->tlock);
	for(x = ctlr->tdh; ctlr->ntq > 0; x = NEXT_RING(x, ctlr->ntd)){
		d = &ctlr->td[x];
		if((control = d->control) & Own)
			break;

		/*
		 * Check errors and log here.
		 */

		/*
		 * Free it up.
		 * Need to clean the descriptor here? Not really.
		 * Simple freeb for now (no chain and freeblist).
		 * Use ntq count for now.
		 */
		freeb(ctlr->tb[x]);
		ctlr->tb[x] = NULL;
		d->control &= Eor;

		ctlr->ntq--;
	}
	ctlr->tdh = x;

	x = ctlr->tdt;
	while(ctlr->ntq < (ctlr->ntd-1)){
		if((bp = qget(edev->oq)) == NULL)
			break;

		d = &ctlr->td[x];
		d->addrlo = paddr_low32(bp->rp);
		d->addrhi = paddr_high32(bp->rp);
		ctlr->tb[x] = bp;
		wmb();
		d->control |= Own|Fs|Ls|((BLEN(bp)<<TxflSHIFT) & TxflMASK);

		x = NEXT_RING(x, ctlr->ntd);
		ctlr->ntq++;
	}
	if(x != ctlr->tdt){
		ctlr->tdt = x;
		csr8w(ctlr, Tppoll, Npq);
	}
	else if(ctlr->ntq >= (ctlr->ntd-1))
		ctlr->txdu++;

	iunlock(&ctlr->tlock);
}

static void
rtl8169receive(struct ether* edev)
{
	D *d;
	int rdh;
	struct block *bp;
	struct ctlr *ctlr;
	uint32_t control;

	ctlr = edev->ctlr;

	rdh = ctlr->rdh;
	for(;;){
		d = &ctlr->rd[rdh];

		if(d->control & Own)
			break;

		control = d->control;
		if((control & (Fs|Ls|Res)) == (Fs|Ls)){
			bp = ctlr->rb[rdh];
			ctlr->rb[rdh] = NULL;
			bp->wp = bp->rp + ((control & RxflMASK)>>RxflSHIFT)-4;
			bp->next = NULL;

			if(control & Fovf)
				ctlr->fovf++;
			if(control & Mar)
				ctlr->mcast++;

			switch(control & (Pid1|Pid0)){
			default:
				break;
			case Pid0:
				if(control & Tcpf){
					ctlr->tcpf++;
					break;
				}
				bp->flag |= Btcpck;
				break;
			case Pid1:
				if(control & Udpf){
					ctlr->udpf++;
					break;
				}
				bp->flag |= Budpck;
				break;
			case Pid1|Pid0:
				if(control & Ipf){
					ctlr->ipf++;
					break;
				}
				bp->flag |= Bipck;
				break;
			}
			etheriq(edev, bp, 1);
		}
		else{
			/*
			 * Error stuff here.
			print("control %#8.8ux\n", control);
			 */
		}
		d->control &= Eor;
		ctlr->nrdfree--;
		rdh = NEXT_RING(rdh, ctlr->nrd);

		if(ctlr->nrdfree < ctlr->nrd/2)
			rtl8169replenish(ctlr);
	}
	ctlr->rdh = rdh;
}

static void
rtl8169interrupt(struct hw_trapframe *hw_tf, void *arg)
{
	struct ctlr *ctlr;
	struct ether *edev;
	uint32_t isr;

	edev = arg;
	ctlr = edev->ctlr;

	while((isr = csr16r(ctlr, Isr)) != 0 && isr != 0xFFFF){
		csr16w(ctlr, Isr, isr);
		if((isr & ctlr->imr) == 0)
			break;
		if(isr & (Fovw|Punlc|Rdu|Rer|Rok)){
			rtl8169receive(edev);
			if(!(isr & (Punlc|Rok)))
				ctlr->ierrs++;
			if(isr & Rer)
				ctlr->rer++;
			if(isr & Rdu)
				ctlr->rdu++;
			if(isr & Punlc)
				ctlr->punlc++;
			if(isr & Fovw)
				ctlr->fovw++;
			isr &= ~(Fovw|Rdu|Rer|Rok);
		}

		if(isr & (Tdu|Ter|Tok)){
			rtl8169transmit(edev);
			isr &= ~(Tdu|Ter|Tok);
		}

		if(isr & Punlc){
			rtl8169link(edev);
			isr &= ~Punlc;
		}

		/*
		 * Some of the reserved bits get set sometimes...
		 */
		if(isr & (Serr|Timeout|Tdu|Fovw|Punlc|Rdu|Ter|Tok|Rer|Rok))
			panic("rtl8169interrupt: imr %#4.4ux isr %#4.4ux\n",
				csr16r(ctlr, Imr), isr);
	}
}

static void
rtl8169pci(void)
{
	struct pci_device *pcidev;

	struct ctlr *ctlr;
	int id, port, pcie;

	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* This checks that pcidev is a Network Controller for Ethernet */
		if (pcidev->class != 0x02 || pcidev->subclass != 0x00)
			continue;
		id = pcidev->dev_id << 16 | pcidev->ven_id;

		pcie = 0;
		switch(id) {
		default:
			continue;
		case Rtl8100e:			/* RTL810[01]E ? */
		case Rtl8168b:			/* RTL8168B */
			pcie = 1;
			break;
		case Rtl8169c:			/* RTL8169C */
		case Rtl8169sc:			/* RTL8169SC */
		case Rtl8169:			/* RTL8169 */
			break;
		case (0xC107<<16)|0x1259:	/* Corega CG-LAPCIGT */
			id = Rtl8169;
			break;
		}
		printk("rtl8169 driver found 0x%04x:%04x at %02x:%02x.%x\n",
		       pcidev->ven_id, pcidev->dev_id,
		       pcidev->bus, pcidev->dev, pcidev->func);

		port = pcidev->bar[0].pio_base;

		ctlr = kzmalloc(sizeof(struct ctlr), KMALLOC_WAIT);
		spinlock_init_irqsave(&ctlr->ilock);
		spinlock_init_irqsave(&ctlr->tlock);
		spinlock_init_irqsave(&ctlr->rlock);
		qlock_init(&ctlr->alock);
		qlock_init(&ctlr->slock);

		ctlr->port = port;
		ctlr->pci = pcidev;
		ctlr->pciv = id;
		ctlr->pcie = pcie;

		/* pcipms is something related to power mgmt, i think */
		#if 0
		if(pcigetpms(p) > 0){
			pcisetpms(p, 0);

			for(int i = 0; i < 6; i++)
				pcicfgw32(p, PciBAR0+i*4, p->mem[i].bar);
			pcicfgw8(p, PciINTL, p->intl);
			pcicfgw8(p, PciLTR, p->ltr);
			pcicfgw8(p, PciCLS, p->cls);
			pcicfgw16(p, PciPCR, p->pcr);
		}
		#endif

		if(rtl8169reset(ctlr)){
			kfree(ctlr);
			continue;
		}

		/*
		 * Extract the chip hardware version,
		 * needed to configure each properly.
		 */
		ctlr->macv = csr32r(ctlr, Tcr) & HwveridMASK;
		if((ctlr->mii = rtl8169mii(ctlr)) == NULL){
			kfree(ctlr);
			continue;
		}

		pci_set_bus_master(pcidev);

		if(rtl8169ctlrhead != NULL)
			rtl8169ctlrtail->next = ctlr;
		else
			rtl8169ctlrhead = ctlr;
		rtl8169ctlrtail = ctlr;
	}
}

static int
rtl8169pnp(struct ether* edev)
{
	uint32_t r;
	struct ctlr *ctlr;
	uint8_t ea[Eaddrlen];

	run_once(rtl8169pci());

	/*
	 * Any adapter matches if no edev->port is supplied,
	 * otherwise the ports must match.
	 */
	for(ctlr = rtl8169ctlrhead; ctlr != NULL; ctlr = ctlr->next){
		if(ctlr->active)
			continue;
		if(edev->port == 0 || edev->port == ctlr->port){
			ctlr->active = 1;
			break;
		}
	}
	if(ctlr == NULL)
		return -1;

	edev->ctlr = ctlr;
	edev->port = ctlr->port;
	edev->irq = ctlr->pci->irqline;
	edev->netif.mbps = 100;

	/*
	 * Check if the adapter's station address is to be overridden.
	 * If not, read it from the device and set in edev->ea.
	 */
	memset(ea, 0, Eaddrlen);
	if(memcmp(ea, edev->ea, Eaddrlen) == 0){
		r = csr32r(ctlr, Idr0);
		edev->ea[0] = r;
		edev->ea[1] = r>>8;
		edev->ea[2] = r>>16;
		edev->ea[3] = r>>24;
		r = csr32r(ctlr, Idr0+4);
		edev->ea[4] = r;
		edev->ea[5] = r>>8;
	}

	edev->tbdf = MKBUS(BusPCI, ctlr->pci->bus, ctlr->pci->dev,
	                   ctlr->pci->func);
	edev->attach = rtl8169attach;
	edev->transmit = rtl8169transmit;
	edev->ifstat = rtl8169ifstat;

	edev->netif.arg = edev;
	edev->netif.promiscuous = rtl8169promiscuous;
	edev->netif.multicast = rtl8169multicast;
//	edev->netif.shutdown = rtl8169shutdown;

	rtl8169link(edev);
	register_irq(edev->irq, rtl8169interrupt, edev, edev->tbdf);

	return 0;
}

linker_func_3(ether8169link)
{
	addethercard("rtl8169", rtl8169pnp);
}
