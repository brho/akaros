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
#include <trap.h>

enum {
	Maxtu = 16 * 1024,
};

typedef struct LB LB;
struct LB {
	struct proc *readp;
	struct queue *q;
	struct fs *f;
};

static void loopbackread(uint32_t core, long a0, long a1, long a2);

static void
loopbackbind(struct ipifc *ifc, int unused_int, char **unused_char_pp_t)
{
	LB *lb;

	lb = kzmalloc(sizeof(*lb), 0);
	lb->f = ifc->conv->p->f;
	lb->q = qopen(1024 * 1024, Qmsg, NULL, NULL);
	if (!lb->q)
		panic("loopbackbind");
	ifc->arg = lb;
	ifc->mbps = 1000;

	send_kernel_message(core_id(), loopbackread, (long)ifc, 0, 0,
			     KMSG_ROUTINE);
	/*
	loopbackread(core_id(), (long)ifc, 0, 0);
 	*/
}

static void loopbackunbind(struct ipifc *ifc)
{
	LB *lb = ifc->arg;

	if (lb->readp)
		postnote(lb->readp, 1, "unbind", 0);

	/* wait for reader to die */
	while (lb->readp != 0) ;	//tsleep(&up->sleep, return0, 0, 300);

	/* clean up */
	qfree(lb->q);
	kfree(lb);
}

static void
loopbackbwrite(struct ipifc *ifc, struct block *bp, int unused_int,
			   uint8_t * unused_uint8_p_t)
{
	LB *lb;

	lb = ifc->arg;
	if (qpass(lb->q, bp) < 0)
		ifc->outerr++;
	ifc->out++;
}

static void loopbackread(uint32_t core, long a0, long a1, long a2)
{
	ERRSTACK(2);
	/* loopback read can only panic ... not exit as in Plan 9 */
	struct ipifc *ifc;
	struct block *bp;
	LB *lb;

printk("loopbackread: %08lx, %08lx, %08lx, %08lx\n", 
core, a0, a1, a2);
	ifc = (void *)a0;
	lb = ifc->arg;
	lb->readp = current;	/* hide identity under a rock for unbind */
	if (waserror()) {
		lb->readp = 0;
		panic("loopbackread:");
	}
	for (;;) {
I_AM_HERE;
		bp = qbread(lb->q, Maxtu);
printk("bp %p\n", bp);
I_AM_HERE;
		if (bp == NULL)
			continue;
		ifc->in++;
I_AM_HERE;
		if (!canrlock(&ifc->rwlock)) {
I_AM_HERE;
			freeb(bp);
			continue;
		}
I_AM_HERE;
		if (waserror()) {
I_AM_HERE;
			runlock(&ifc->rwlock);
			nexterror();
		}
I_AM_HERE;
		if (ifc->lifc == NULL)
			freeb(bp);
		else
			ipiput4(lb->f, ifc, bp);
I_AM_HERE;
		runlock(&ifc->rwlock);
		poperror();
I_AM_HERE;
	}
}

struct medium loopbackmedium = {
	.hsize = 0,
	.mintu = 0,
	.maxtu = Maxtu,
	.maclen = 0,
	.name = "loopback",
	.bind = loopbackbind,
	.unbind = loopbackunbind,
	.bwrite = loopbackbwrite,
};

void loopbackmediumlink(void)
{
	addipmedium(&loopbackmedium);
}
