// INFERNO
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

struct IProuter iprouter;

/*
 *  User level routing.  Ip packets we don't know what to do with
 *  come here.
 */
void useriprouter(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	qlock(&(&f->iprouter)->qlock);
	if (f->iprouter.q != NULL) {
		bp = padblock(bp, IPaddrlen);
		if (bp == NULL)
			return;
		ipmove(bp->rp, ifc->lifc->local);
		qpass(f->iprouter.q, bp);
	} else
		freeb(bp);
	qunlock(&(&f->iprouter)->qlock);
}

void iprouteropen(struct Fs *f)
{
	qlock(&(&f->iprouter)->qlock);
	f->iprouter.opens++;
	if (f->iprouter.q == NULL)
		f->iprouter.q = qopen(64 * 1024, 0, 0, 0);
	else if (f->iprouter.opens == 1)
		qreopen(f->iprouter.q);
	qunlock(&(&f->iprouter)->qlock);
}

void iprouterclose(struct Fs *f)
{
	qlock(&(&f->iprouter)->qlock);
	f->iprouter.opens--;
	if (f->iprouter.opens == 0)
		qclose(f->iprouter.q);
	qunlock(&(&f->iprouter)->qlock);
}

long iprouterread(struct Fs *f, void *a, int n)
{
	return qread(f->iprouter.q, a, n);
}
