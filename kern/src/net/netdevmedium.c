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

static void	netdevbind(struct Ipifc *ifc, int argc, char **argv);
static void	netdevunbind(struct Ipifc *ifc);
static void	netdevbwrite(struct Ipifc *ifc, struct block *bp, int version,
				uint8_t *ip);
static void	netdevread(void *a);

typedef struct	Netdevrock Netdevrock;
struct Netdevrock
{
	struct Fs	*f;		/* file system we belong to */
	struct proc	*readp;		/* reading process */
	struct chan	*mchan;		/* Data channel */
};

struct medium netdevmedium =
{
.name=		"netdev",
.hsize=		0,
.mintu=	0,
.maxtu=	64000,
.maclen=	0,
.bind=		netdevbind,
.unbind=	netdevunbind,
.bwrite=	netdevbwrite,
.unbindonclose=	0,
};

/*
 *  called to bind an IP ifc to a generic network device
 *  called with ifc qlock'd
 */
static void
netdevbind(struct Ipifc *ifc, int argc, char **argv)
{
	struct chan *mchan;
	Netdevrock *er;

	if(argc < 2)
		error(Ebadarg);

	mchan = namec(argv[2], Aopen, ORDWR, 0);

	er = kzmalloc(sizeof(*er), 0);
	er->mchan = mchan;
	er->f = ifc->conv->p->f;

	ifc->arg = er;

	ktask("netdevread", netdevread, ifc);
}

/*
 *  called with ifc wlock'd
 */
static void
netdevunbind(struct Ipifc *ifc)
{
	Netdevrock *er = ifc->arg;
/*
	if(er->readp != NULL)
		postnote(er->readp, 1, "unbind", 0);
*/
	printk("%s is messed up, shouldn't track procs\n", __FUNCTION__);
	/* wait for readers to die */
	while(er->readp != NULL)
		udelay_sched(300 * 1000);

	if(er->mchan != NULL)
		cclose(er->mchan);

	kfree(er);
}

/*
 *  called by ipoput with a single block to write
 */
static void
netdevbwrite(struct Ipifc *ifc, struct block *bp, int unused_int, uint8_t *unused_uint8_p_t)
{
	Netdevrock *er = ifc->arg;

	if(bp->next)
		bp = concatblock(bp);
	if(BLEN(bp) < ifc->mintu)
		bp = adjustblock(bp, ifc->mintu);

	devtab[er->mchan->type]->bwrite(er->mchan, bp, 0);
	ifc->out++;
}

/*
 *  process to read from the device
 */
static void
netdevread(void *a)
{
	ERRSTACK(2);
	struct Ipifc *ifc;
	struct block *bp;
	Netdevrock *er;
	char *argv[1];

	ifc = a;
	er = ifc->arg;
	er->readp = current;	/* hide identity under a rock for unbind */
	if(waserror()){
		er->readp = NULL;
		warn("netdevread returns unexpectedly");
		poperror();
		return;
	}
	for(;;){
		bp = devtab[er->mchan->type]->bread(er->mchan, ifc->maxtu, 0);
		if(bp == NULL){
			/*
			 * get here if mchan is a pipe and other side hangs up
			 * clean up this interface & get out
ZZZ is this a good idea?  (watch your errors btw)
			 */
			poperror();
			er->readp = NULL;
			argv[0] = "unbind";
			if(!waserror())
				ifc->conv->p->ctl(ifc->conv, argv, 1);
			poperror();
			warn("netdevread returns unexpectedly");
			return;
		}
		if(!canrlock(&ifc->rwlock)){
			freeb(bp);
			continue;
		}
		if(waserror()){
			runlock(&ifc->rwlock);
			nexterror();
		}
		ifc->in++;
		if(ifc->lifc == NULL)
			freeb(bp);
		else
			ipiput4(er->f, ifc, bp);
		runlock(&ifc->rwlock);
		poperror();
	}
	poperror();
}

void
netdevmediumlink(void)
{
	addipmedium(&netdevmedium);
}
