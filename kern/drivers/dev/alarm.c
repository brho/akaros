/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #alarm: a device for registering per-process alarms.
 *
 * Allows a process to set up alarms, where the kernel will send an event to a
 * posted ev_q at a certain TSC time.
 *
 * Every process has their own alarm sets and view of #alarm; gen and friends
 * look at current's alarmset when it is time to gen or open a file.
 *
 * To use, first open #alarm/clone, and that gives you an alarm directory aN,
 * where N is ID of the alarm.  ctl takes two commands: "evq POINTER" (to tell
 * the kernel the pointer of your ev_q) and "cancel", to stop an alarm.  timer
 * takes just the value (in absolute tsc time) to fire the alarm.
 *
 * While each process has a separate view of #alarm, it is possible to post a
 * chan to Qctl or Qtimer to #srv.  If another proc has your Qtimer, it can set
 * it in the past, thereby triggering an immediate event.  More clever than
 * useful.
 *
 * Notes on refcnting (the trickier parts here):
 * - the proc_alarms have counted references to their proc
 * 		proc won't free til all alarms are closed, which is fine.  we close
 * 		all files in destroy.  if a proc drops a chan in srv, the proc will stay
 * 		alive because the alarm is alive - til that chan is closed (srvremove)
 *
 * 		other shady ways to keep a chan alive: cd to it!  if it is ., we'd
 * 		keep a ref around.  however, only alarmdir *file* grab refs, not
 * 		directories.
 *
 * - proc_alarms are kref'd, since there can be multiple chans per alarm
 * 		the only thing that keeps an alarm alive is a chan on a CTL or TIMER (or
 * 		other file).  when you cloned, you got back an open CTL, which keeps the
 * 		alarm (and the dir) alive.
 *
 * 		we need to be careful generating krefs, in case alarms are concurrently
 * 		released and removed from the lists.  just like with procs and pid2proc,
 * 		we need to sync with the source of the kref. */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <sys/queue.h>
#include <smp.h>
#include <kref.h>
#include <atomic.h>
#include <alarm.h>
#include <event.h>
#include <umem.h>
#include <devalarm.h>

struct dev alarmdevtab;

static char *devname(void)
{
	return alarmdevtab.name;
}

/* qid path types */
#define Qtopdir					1
#define Qclone					2
#define Qalarmdir				3
#define Qctl					4
#define Qtimer					5	/* Qctl + 1 */

/* This paddr/kaddr is a bit dangerous.  it'll work so long as we don't need all
 * 64 bits for a physical address (48 is the current norm on x86_64). */
#define ADDR_SHIFT 5
#define QID2A(q) ((struct proc_alarm*)KADDR(((q).path >> ADDR_SHIFT)))
#define TYPE(q) ((q).path & ((1 << ADDR_SHIFT) - 1))
#define QID(ptr, type) ((PADDR(ptr) << ADDR_SHIFT) | type)
extern char *eve;

static void alarm_release(struct kref *kref)
{
	struct proc_alarm *a = container_of(kref, struct proc_alarm, kref);
	struct proc *p = a->proc;
	assert(p);
	spin_lock(&p->alarmset.lock);
	TAILQ_REMOVE(&p->alarmset.list, a, link);
	spin_unlock(&p->alarmset.lock);
	/* When this returns, the alarm has either fired or it never will */
	unset_alarm(p->alarmset.tchain, &a->a_waiter);
	proc_decref(p);
	kfree(a);
}

static void proc_alarm_handler(struct alarm_waiter *a_waiter)
{
	struct proc_alarm *a = container_of(a_waiter, struct proc_alarm, a_waiter);
	struct event_queue *ev_q = ACCESS_ONCE(a->ev_q);
	struct event_msg msg;
	if (!ev_q || !a->proc) {
		printk("[kernel] proc_alarm, bad ev_q %p or proc %p\n", ev_q, a->proc);
		return;
	}
	memset(&msg, 0, sizeof(struct event_msg));
	msg.ev_type = EV_ALARM;
	msg.ev_arg2 = a->id;
	send_event(a->proc, ev_q, &msg, 0);
}

void devalarm_init(struct proc *p)
{
	TAILQ_INIT(&p->alarmset.list);
	spinlock_init(&p->alarmset.lock);
	/* Just running all the proc alarms on core 0. */
	p->alarmset.tchain = &per_cpu_info[0].tchain;
	p->alarmset.id_counter = 0;
}

static int alarmgen(struct chan *c, char *entry_name,
					struct dirtab *unused, int unused_nr_dirtab,
					int s, struct dir *dp)
{
	struct qid q;
	struct proc_alarm *a_i;
	struct proc *p = current;
	/* Whether we're in one dir or at the top, .. still takes us to the top. */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, q, devname(), 0, eve, 0555, dp);
		return 1;
	}
	switch (TYPE(c->qid)) {
		case Qtopdir:
			/* Generate elements for the top level dir.  We support a clone and
			 * alarm dirs at the top level */
			if (s == 0) {
				mkqid(&q, Qclone, 0, QTFILE);
				devdir(c, q, "clone", 0, eve, 0666, dp);
				return 1;
			}
			s--;	/* 1 -> 0th element, 2 -> 1st element, etc */
			/* Gets the s-th element (0 index)
			 * 
			 * I would like to take advantage of the state machine and our
			 * previous answer to get the sth element of the list.  We can get
			 * at our previous run of gen from dp (struct dir), and use that to
			 * get the next item.  I'd like to do something like:
			 *
			 * if (dp->qid.path >> ADDR_SHIFT)
			 *      a_i = TAILQ_NEXT(QID2A(dp->qid), link);
			 *
			 * Dev would give us a 0'd dp path on the first run, so if we have a
			 * path, we know we're on an iterative run.  However, the problem is
			 * that we could have lost the element dp refers to (QID2A(dp->qid))
			 * since our previous run, so we can't even access that memory to
			 * check for refcnts or anything.  We need a new model for how gen
			 * works (probably a gen_start and gen_stop devop, passed as
			 * parameters to devwalk), so that we can have some invariants
			 * between gen runs.
			 *
			 * Til then, we're stuck with arrays like in #ip (though we can use
			 * Linux style fdsets) or lousy O(n^2) linked lists (like #srv).
			 *
			 * Note that we won't always start a gen loop with s == 0
			 * (devdirread, for instance) */
			spin_lock(&p->alarmset.lock);
			TAILQ_FOREACH(a_i, &p->alarmset.list, link) {
				if (s-- == 0)
					break;
			}
			/* As soon as we unlock, someone could free a_i */
			if (!a_i) {
				spin_unlock(&p->alarmset.lock);
				return -1;
			}
			snprintf(get_cur_genbuf(), GENBUF_SZ, "a%d", a_i->id);
			mkqid(&q, QID(a_i, Qalarmdir), 0, QTDIR);
			devdir(c, q, get_cur_genbuf(), 0, eve, 0555, dp);
			spin_unlock(&p->alarmset.lock);
			return 1;
		case Qalarmdir:
			/* Gen the contents of the alarm dirs */
			s += Qctl;	/* first time through, start on Qctl */
			switch (s) {
				case Qctl:
					mkqid(&q, QID(QID2A(c->qid), Qctl), 0, QTFILE);
					devdir(c, q, "ctl", 0, eve, 0666, dp);
					return 1;
				case Qtimer:
					mkqid(&q, QID(QID2A(c->qid), Qtimer), 0, QTFILE);
					devdir(c, q, "timer", 0, eve, 0666, dp);
					return 1;
			}
			return -1;
			/* Need to also provide a direct hit for Qclone and all other files
			 * (at all levels of the hierarchy).  Every file is both generated
			 * (via the s increments in their respective directories) and
			 * directly gen-able.  devstat() will call gen with a specific path
			 * in the qid.  In these cases, we make a dir for whatever they are
			 * asking for.  Note the qid stays the same.  I think this is what
			 * the old plan9 comments above devgen were talking about for (ii).
			 *
			 * We don't need to do this for the directories - devstat will look
			 * for the a directory by path and fail.  Then it will manually
			 * build the stat output (check the -1 case in devstat). */
		case Qclone:
			devdir(c, c->qid, "clone", 0, eve, 0666, dp);
			return 1;
		case Qctl:
			devdir(c, c->qid, "ctl", 0, eve, 0666, dp);
			return 1;
		case Qtimer:
			devdir(c, c->qid, "timer", 0, eve, 0666, dp);
			return 1;
	}
	return -1;
}

static void alarminit(void)
{
}

static struct chan *alarmattach(char *spec)
{
	struct chan *c = devattach(devname(), spec);
	mkqid(&c->qid, Qtopdir, 0, QTDIR);
	return c;
}

static struct walkqid *alarmwalk(struct chan *c, struct chan *nc, char **name,
								 int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, alarmgen);
}

static int alarmstat(struct chan *c, uint8_t * db, int n)
{
	return devstat(c, db, n, 0, 0, alarmgen);
}

/* It shouldn't matter if p = current is DYING.  We'll eventually fail to insert
 * the open chan into p's fd table, then decref the chan. */
static struct chan *alarmopen(struct chan *c, int omode)
{
	struct proc *p = current;
	struct proc_alarm *a, *a_i;
	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qalarmdir:
			if (omode & O_REMCLO)
				error(EPERM, ERROR_FIXME);
			if (omode & O_WRITE)
				error(EISDIR, ERROR_FIXME);
			break;
		case Qclone:
			a = kzmalloc(sizeof(struct proc_alarm), KMALLOC_WAIT);
			kref_init(&a->kref, alarm_release, 1);
			init_awaiter(&a->a_waiter, proc_alarm_handler);
			spin_lock(&p->alarmset.lock);
			a->id = p->alarmset.id_counter++;
			proc_incref(p, 1);
			a->proc = p;
			TAILQ_INSERT_TAIL(&p->alarmset.list, a, link);
			spin_unlock(&p->alarmset.lock);
			mkqid(&c->qid, QID(a, Qctl), 0, QTFILE);
			break;
		case Qctl:
		case Qtimer:
			/* the purpose of opening is to hold a kref on the proc_alarm */
			a = QID2A(c->qid);
			assert(a);
			/* this isn't a valid pointer yet, since our chan doesn't have a
			 * ref.  since the time that walk gave our chan the qid, the chan
			 * could have been closed, and the alarm decref'd and freed.  the
			 * qid is essentially an uncounted reference, and we need to go to
			 * the source to attempt to get a real ref.  Unfortunately, this is
			 * another scan of the list, same as devsrv. */
			spin_lock(&p->alarmset.lock);
			TAILQ_FOREACH(a_i, &p->alarmset.list, link) {
				if (a_i == a) {
					assert(a->proc == current);
					/* it's still possible we're not getting the ref, racing
					 * with the release method */
					if (!kref_get_not_zero(&a->kref, 1)) {
						a_i = 0;	/* lost the race, will error out later */
					}
					break;
				}
			}
			spin_unlock(&p->alarmset.lock);
			if (!a_i)
				error(EFAIL, "Unable to open alarm, concurrent closing");
			break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void alarmcreate(struct chan *c, char *name, int omode, uint32_t perm)
{
	error(EPERM, ERROR_FIXME);
}

static void alarmremove(struct chan *c)
{
	error(EPERM, ERROR_FIXME);
}

static int alarmwstat(struct chan *c, uint8_t * dp, int n)
{
	error(EFAIL, "No alarmwstat");
	return 0;
}

static void alarmclose(struct chan *c)
{
	/* There are more closes than opens.  For instance, sysstat doesn't open,
	 * but it will close the chan it got from namec.  We only want to clean
	 * up/decref chans that were actually open. */
	if (!(c->flag & COPEN))
		return;
	switch (TYPE(c->qid)) {
		case Qctl:
		case Qtimer:
			kref_put(&QID2A(c->qid)->kref);
			break;
	}
}

static long alarmread(struct chan *c, void *ubuf, long n, int64_t offset)
{
	struct proc_alarm *p_alarm;
	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qalarmdir:
			return devdirread(c, ubuf, n, 0, 0, alarmgen);
		case Qctl:
			p_alarm = QID2A(c->qid);
			return readnum(offset, ubuf, n, p_alarm->id, NUMSIZE32);
		case Qtimer:
			p_alarm = QID2A(c->qid);
			return readnum(offset, ubuf, n, p_alarm->a_waiter.wake_up_time,
						   NUMSIZE64);
		default:
			panic("Bad QID %p in devalarm", c->qid.path);
	}
	return 0;
}

/* Note that in read and write we have an open chan, which means we have an
 * active kref on the p_alarm.  Also note that we make no assumptions about
 * current here - we find the proc (and the tchain) via the ref stored in the
 * proc_alarm. */
static long alarmwrite(struct chan *c, void *ubuf, long n, int64_t unused)
{
	ERRSTACK(1);
	char num64[NUMSIZE64];
	struct cmdbuf *cb;
	struct proc_alarm *p_alarm;
	uint64_t hexval;

	switch (TYPE(c->qid)) {
		case Qtopdir:
		case Qalarmdir:
			error(EPERM, ERROR_FIXME);
		case Qctl:
			p_alarm = QID2A(c->qid);
			cb = parsecmd(ubuf, n);
			if (waserror()) {
				kfree(cb);
				nexterror();
			}
			if (cb->nf < 1)
				error(EFAIL, "short control request");
			if (!strcmp(cb->f[0], "evq")) {
				if (cb->nf < 2)
					error(EFAIL, "evq needs a pointer");
				/* i think it's safe to do a stroul on a parsecmd.  it's kernel
				 * memory, and space or 0 terminated */
				hexval = strtoul(cb->f[1], 0, 16);
				/* This is just to help userspace - event code can handle it */
				if (!is_user_rwaddr((void *)hexval, sizeof(struct event_queue)))
					error(EFAIL, "Non-user ev_q pointer");
				p_alarm->ev_q = (struct event_queue *)hexval;
			} else if (!strcmp(cb->f[0], "cancel")) {
				unset_alarm(p_alarm->proc->alarmset.tchain, &p_alarm->a_waiter);
			} else {
				error(EFAIL, "%s: not implemented", cb->f[0]);
			}
			kfree(cb);
			poperror();
			break;
		case Qtimer:
			/* want to give strtoul a null-terminated buf (can't handle random
			 * user strings) */
			if (n > sizeof(num64)) {
				set_errno(EINVAL);
				error(EFAIL, "attempted to write %d chars, max %d", n,
					  sizeof(num64));
			}
			memcpy(num64, ubuf, n);
			num64[n] = 0;	/* enforce trailing 0 */
			hexval = strtoul(num64, 0, 16);
			p_alarm = QID2A(c->qid);
			/* if you don't know if it was running or not, resetting will turn
			 * it on regardless. */
			reset_alarm_abs(p_alarm->proc->alarmset.tchain, &p_alarm->a_waiter,
							hexval);
			break;
		default:
			panic("Bad QID %p in devalarm", c->qid.path);
	}
	return n;
}

struct dev alarmdevtab __devtab = {
	.name = "alarm",

	.reset = devreset,
	.init = alarminit,
	.shutdown = devshutdown,
	.attach = alarmattach,
	.walk = alarmwalk,
	.stat = alarmstat,
	.open = alarmopen,
	.create = alarmcreate,
	.close = alarmclose,
	.read = alarmread,
	.bread = devbread,
	.write = alarmwrite,
	.bwrite = devbwrite,
	.remove = alarmremove,
	.wstat = alarmwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};
