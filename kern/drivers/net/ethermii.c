/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

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
#include "ethermii.h"

static int
miiprobe(struct mii* mii, int mask)
{
	struct miiphy *miiphy;
	int bit, oui, phyno, r, rmask;

	/*
	 * Probe through mii for PHYs in mask;
	 * return the mask of those found in the current probe.
	 * If the PHY has not already been probed, update
	 * the Mii information.
	 */
	rmask = 0;
	for(phyno = 0; phyno < NMiiPhy; phyno++){
		bit = 1<<phyno;
		if(!(mask & bit))
			continue;
		if(mii->mask & bit){
			rmask |= bit;
			continue;
		}
		if(mii->rw(mii, 0, phyno, Bmsr, 0) == -1)
			continue;
		r = mii->rw(mii, 0, phyno, Phyidr1, 0)<<16;
		r |= mii->rw(mii, 0, phyno, Phyidr2, 0);
		oui = (r>>10) & 0xffff;
		if(oui == 0xffff || oui == 0)
			continue;

		if((miiphy = kzmalloc(sizeof(struct miiphy), 0)) == NULL)
			continue;

		miiphy->mii = mii;
		miiphy->phyno = phyno;
		miiphy->phyid = r;
		miiphy->oui = oui;

		miiphy->anar = ~0;
		miiphy->fc = ~0;
		miiphy->mscr = ~0;

		mii->phy[phyno] = miiphy;
		if(mii->curphy == NULL)
			mii->curphy = miiphy;
		mii->mask |= bit;
		mii->nphy++;

		rmask |= bit;
	}
	return rmask;
}

int
miimir(struct mii* mii, int r)
{
	if(mii == NULL || mii->ctlr == NULL || mii->curphy == NULL)
		return -1;
	return mii->rw(mii, 0, mii->curphy->phyno, r, 0);
}

int
miimiw(struct mii* mii, int r, int data)
{
	if(mii == NULL || mii->ctlr == NULL || mii->curphy == NULL)
		return -1;
	return mii->rw(mii, 1, mii->curphy->phyno, r, data);
}

int
miireset(struct mii* mii)
{
	int bmcr, timeo;

	if(mii == NULL || mii->ctlr == NULL || mii->curphy == NULL)
		return -1;
	bmcr = mii->rw(mii, 0, mii->curphy->phyno, Bmcr, 0);
	mii->rw(mii, 1, mii->curphy->phyno, Bmcr, BmcrR|bmcr);
	for(timeo = 0; timeo < 1000; timeo++){
		bmcr = mii->rw(mii, 0, mii->curphy->phyno, Bmcr, 0);
		if(!(bmcr & BmcrR))
			break;
		udelay(1);
	}
	if(bmcr & BmcrR)
		return -1;
	if(bmcr & BmcrI)
		mii->rw(mii, 1, mii->curphy->phyno, Bmcr, bmcr & ~BmcrI);
	return 0;
}

int
miiane(struct mii* mii, int a, int p, int e)
{
	int anar, bmsr, mscr, r, phyno;

	if(mii == NULL || mii->ctlr == NULL || mii->curphy == NULL)
		return -1;
	phyno = mii->curphy->phyno;

	mii->rw(mii, 1, phyno, Bmsr, 0);
	bmsr = mii->rw(mii, 0, phyno, Bmsr, 0);
	if(!(bmsr & BmsrAna))
		return -1;

	if(a != ~0)
		anar = (AnaTXFD|AnaTXHD|Ana10FD|Ana10HD) & a;
	else if(mii->curphy->anar != ~0)
		anar = mii->curphy->anar;
	else{
		anar = mii->rw(mii, 0, phyno, Anar, 0);
		anar &= ~(AnaAP|AnaP|AnaT4|AnaTXFD|AnaTXHD|Ana10FD|Ana10HD);
		if(bmsr & Bmsr10THD)
			anar |= Ana10HD;
		if(bmsr & Bmsr10TFD)
			anar |= Ana10FD;
		if(bmsr & Bmsr100TXHD)
			anar |= AnaTXHD;
		if(bmsr & Bmsr100TXFD)
			anar |= AnaTXFD;
	}
	mii->curphy->anar = anar;

	if(p != ~0)
		anar |= (AnaAP|AnaP) & p;
	else if(mii->curphy->fc != ~0)
		anar |= mii->curphy->fc;
	mii->curphy->fc = (AnaAP|AnaP) & anar;

	if(bmsr & BmsrEs){
		mscr = mii->rw(mii, 0, phyno, Mscr, 0);
		mscr &= ~(Mscr1000TFD|Mscr1000THD);
		if(e != ~0)
			mscr |= (Mscr1000TFD|Mscr1000THD) & e;
		else if(mii->curphy->mscr != ~0)
			mscr = mii->curphy->mscr;
		else{
			r = mii->rw(mii, 0, phyno, Esr, 0);
			if(r & Esr1000THD)
				mscr |= Mscr1000THD;
			if(r & Esr1000TFD)
				mscr |= Mscr1000TFD;
		}
		mii->curphy->mscr = mscr;
		mii->rw(mii, 1, phyno, Mscr, mscr);
	}
	else
		mii->curphy->mscr = 0;
	mii->rw(mii, 1, phyno, Anar, anar);

	r = mii->rw(mii, 0, phyno, Bmcr, 0);
	if(!(r & BmcrR)){
		r |= BmcrAne|BmcrRan;
		mii->rw(mii, 1, phyno, Bmcr, r);
	}

	return 0;
}

int
miistatus(struct mii* mii)
{
	struct miiphy *phy;
	int anlpar, bmsr, p, r, phyno;

	if(mii == NULL || mii->ctlr == NULL || mii->curphy == NULL)
		return -1;
	phy = mii->curphy;
	phyno = phy->phyno;

	/*
	 * Check Auto-Negotiation is complete and link is up.
	 * (Read status twice as the Ls bit is sticky).
	 */
	bmsr = mii->rw(mii, 0, phyno, Bmsr, 0);
	if(!(bmsr & (BmsrAnc|BmsrAna)))
		return -1;

	bmsr = mii->rw(mii, 0, phyno, Bmsr, 0);
	if(!(bmsr & BmsrLs)){
		phy->link = 0;
		return -1;
	}

	phy->speed = phy->fd = phy->rfc = phy->tfc = 0;
	if(phy->mscr){
		r = mii->rw(mii, 0, phyno, Mssr, 0);
		if((phy->mscr & Mscr1000TFD) && (r & Mssr1000TFD)){
			phy->speed = 1000;
			phy->fd = 1;
		}
		else if((phy->mscr & Mscr1000THD) && (r & Mssr1000THD))
			phy->speed = 1000;
	}

	anlpar = mii->rw(mii, 0, phyno, Anlpar, 0);
	if(phy->speed == 0){
		r = phy->anar & anlpar;
		if(r & AnaTXFD){
			phy->speed = 100;
			phy->fd = 1;
		}
		else if(r & AnaTXHD)
			phy->speed = 100;
		else if(r & Ana10FD){
			phy->speed = 10;
			phy->fd = 1;
		}
		else if(r & Ana10HD)
			phy->speed = 10;
	}
	if(phy->speed == 0)
		return -1;

	if(phy->fd){
		p = phy->fc;
		r = anlpar & (AnaAP|AnaP);
		if(p == AnaAP && r == (AnaAP|AnaP))
			phy->tfc = 1;
		else if(p == (AnaAP|AnaP) && r == AnaAP)
			phy->rfc = 1;
		else if((p & AnaP) && (r & AnaP))
			phy->rfc = phy->tfc = 1;
	}

	phy->link = 1;

	return 0;
}

char*
miidumpphy(struct mii* mii, char* p, char* e)
{
	int i, r;

	if(mii == NULL || mii->curphy == NULL)
		return p;

	p = seprintf(p, e, "phy:   ");
	for(i = 0; i < NMiiPhyr; i++){
		if(i && ((i & 0x07) == 0))
			p = seprintf(p, e, "\n       ");
		r = mii->rw(mii, 0, mii->curphy->phyno, i, 0);
		p = seprintf(p, e, " %4.4ux", r);
	}
	p = seprintf(p, e, "\n");

	return p;
}

void
miidetach(struct mii* mii)
{
	int i;

	for(i = 0; i < NMiiPhy; i++){
		if(mii->phy[i] == NULL)
			continue;
		kfree(mii);
		mii->phy[i] = NULL;
	}
	kfree(mii);
}

struct mii*
miiattach(void* ctlr, int mask, int (*rw)(struct mii*, int unused_int, int, int, int))
{
	struct mii* mii;

	if((mii = kzmalloc(sizeof(struct mii), 0)) == NULL)
		return NULL;
	mii->ctlr = ctlr;
	mii->rw = rw;

	if(miiprobe(mii, mask) == 0){
		kfree(mii);
		mii = NULL;
	}

	return mii;
}
