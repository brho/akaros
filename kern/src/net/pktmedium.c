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

static void pktbind(struct Ipifc *i, int unused_int, char **unused_char_pp_t);
static void pktunbind(struct Ipifc *i);
static void pktbwrite(struct Ipifc *i, struct block *, int unused_int,
					  uint8_t * unused_uint8_p_t);
static void pktin(struct Fs *f, struct Ipifc *i, struct block *b);

struct medium pktmedium = {
	.name = "pkt",
	.hsize = 14,
	.mintu = 40,
	.maxtu = 4 * 1024,
	.maclen = 6,
	.bind = pktbind,
	.unbind = pktunbind,
	.bwrite = pktbwrite,
	.pktin = pktin,
	.unbindonclose = 1,
};

/*
 *  called to bind an IP ifc to an ethernet device
 *  called with ifc wlock'd
 */
static void pktbind(struct Ipifc *i, int unused_int, char **unused_char_pp_t)
{
}

/*
 *  called with ifc wlock'd
 */
static void pktunbind(struct Ipifc *i)
{
}

/*
 *  called by ipoput with a single packet to write
 */
static void
pktbwrite(struct Ipifc *ifc, struct block *bp, int unused_int,
		  uint8_t * unused_uint8_p_t)
{
	/* enqueue onto the conversation's rq */
	bp = concatblock(bp);
	if (atomic_read(&ifc->conv->snoopers) > 0)
		qpass(ifc->conv->sq, copyblock(bp, BLEN(bp)));
	qpass(ifc->conv->rq, bp);
}

/*
 *  called with ifc rlocked when someone write's to 'data'
 */
static void pktin(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	if (ifc->lifc == NULL)
		freeb(bp);
	else {
		if (atomic_read(&ifc->conv->snoopers) > 0)
			qpass(ifc->conv->sq, copyblock(bp, BLEN(bp)));
		ipiput4(f, ifc, bp);
	}
}

void pktmediumlink(void)
{
	addipmedium(&pktmedium);
}
