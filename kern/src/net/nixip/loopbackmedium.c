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

enum
{
	Maxtu=	16*1024,
};

typedef struct LB LB;
struct LB
{
	struct proc	*readp;
	struct queue	*q;
	struct fs	*f;
};

static void loopbackread(void *a);

static void
loopbackbind(struct ipifc *ifc, int unused_int, char **unused_char_pp_t)
{
	LB *lb;

	lb = kmalloc(sizeof(*lb), 0);
	lb->f = ifc->conv->p->f;
	lb->q = qopen(1024*1024, Qmsg, NULL, NULL);
	ifc->arg = lb;
	ifc->mbps = 1000;

	kproc("loopbackread", loopbackread, ifc);

}

static void
loopbackunbind(struct ipifc *ifc)
{
	LB *lb = ifc->arg;

	if(lb->readp)
		postnote(lb->readp, 1, "unbind", 0);

	/* wait for reader to die */
	while(lb->readp != 0)
		; //tsleep(&up->sleep, return0, 0, 300);

	/* clean up */
	qfree(lb->q);
	kfree(lb);
}

static void
loopbackbwrite(struct ipifc *ifc, struct block *bp, int unused_int, uint8_t *unused_uint8_p_t)
{
	LB *lb;

	lb = ifc->arg;
	if(qpass(lb->q, bp) < 0)
		ifc->outerr++;
	ifc->out++;
}

static void
loopbackread(void *a)
{
	ERRSTACK(1);
	struct ipifc *ifc;
	struct block *bp;
	LB *lb;

	ifc = a;
	lb = ifc->arg;
	lb->readp = current;	/* hide identity under a rock for unbind */
	if(waserror()){
		lb->readp = 0;
		pexit("hangup", 1);
	}
	for(;;){
		bp = qbread(lb->q, Maxtu);
		if(bp == NULL)
			continue;
		ifc->in++;
		if(!canrlock(&ifc->rwlock)){
			freeb(bp);
			continue;
		}
		if(waserror()){
			runlock(&ifc->rwlock);
			nexterror();
		}
		if(ifc->lifc == NULL)
			freeb(bp);
		else
			ipiput4(lb->f, ifc, bp);
		runlock(&ifc->rwlock);
		poperror();
	}
}

struct medium loopbackmedium =
{
.hsize=		0,
.mintu=		0,
.maxtu=		Maxtu,
.maclen=	0,
.name=		"loopback",
.bind=		loopbackbind,
.unbind=	loopbackunbind,
.bwrite=	loopbackbwrite,
};

void
loopbackmediumlink(void)
{
	addipmedium(&loopbackmedium);
}
