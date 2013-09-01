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


static void	pktbind(struct ipifc *unused_ipifc, int unused_int, char**);
static void	pktunbind(struct ipifc *unused_ipifc);
static void	pktbwrite(struct ipifc *unused_ipifc, struct block*, int unused_int, uint8_t *unused_uint8_p_t);
static void	pktin(struct fs*, struct ipifc *unused_ipifc, struct block*);

struct medium pktmedium =
{
.name=		"pkt",
.hsize=		14,
.mintu=		40,
.maxtu=		4*1024,
.maclen=	6,
.bind=		pktbind,
.unbind=	pktunbind,
.bwrite=	pktbwrite,
.pktin=		pktin,
};

/*
 *  called to bind an IP ifc to an ethernet device
 *  called with ifc wlock'd
 */
static void
pktbind(struct ipifc *unused_ipifc, int argc, char **argv)
{
}

/*
 *  called with ifc wlock'd
 */
static void
pktunbind(struct ipifc *unused_ipifc)
{
}

/*
 *  called by ipoput with a single packet to write
 */
static void
pktbwrite(struct ipifc *ifc, struct block *bp, int unused_int, uint8_t *unused_uint8_p_t)
{
	/* enqueue onto the conversation's rq */
	bp = concatblock(bp);
	if(ifc->conv->snoopers.ref > 0)
		qpass(ifc->conv->sq, copyblock(bp, BLEN(bp)));
	qpass(ifc->conv->rq, bp);
}

/*
 *  called with ifc rlocked when someone write's to 'data'
 */
static void
pktin(struct fs *f, struct ipifc *ifc, struct block *bp)
{
	if(ifc->lifc == NULL)
		freeb(bp);
	else {
		if(ifc->conv->snoopers.ref > 0)
			qpass(ifc->conv->sq, copyblock(bp, BLEN(bp)));
		ipiput4(f, ifc, bp);
	}
}

void
pktmediumlink(void)
{
	addipmedium(&pktmedium);
}
