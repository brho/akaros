// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2013, 2014 The Regents of the University of California
 * Copyright (c) 2020 Google Inc
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * Sorry, but you'll need to change your linux source to expose this function:

 EXPORT_SYMBOL_GPL(kthread_create_on_cpu);

 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#include <linux/sched/task.h>
#include <linux/sched/mm.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <asm/msr.h>

struct lock_sample {
	u64 pre;
	u64 acq;
	u64 un;
	bool valid;
};

/* mtx protects all variables and the test run */
static struct mutex mtx;

static DECLARE_COMPLETION(test_done);

static unsigned int nr_threads;
static unsigned int nr_loops;
static unsigned int hold_time;
static unsigned int delay_time;

/* array[nr_thread] of pointers of lock_sample[nr_loops] */
static struct lock_sample **times;
/* array[nr_thread] of task* */
static struct task_struct **threads;
/* array[nr_thread] of void* */
static void **retvals;
static void *results;
static size_t results_sz;

static bool run_locktest __cacheline_aligned_in_smp;
static atomic_t horses __cacheline_aligned_in_smp;

static struct qspinlock l = __ARCH_SPIN_LOCK_UNLOCKED;

static int __mcs_thread_lock_test(void *arg)
{
	long thread_id = (long)arg;
	u64 pre_lock, acq_lock, un_lock;
	struct lock_sample *this_time;
	int i;

	atomic_dec(&horses);
	while (atomic_read(&horses))
		cpu_relax();
	for (i = 0; i < nr_loops; i++) {
		/*
		 * might be able to replace this with post-processing.  let the
		 * test run, and discard all entries after the first finisher
		 */
		if (!READ_ONCE(run_locktest))
			break;

		local_irq_disable();
		pre_lock = rdtsc_ordered();

		queued_spin_lock(&l);

		acq_lock = rdtsc_ordered();

		if (hold_time)
			ndelay(hold_time);

		queued_spin_unlock(&l);

		un_lock = rdtsc_ordered();

		local_irq_enable();

		this_time = &times[thread_id][i];
		this_time->pre = pre_lock;
		this_time->acq = acq_lock;
		this_time->un = un_lock;
		/* Can turn these on/off to control which samples we gather */
		this_time->valid = true;
		if (delay_time)
			ndelay(delay_time);
		/*
		 * This can throw off your delay_time.  Think of delay_time as
		 * the least amount of time we'll wait between reacquiring the
		 * lock.  After all, IRQs are enabled, so all bets are off.
		 */
		cond_resched();
	}
	/* First thread to finish stops the test */
	WRITE_ONCE(run_locktest, false);
	/*
	 * Wakes the controller thread.  The others will be done soon, to
	 * complete the hokey thread join.
	 */
	complete(&test_done);

	WRITE_ONCE(retvals[thread_id], (void*)(long)i);

	return 0;
}

/*
 * This consolidates the results in a format we will export to userspace.  We
 * could have just used this format for the test itself, but then the times
 * arrays wouldn't be NUMA local.
 */
static int mcs_build_output(struct lock_sample **times, void **retvals)
{
	int i;
	size_t sz_rets = nr_threads * sizeof(void*);
	size_t sz_times_per = nr_loops * sizeof(struct lock_sample);

	results_sz = sz_rets + nr_threads * sz_times_per;

	kvfree(results);

	results = kvzalloc(results_sz, GFP_KERNEL);
	if (!results) {
		pr_err("fucked %d", __LINE__);
		return -1;
	}

	memcpy(results, retvals, sz_rets);
	for (i = 0; i < nr_threads; i++) {
		memcpy(results + sz_rets + i * sz_times_per,
		       times[i], sz_times_per);
	}

	return 0;
}

static int mcs_lock_test(void)
{
	int i;
	int ret = -1;
	size_t amt;

	atomic_set(&horses, nr_threads);
	WRITE_ONCE(run_locktest, true);

	times = kcalloc(nr_threads, sizeof(struct lock_sample *), GFP_KERNEL);
	if (!times) {
		pr_err("fucked %d", __LINE__);
		return ret;
	}

	if (check_mul_overflow((size_t)nr_loops, sizeof(struct lock_sample),
			       &amt)) {
		pr_err("fucked %d", __LINE__);
		goto out_times;
	}
	for (i = 0; i < nr_threads; i++) {
		times[i] = kvzalloc_node(amt, GFP_KERNEL, cpu_to_node(i));
		if (!times[i]) {
			/* we clean up the times[i]s below */
			pr_err("fucked %d", __LINE__);
			goto out_times;
		}
	}

	retvals = kcalloc(nr_threads, sizeof(void *), GFP_KERNEL);
	if (!retvals) {
		pr_err("fucked %d", __LINE__);
		goto out_times;
	}
	for (i = 0; i < nr_threads; i++)
		retvals[i] = (void*)-1;

	threads = kcalloc(nr_threads, sizeof(struct task_struct *),
			  GFP_KERNEL);
	if (!threads) {
		pr_err("fucked %d", __LINE__);
		goto out_retvals;
	}

	for (i = 0; i < nr_threads; i++) {
		threads[i] = kthread_create_on_cpu(__mcs_thread_lock_test,
						   (void*)(long)i, i, "mcs-%u");
		if (IS_ERR(threads[i])) {
			while (--i >= 0) {
				/*
				 * We could recover, perhaps with something like
				 * kthread_stop(threads[i]), but we'd need those
				 * threads to check kthread_should_stop(),
				 * perhaps in their hokey barrier.  I've never
				 * had this fail, so I haven't tested it.
				 */
			}
			pr_err("fucked %d", __LINE__);
			goto out_threads;
		}
	}
	for (i = 0; i < nr_threads; i++) {
		/* what's the deal with refcnting here?  it looks like an
		 * uncounted ref: create->result = current.  so once we start
		 * them, we probably can't touch this again. */
		wake_up_process(threads[i]);
	}

	/* Hokey join.  We know when the test is done but wait for the others */
	wait_for_completion(&test_done);
	for (i = 0; i < nr_threads; i++) {
		while (READ_ONCE(retvals[i]) == (void*)-1)
			cond_resched();
	}

	ret = mcs_build_output(times, retvals);

out_threads:
	kfree(threads);
out_retvals:
	kfree(retvals);
out_times:
	for (i = 0; i < nr_threads; i++)
		kvfree(times[i]);
	kfree(times);
	return ret;
}

static ssize_t mcs_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr,
			char *buf, loff_t off, size_t count)
{
	mutex_lock(&mtx);

	if (!off) {
		if (mcs_lock_test()) {
			mutex_unlock(&mtx);
			return -1;
		}
	}
	if (!results) {
		pr_err("fucked %d", __LINE__);
		mutex_unlock(&mtx);
		return -1;
	}
	/* mildly concerned about addition overflow.  caller's job? */
	if (count + off > results_sz) {
		pr_err("fucked off %lld count %lu sz %lu\n", off, count,
		       results_sz);
		count = results_sz - off;
	}
	memcpy(buf, results + off, count);

	mutex_unlock(&mtx);

	return count;
}

static loff_t __mcs_get_results_size(void)
{
	return nr_threads *
		(sizeof(void*) + nr_loops * sizeof(struct lock_sample));
}

/*
 * Unfortunately, this doesn't update the file live.  It'll only take effect the
 * next time you open it.  So users need to write, close, open, read.
 */
static void __mcs_update_size(void)
{
	struct kernfs_node *kn = kernfs_find_and_get(kernel_kobj->sd, "mcs");

	if (!kn) {
		pr_err("fucked %d", __LINE__);
		return;
	}
	kn->attr.size = __mcs_get_results_size();
}

static ssize_t mcs_write(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr,
			 char *buf, loff_t off, size_t count)
{
	unsigned int threads, loops, hold, delay;
	ssize_t ret;

	/* TODO: check_mul_overflow and whatnot, esp for the result_sz buffer */
	ret = sscanf(buf, "%u %u %u %u", &threads, &loops, &hold,
		     &delay);
	if (ret != 4)
		return -EINVAL;
	if (threads > num_online_cpus())
		return -ENODEV;
	if (threads == 0)
		threads = num_online_cpus();
	mutex_lock(&mtx);
	nr_threads = threads;
	nr_loops = loops;
	hold_time = hold;
	delay_time = delay;
	__mcs_update_size();
	mutex_unlock(&mtx);
	return count;
}

struct bin_attribute mcs_attr = {
	.attr = {
		.name = "mcs",
		.mode = 0666,
	},
	.size = 0,
	.private = NULL,
	.read = mcs_read,
	.write = mcs_write,
};

static int __init mcs_init(void)
{
	mutex_init(&mtx);

	/*
	 * The user needs to set these, but start with sensible defaults in case
	 * they read without writing.
	 */
	nr_threads = num_online_cpus();
	nr_loops = 10000;
	mcs_attr.size = __mcs_get_results_size();

	if (sysfs_create_bin_file(kernel_kobj, &mcs_attr)) {
		pr_err("\n\nfucked %d !!!\n\n\n", __LINE__);
		return -1;
	}
	return 0;
}

static void __exit mcs_exit(void)
{
	sysfs_remove_bin_file(kernel_kobj, &mcs_attr);
}

module_init(mcs_init);
module_exit(mcs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Barret Rhoden <brho@google.com>");
MODULE_DESCRIPTION("MCS lock test");
