/* Copyright (c) 2013 The Regents of the University of California
 * Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #alarm: a device for registering per-process alarms.
 *
 * Allows a process to set up alarms, which they can tap to get events at a
 * certain TSC time.
 *
 * Every process has their own alarm sets and view of #alarm; gen and friends
 * look at current's alarmset when it is time to gen or open a file.
 *
 * To use, first open #alarm/clone, and that gives you an alarm directory aN,
 * where N is ID of the alarm.  The FD you get from clone points to 'ctl.'
 *
 * 'ctl' takes no commands.  You can read it to get the ID.  That's it.
 *
 * 'timer' takes the hex string value (in absolute tsc time) to fire the alarm.
 * Writing 0 disables the alarm.  You can read 'timer' to get the next time it
 * will fire, in TSC time.  0 means it is disabled.  To find out about the timer
 * firing, put an FD tap on 'timer' for FDTAP_FILT_WRITTEN.
 *
 * 'period' takes the hex string value (in TSC ticks) for the period of the
 * alarm.  If non-zero, the alarm will rearm when it fires.  You can read the
 * period.
 *
 * Reading the 'count' file will return the number of times the alarm has
 * expired since the last read or the last write to 'timer'.  If this is 0, then
 * read() will block or EAGAIN.  You cannot write 'count'.  You can tap it for
 * FDTAP_FILT_READABLE.
 *
 * While each process has a separate view of #alarm, it is possible to post a
 * chan to Qctl or Qtimer to #srv.  If another proc has your Qtimer, it can set
 * it in the past, thereby triggering an immediate event.  More clever than
 * useful.
 *
 * Notes on refcnting (the trickier parts here):
 * - the proc_alarms have counted references to their proc
 * 	proc won't free til all alarms are closed, which is fine.  we close
 * 	all files in destroy.  if a proc drops a chan in srv, the proc will stay
 * 	alive because the alarm is alive - til that chan is closed (srvremove)
 *
 * 	other shady ways to keep a chan alive: cd to it!  if it is ., we'd
 * 	keep a ref around.  however, only alarmdir *file* grab refs, not
 * 	directories.
 *
 * - proc_alarms are kref'd, since there can be multiple chans per alarm
 * 	the only thing that keeps an alarm alive is a chan on a CTL or TIMER (or
 * 	other file).  when you cloned, you got back an open CTL, which keeps the
 * 	alarm (and the dir) alive.
 *
 * 	we need to be careful generating krefs, in case alarms are concurrently
 * 	released and removed from the lists.  just like with procs and pid2proc,
 * 	we need to sync with the source of the kref. */

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
#include <umem.h>
#include <devalarm.h>

struct dev alarmdevtab;

static char *devname(void)
{
	return alarmdevtab.name;
}

/* qid path types */
#define Qtopdir			1
#define Qclone			2
#define Qalarmdir		3
#define Qctl			4
#define Qtimer			5	/* Qctl + 1 */
#define Qperiod			6
#define Qcount			7

/* This paddr/kaddr is a bit dangerous.  it'll work so long as we don't need all
 * 64 bits for a physical address (48 is the current norm on x86_64). */
#define ADDR_SHIFT 5
#define QID2A(q) ((struct proc_alarm*)KADDR(((q).path >> ADDR_SHIFT)))
#define TYPE(q) ((q).path & ((1 << ADDR_SHIFT) - 1))
#define QID(ptr, type) ((PADDR(ptr) << ADDR_SHIFT) | type)
extern struct username eve;

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

static void alarm_fire_taps(struct proc_alarm *a, int filter)
{
	struct fd_tap *tap_i;

	SLIST_FOREACH(tap_i, &a->fd_taps, link)
		fire_tap(tap_i, filter);
}

static void proc_alarm_handler(struct alarm_waiter *a_waiter)
{
	struct proc_alarm *a = container_of(a_waiter, struct proc_alarm,
					    a_waiter);

	cv_lock(&a->cv);
	a->count++;
	if (!a->period) {
		a_waiter->wake_up_time = 0;
	} else {
		/* TODO: use an alarm helper, once we switch over to nsec */
		a_waiter->wake_up_time += a->period;
		set_alarm(a->proc->alarmset.tchain, a_waiter);
	}
	__cv_broadcast(&a->cv);
	/* Fires taps for both Qtimer and Qcount. */
	alarm_fire_taps(a, FDTAP_FILT_WRITTEN | FDTAP_FILT_READABLE);
	cv_unlock(&a->cv);
}

void devalarm_init(struct proc *p)
{
	TAILQ_INIT(&p->alarmset.list);
	spinlock_init(&p->alarmset.lock);
	/* Just running all the proc alarms on core 0. */
	p->alarmset.tchain = &per_cpu_info[0].tchain;
	p->alarmset.id_counter = 0;
}

static int alarmgen(struct chan *c, char *entry_name, struct dirtab *unused,
		    int unused_nr_dirtab, int s, struct dir *dp)
{
	struct qid q;
	struct proc_alarm *a_i;
	struct proc *p = current;

	/* Whether we're in one dir or at the top, .. still takes us to the top.
	 */
	if (s == DEVDOTDOT) {
		mkqid(&q, Qtopdir, 0, QTDIR);
		devdir(c, q, devname(), 0, eve.name, 0555, dp);
		return 1;
	}
	switch (TYPE(c->qid)) {
	case Qtopdir:
		/* Generate elements for the top level dir.  We support a clone
		 * and alarm dirs at the top level */
		if (s == 0) {
			mkqid(&q, Qclone, 0, QTFILE);
			devdir(c, q, "clone", 0, eve.name, 0666, dp);
			return 1;
		}
		s--;	/* 1 -> 0th element, 2 -> 1st element, etc */
		/* Gets the s-th element (0 index)
		 *
		 * I would like to take advantage of the state machine and our
		 * previous answer to get the sth element of the list.  We can
		 * get at our previous run of gen from dp (struct dir), and use
		 * that to get the next item.  I'd like to do something like:
		 *
		 * if (dp->qid.path >> ADDR_SHIFT)
		 *      a_i = TAILQ_NEXT(QID2A(dp->qid), link);
		 *
		 * Dev would give us a 0'd dp path on the first run, so if we
		 * have a path, we know we're on an iterative run.  However, the
		 * problem is that we could have lost the element dp refers to
		 * (QID2A(dp->qid)) since our previous run, so we can't even
		 * access that memory to check for refcnts or anything.  We need
		 * a new model for how gen works (probably a gen_start and
		 * gen_stop devop, passed as parameters to devwalk), so that we
		 * can have some invariants between gen runs.
		 *
		 * Til then, we're stuck with arrays like in #ip (though we can
		 * use Linux style fdsets) or lousy O(n^2) linked lists (like
		 * #srv).
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
		devdir(c, q, get_cur_genbuf(), 0, eve.name, 0555, dp);
		spin_unlock(&p->alarmset.lock);
		return 1;
	case Qalarmdir:
		/* Gen the contents of the alarm dirs */
		s += Qctl;	/* first time through, start on Qctl */
		switch (s) {
		case Qctl:
			mkqid(&q, QID(QID2A(c->qid), Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve.name, 0666, dp);
			return 1;
		case Qtimer:
			mkqid(&q, QID(QID2A(c->qid), Qtimer), 0, QTFILE);
			devdir(c, q, "timer", 0, eve.name, 0666, dp);
			return 1;
		case Qperiod:
			mkqid(&q, QID(QID2A(c->qid), Qperiod), 0, QTFILE);
			devdir(c, q, "period", 0, eve.name, 0666, dp);
			return 1;
		case Qcount:
			mkqid(&q, QID(QID2A(c->qid), Qcount), 0, QTFILE);
			devdir(c, q, "count", 0, eve.name, 0666, dp);
			return 1;
		}
		return -1;
		/* Need to also provide a direct hit for Qclone and all other
		 * files (at all levels of the hierarchy).  Every file is both
		 * generated (via the s increments in their respective
		 * directories) and directly gen-able.  devstat() will call gen
		 * with a specific path in the qid.  In these cases, we make a
		 * dir for whatever they are asking for.  Note the qid stays the
		 * same.  I think this is what the old plan9 comments above
		 * devgen were talking about for (ii).
		 *
		 * We don't need to do this for the directories - devstat will
		 * look for the a directory by path and fail.  Then it will
		 * manually build the stat output (check the -1 case in
		 * devstat). */
	case Qclone:
		devdir(c, c->qid, "clone", 0, eve.name, 0666, dp);
		return 1;
	case Qctl:
		devdir(c, c->qid, "ctl", 0, eve.name, 0666, dp);
		return 1;
	case Qtimer:
		devdir(c, c->qid, "timer", 0, eve.name, 0666, dp);
		return 1;
	case Qperiod:
		devdir(c, c->qid, "period", 0, eve.name, 0666, dp);
		return 1;
	case Qcount:
		devdir(c, c->qid, "count", 0, eve.name, 0666, dp);
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
				 unsigned int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, alarmgen);
}

static size_t alarmstat(struct chan *c, uint8_t *db, size_t n)
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
		a = kzmalloc(sizeof(struct proc_alarm), MEM_WAIT);
		kref_init(&a->kref, alarm_release, 1);
		SLIST_INIT(&a->fd_taps);
		cv_init(&a->cv);
		qlock_init(&a->qlock);
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
	case Qperiod:
	case Qcount:
		/* the purpose of opening is to hold a kref on the proc_alarm */
		a = QID2A(c->qid);
		assert(a);
		/* this isn't a valid pointer yet, since our chan doesn't have a
		 * ref.  since the time that walk gave our chan the qid, the
		 * chan could have been closed, and the alarm decref'd and
		 * freed.  the qid is essentially an uncounted reference, and we
		 * need to go to the source to attempt to get a real ref.
		 * Unfortunately, this is another scan of the list, same as
		 * devsrv. */
		spin_lock(&p->alarmset.lock);
		TAILQ_FOREACH(a_i, &p->alarmset.list, link) {
			if (a_i == a) {
				assert(a->proc == current);
				/* it's still possible we're not getting the
				 * ref, racing with the release method */
				if (!kref_get_not_zero(&a->kref, 1)) {
					/* lost the race; error out later */
					a_i = 0;
				}
				break;
			}
		}
		spin_unlock(&p->alarmset.lock);
		if (!a_i)
			error(EFAIL,
			      "Unable to open alarm, concurrent closing");
		break;
	}
	c->mode = openmode(omode);
	/* Assumes c is unique (can't be closed concurrently */
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void alarmclose(struct chan *c)
{
	/* There are more closes than opens.  For instance, sysstat doesn't
	 * open, but it will close the chan it got from namec.  We only want to
	 * clean up/decref chans that were actually open. */
	if (!(c->flag & COPEN))
		return;
	switch (TYPE(c->qid)) {
	case Qctl:
	case Qtimer:
	case Qperiod:
	case Qcount:
		kref_put(&QID2A(c->qid)->kref);
		break;
	}
}

/* Helper for Qcount to encapsulate timerfd. */
static long read_qcount(struct chan *c, void *ubuf, size_t n)
{
	ERRSTACK(1);
	struct proc_alarm *a = QID2A(c->qid);
	struct cv_lookup_elm cle;
	unsigned long old_count;

	if (n > sizeof(old_count))
		error(EINVAL, "timerfd buffer is too small (%llu)", n);
	/* TODO: have easily abortable CVs that don't require this mechanism. */
	cv_lock(&a->cv);
	__reg_abortable_cv(&cle, &a->cv);
	if (waserror()) {
		cv_unlock(&a->cv);
		dereg_abortable_cv(&cle);
		nexterror();
	}
	while (!a->count) {
		if (c->flag & O_NONBLOCK)
			error(EAGAIN, "#alarm count was 0");
		if (should_abort(&cle))
			error(EINTR, "syscall aborted");
		cv_wait(&a->cv);
	}
	old_count = a->count;
	a->count = 0;
	cv_unlock(&a->cv);
	dereg_abortable_cv(&cle);
	poperror();
	if (copy_to_user(ubuf, &old_count, sizeof(old_count)))
		error(EFAULT, "timerfd copy_to_user failed");
	return sizeof(old_count);
}

static size_t alarmread(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	struct proc_alarm *p_alarm;

	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qalarmdir:
		return devdirread(c, ubuf, n, 0, 0, alarmgen);
	case Qctl:
		p_alarm = QID2A(c->qid);
		/* simple reads from p_alarm shouldn't need a lock */
		return readnum(offset, ubuf, n, p_alarm->id, NUMSIZE32);
	case Qtimer:
		p_alarm = QID2A(c->qid);
		return readnum(offset, ubuf, n, p_alarm->a_waiter.wake_up_time,
					   NUMSIZE64);
	case Qperiod:
		p_alarm = QID2A(c->qid);
		return readnum(offset, ubuf, n, p_alarm->period, NUMSIZE64);
	case Qcount:
		return read_qcount(c, ubuf, n);	/* ignore offset */
	default:
		panic("Bad QID %p in devalarm", c->qid.path);
	}
	return 0;
}

/* Helper, sets the procalarm to hexval (abs TSC ticks).  0 disarms. */
static void set_proc_alarm(struct proc_alarm *a, uint64_t hexval)
{
	/* Due to how we have to maintain 'count', we need to strictly account
	 * for the firings of the alarm.  Easiest thing is to disarm it, reset
	 * everything, then rearm it.  Note that if someone is blocked on count
	 * = 0, they may still be blocked until the next time the alarm fires.
	 *
	 * unset waits on the handler, which grabs the cv lock, so we don't grab
	 * the cv lock.  However, we still need to protect ourselves from
	 * multiple setters trying to run this at once.  Unset actually can
	 * handle being called concurrently, but alarm setters can't, nor can it
	 * handle the unsets and sets getting out of sync.  For instance, two
	 * unsets followed by two sets would be a bug.  Likewise, setting the
	 * awaiter value while it is on a tchain is a bug.  The qlock prevents
	 * that. */
	qlock(&a->qlock);
	unset_alarm(a->proc->alarmset.tchain, &a->a_waiter);
	cv_lock(&a->cv);
	a->count = 0;
	if (hexval) {
		set_awaiter_abs(&a->a_waiter, hexval);
		set_alarm(a->proc->alarmset.tchain, &a->a_waiter);
	}
	cv_unlock(&a->cv);
	qunlock(&a->qlock);
}

/* Note that in read and write we have an open chan, which means we have an
 * active kref on the p_alarm.  Also note that we make no assumptions about
 * current here - we find the proc (and the tchain) via the ref stored in the
 * proc_alarm. */
static size_t alarmwrite(struct chan *c, void *ubuf, size_t n, off64_t unused)
{
	struct proc_alarm *p_alarm;

	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qalarmdir:
	case Qctl:
	case Qcount:
		error(EPERM, ERROR_FIXME);
	case Qtimer:
		set_proc_alarm(QID2A(c->qid), strtoul_from_ubuf(ubuf, n, 16));
		break;
	case Qperiod:
		p_alarm = QID2A(c->qid);
		/* racing with the handler which checks the val repeatedly */
		cv_lock(&p_alarm->cv);
		p_alarm->period = strtoul_from_ubuf(ubuf, n, 16);
		cv_unlock(&p_alarm->cv);
		break;
	default:
		panic("Bad QID %p in devalarm", c->qid.path);
	}
	return n;
}

/* We use the same tap list, regardless of Qtimer or Qcount */
static int tap_alarm(struct proc_alarm *a, struct fd_tap *tap, int cmd,
                     int legal_filter)
{
	int ret;

	if (tap->filter & ~legal_filter) {
		set_error(ENOSYS, "Unsupported #%s tap %p, must be %p",
			  devname(), tap->filter, legal_filter);
		return -1;
	}
	cv_lock(&a->cv);
	switch (cmd) {
	case (FDTAP_CMD_ADD):
		SLIST_INSERT_HEAD(&a->fd_taps, tap, link);
		ret = 0;
		break;
	case (FDTAP_CMD_REM):
		SLIST_REMOVE(&a->fd_taps, tap, fd_tap, link);
		ret = 0;
		break;
	default:
		set_error(ENOSYS, "Unsupported #%s tap command %p",
				  devname(), cmd);
		ret = -1;
	}
	cv_unlock(&a->cv);
	return ret;
}

static int alarm_tapfd(struct chan *c, struct fd_tap *tap, int cmd)
{
	struct proc_alarm *a = QID2A(c->qid);

	/* We don't actually support HANGUP, but epoll implies it. */
	#define ALARM_LEGAL_TIMER_TAPS (FDTAP_FILT_WRITTEN | FDTAP_FILT_HANGUP)
	#define ALARM_LEGAL_COUNT_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_HANGUP)

	switch (TYPE(c->qid)) {
	case Qtimer:
		return tap_alarm(a, tap, cmd, ALARM_LEGAL_TIMER_TAPS);
	case Qcount:
		return tap_alarm(a, tap, cmd, ALARM_LEGAL_COUNT_TAPS);
	default:
		set_error(ENOSYS, "Can't tap #%s file type %d", devname(),
		          c->qid.path);
		return -1;
	}
}

static char *alarm_chaninfo(struct chan *ch, char *ret, size_t ret_l)
{
	struct proc_alarm *a;
	struct timespec ts;

	switch (TYPE(ch->qid)) {
	case Qctl:
	case Qtimer:
	case Qperiod:
	case Qcount:
		a = QID2A(ch->qid);
		ts = tsc2timespec(a->a_waiter.wake_up_time);
		snprintf(ret, ret_l,
		         "Id %d, %s, expires [%7d.%09d] (%p), period %llu, count %llu",
		         a->id,
		         SLIST_EMPTY(&a->fd_taps) ? "untapped" : "tapped",
		         ts.tv_sec, ts.tv_nsec, a->a_waiter.wake_up_time,
		         a->period, a->count);
		break;
	default:
		return devchaninfo(ch, ret, ret_l);
	}
	return ret;
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
	.create = devcreate,
	.close = alarmclose,
	.read = alarmread,
	.bread = devbread,
	.write = alarmwrite,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = alarm_chaninfo,
	.tapfd = alarm_tapfd,
};
