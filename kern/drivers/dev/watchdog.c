/* Copyright (c) 2020 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #watchdog
 */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>
#include <error.h>

#include <stdio.h>
#include <arch/console.h>

/* The usage of the HPET is so hokey that I don't want it in a header in
 * include/ */
#include "../timers/hpet.h"

/* Primitive ktask control.  Probably want better general support for this
 * stuff, maybe including rendez or something to kick us out of a sleep.
 * kthread_usleep() has a built-in rendez already, so it's almost there. */
struct wd_ctl {
	bool should_exit;
};

/* lock-protected invariants
 * ----------
 * creation and manipulation of the hpet_timer ht
 *
 * if enabled is set:
 *	- cur_wd is set (the ktask responsible for updating the hpet)
 *	- timeout is set once and unchanged
 *	- there may be an old ktask with their own ctl, but it is set to
 *	should_exit.
 *	- ht was already created and initialized
 * if disabled:
 *	- global cur_wd is NULL
 *	- timeout is zero
 *	- any previously running ktask's should_exit is true
 *
 * on the edges:
 * ----------
 * disabled->enabled: ktask is kicked, it'll turn on the timer
 * enabled->disabled: ktask is told to die, we turn off the timer
 */
static spinlock_t lock = SPINLOCK_INITIALIZER;
static bool enabled;
static struct wd_ctl *cur_wd;
static uint64_t timeout;
static struct hpet_timer *ht;

struct dev watchdog_devtab;

static char *devname(void)
{
	return watchdog_devtab.name;
}

enum {
	Qdir,
	Qctl,
};

static struct dirtab wd_dir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"ctl", {Qctl, 0, QTFILE}, 0, 0666},
};

static struct chan *wd_attach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *wd_walk(struct chan *c, struct chan *nc, char **name,
			       unsigned int nname)
{
	return devwalk(c, nc, name, nname, wd_dir, ARRAY_SIZE(wd_dir),
		       devgen);
}

static size_t wd_stat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, wd_dir, ARRAY_SIZE(wd_dir), devgen);
}

static struct chan *wd_open(struct chan *c, int omode)
{
	return devopen(c, omode, wd_dir, ARRAY_SIZE(wd_dir), devgen);
}

static void wd_close(struct chan *c)
{
	if (!(c->flag & COPEN))
		return;
}

static size_t wd_read(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, ubuf, n, wd_dir, ARRAY_SIZE(wd_dir),
				  devgen);
	case Qctl:
		if (READ_ONCE(enabled))
			return readstr(offset, ubuf, n, "on");
		else
			return readstr(offset, ubuf, n, "off");
	default:
		panic("Bad Qid %p!", c->qid.path);
	}
	return -1;
}

/* do_nmi_work() call this directly.  We don't have IRQ handlers for NMIs, and
 * this will get called on *every* NMI, since we're basically muxing in SW. */
void __watchdog_nmi_handler(struct hw_trapframe *hw_tf)
{
	/* It's not enough to check 'enabled', since we get the spurious IRQ at
	 * some point after we call hpet_timer_enable().  We could attempt to
	 * deal with this by enabling the timer, waiting a bit in case the IRQ
	 * fires (which it might not, so we don't know how long to wait), and
	 * *then* setting enabled.  With barriers.  Fun. */
	if (!READ_ONCE(enabled))
		return;
	if (hpet_check_spurious_64(ht))
		return;

	/* This is real hokey, and could easily trigger another deadlock. */
	panic_skip_console_lock = true;
	panic_skip_print_lock = true;
	print_trapframe(hw_tf);
	backtrace_hwtf(hw_tf);

	printk("Watchdog forcing a reboot in 10 sec!\n");
	udelay(10000000);

	reboot();
}

/* Attempts to set up a timer.  Returns 0 on failure.  Returns the actual
 * timeout to use.  i.e. if we're limited by the timer's reach. */
static uint64_t __init_timer_once(uint64_t sec_timeout)
{
	uint64_t max;

	if (!ht) {
		ht = hpet_get_magic_timer();
		if (!ht)
			return 0;
		/* NMI mode.  Vector is ignored, but passing 2 for clarity.  If
		 * you try a regular vector/IRQ, you'll need to hook up an
		 * irq_handler.  (EOIs, handlers, etc). */
		hpet_magic_timer_setup(ht, 2, 0x4);
	}
	/* We use a 64 bit counter, so the reach32 is a little excessive.
	 * However, we need some limit to avoid wraparound.  Might as well use
	 * the 32 bit one, in case we ever sort out the HPET spurious crap. */
	max = ht->hpb->reach32 / 2;
	if (max < sec_timeout) {
		trace_printk("Watchdog request for %d, throttled to %d\n",
			     sec_timeout, max);
		return max;
	}
	return sec_timeout;
}

static void __shutoff_timer(void)
{
	hpet_timer_disable(ht);
}

static void __increment_timer(uint64_t two_x_timeout)
{
	hpet_timer_increment_comparator(ht, two_x_timeout * 1000000000);
	hpet_timer_enable(ht);
}

/* Our job is to kick the watchdog by periodically adjusting the interrupt
 * deadline in the timer into the future.  When we execute, we set it for
 * 2 * timeout more time, based on whatever it is at - not based on our runtime.
 * We'll sleep for timeout.  If we get delayed by another timeout and fail to
 * reset it, the IRQ will fire and we'll reboot.  Technically we could be held
 * up for 2 * timeout before kicking, but we were held up for at least one
 * timeout.
 *
 * It's mostly OK to have multiple of these ktasks running - that can happen if
 * you do multiple off-ons quickly.  (i.e. start a new one before the old one
 * had a chance to shut down).  Each thread has its own control structure, so
 * that's fine.  They will stop (if instructed) before doing anything.  These
 * threads will sit around though, until their timeout.  We don't have any easy
 * support for kicking a ktask to make it wake up faster. */
static void wd_ktask(void *arg)
{
	struct wd_ctl *ctl = arg;
	uint64_t sleep_usec;

	while (1) {
		spin_lock(&lock);
		if (ctl->should_exit) {
			spin_unlock(&lock);
			break;
		}
		if (!timeout) {
			/* We should have been told to exit already. */
			warn("WD saw timeout == 0!");
			spin_unlock(&lock);
			break;
		}
		__increment_timer(timeout * 2);
		sleep_usec = timeout * 1000000;
		spin_unlock(&lock);
		kthread_usleep(sleep_usec);
	}
	kfree(ctl);
}

#define WD_CTL_USAGE "on SEC_TIMEOUT | off"

static void wd_ctl_cmd(struct chan *c, struct cmdbuf *cb)
{
	struct wd_ctl *ctl;
	unsigned long sec_timeout;

	if (cb->nf < 1)
		error(EFAIL, WD_CTL_USAGE);

	if (!strcmp(cb->f[0], "on")) {
		if (cb->nf < 2)
			error(EFAIL, WD_CTL_USAGE);
		sec_timeout = strtoul(cb->f[1], 0, 0);
		if (!sec_timeout)
			error(EFAIL, "need a non-zero timeout");
		ctl = kzmalloc(sizeof(struct wd_ctl), MEM_WAIT);
		spin_lock(&lock);
		if (enabled) {
			spin_unlock(&lock);
			kfree(ctl);
			error(EFAIL, "watchdog already running; stop it first");
		}
		sec_timeout = __init_timer_once(sec_timeout);
		if (!sec_timeout) {
			spin_unlock(&lock);
			kfree(ctl);
			error(EFAIL, "unable to get an appropriate timer");
		}
		timeout = sec_timeout;
		WRITE_ONCE(enabled, true);
		cur_wd = ctl;
		ktask("watchdog", wd_ktask, cur_wd);
		spin_unlock(&lock);
	} else if (!strcmp(cb->f[0], "off")) {
		spin_lock(&lock);
		if (!enabled) {
			spin_unlock(&lock);
			error(EFAIL, "watchdog was not on");
		}
		WRITE_ONCE(enabled, false);
		timeout = 0;
		cur_wd->should_exit = true;
		cur_wd = NULL;
		__shutoff_timer();
		spin_unlock(&lock);
	} else {
		error(EFAIL, WD_CTL_USAGE);
	}
}

static size_t wd_write(struct chan *c, void *ubuf, size_t n, off64_t unused)
{
	ERRSTACK(1);
	struct cmdbuf *cb = parsecmd(ubuf, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	switch (c->qid.path) {
	case Qctl:
		wd_ctl_cmd(c, cb);
		break;
	default:
		error(EFAIL, "Unable to write to %s", devname());
	}
	kfree(cb);
	poperror();
	return n;
}

struct dev watchdog_devtab __devtab = {
	.name = "watchdog",
	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = wd_attach,
	.walk = wd_walk,
	.stat = wd_stat,
	.open = wd_open,
	.create = devcreate,
	.close = wd_close,
	.read = wd_read,
	.bread = devbread,
	.write = wd_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};
