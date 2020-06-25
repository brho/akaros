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

/* Seems fine either way.  Userspace uses lfence; rdtsc.  So use this if you're
 * paranoid about comparisons between user and kernel. */
#if 1
static inline u64 read_tsc_serialized(void)
{
	u32 lo, hi;

	asm volatile("lfence; rdtsc" : "=a" (lo), "=d" (hi));
	return (u64)hi << 32 | lo;
}

#else
#define read_tsc_serialized rdtsc_ordered
#endif

static void simple_ndelay(u64 nsec)
{
	u64 end;

	end = rdtsc() + (tsc_khz * nsec) / 1000000;
	do {
		cpu_relax();
	} while (rdtsc() < end);
}

struct lock_sample {
	u64 pre;
	u64 acq;
	u64 un;
	bool valid;
};

/* Consider using 128, if you're worried about Advanced Cacheline Prefetching */
#define CL_SZ 64

/* How long we'll run without IRQs enabled */
#define MSEC_WITHOUT_IRQ 100

/*********************** MCS ******************************/

#define MCS_LOCK_INIT {0}
#define MCS_QNODE_INIT {0, 0}

typedef struct mcs_lock_qnode
{
	struct mcs_lock_qnode *next;
	int locked;
} __attribute__((aligned(CL_SZ))) mcs_lock_qnode_t;

typedef struct mcs_lock
{
	mcs_lock_qnode_t *lock;
} mcs_lock_t;

/* Dirty trick to get an isolated cache line; need the attrib on the type. */
struct {
	struct mcs_lock ___mcs_l;
} __attribute__((aligned(CL_SZ))) __mcs_l = {MCS_LOCK_INIT};
#define mcs_l __mcs_l.___mcs_l

void mcs_lock_init(struct mcs_lock *lock)
{
	memset(lock, 0, sizeof(mcs_lock_t));
}

static inline mcs_lock_qnode_t *mcs_qnode_swap(mcs_lock_qnode_t **addr,
                                               mcs_lock_qnode_t *val)
{
	return (mcs_lock_qnode_t*) __sync_lock_test_and_set((void**)addr, val);
}

void notrace mcs_lock_lock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	mcs_lock_qnode_t *predecessor;

	qnode->next = 0;
	barrier();	/* swap provides a CPU mb() */
	predecessor = mcs_qnode_swap(&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		smp_wmb();
		predecessor->next = qnode;
		/* no need for a wrmb(), since this will only get unlocked
		 * after they read our previous write */
		while (qnode->locked)
			cpu_relax();
	}
	barrier();/* just need a cmb, the swap handles the CPU wmb/wrmb() */
}

/* CAS version (no usurper) */
void notrace mcs_lock_unlock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		/* no need for CPU mbs, since there's an atomic_cas() */
		barrier();
		/* If we're still the lock, just swap it with 0 (unlock) and
		 * return */
		if (__sync_bool_compare_and_swap((void**)&lock->lock, qnode, 0))
			return;
		/* We failed, someone is there and we are some (maybe a
		 * different) thread's pred.  Since someone else was waiting,
		 * they should have made themselves our next.  Spin (very
		 * briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		smp_wmb();
		/* need to make sure any reads happen before the unlocking */
		barrier(); //rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/*********************** QUEUE ******************************/
struct {
	struct qspinlock ___qsl;
} __attribute__((aligned(CL_SZ))) __qsl = {__ARCH_SPIN_LOCK_UNLOCKED};
#define qsl __qsl.___qsl

/*********************** SPIN ******************************/
struct {
	arch_spinlock_t ___asl;;
} __attribute__((aligned(CL_SZ))) __asl = {__ARCH_SPIN_LOCK_UNLOCKED};
#define asl __asl.___asl


/* mtx protects all variables and the test run */
static struct mutex mtx;

struct lock_test;
static struct lock_test *test;
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

static bool run_locktest;
static atomic_t horses;

/* Every lock type has their own function, named __lock_name_thread(). */
#define lock_func(lock_name, pre_cmd, lock_cmd, unlock_cmd)                    \
static int __##lock_name##_thread(void *arg)                                   \
{                                                                              \
	long thread_id = (long)arg;                                            \
	u64 pre_lock, acq_lock, un_lock;                                       \
	struct lock_sample *this_time;                                         \
	int i;                                                                 \
	u64 next_irq;                                                          \
                                                                               \
	/*                                                                     
	 * hold_time is the important one to have locally.  o/w we might cache
	 * miss during the critical section.  Why would we miss?  Perhaps
	 * because hold_time is on the adjacent cache line to the spinlock, and
	 * !(MSR 0x1A4 & 2), though that'd only make sense if Adjacent Cachelin
	 * Prefetching prefetched in exclusive mode (and thus invalidating).
	 * The others are important too, though less so.  Their miss would be
	 * outside the critical section, but if you happen to rearrange the
	 * file, they could falsely share with the lock.
	 */                                                                    \
	unsigned int hold_time_l = READ_ONCE(hold_time);                       \
	unsigned int delay_time_l = READ_ONCE(delay_time);                     \
	unsigned int nr_loops_l = READ_ONCE(nr_loops);                         \
                                                                               \
	pre_cmd                                                                \
                                                                               \
	atomic_dec(&horses);                                                   \
	while (atomic_read(&horses))                                           \
		cpu_relax();                                                   \
                                                                               \
	/*                                                                     
	 * I'd like to enable/disable IRQs in the loop, but that affects the
	 * test, even if they are outside the timestamps and the critical
	 * section.  Instead, just turn them on periodically.  100ms was what I
	 * noticed didn't affect the test's throughput (Haswell).
	 */                                                                    \
	local_irq_disable();                                                   \
	next_irq = rdtsc() + tsc_khz * MSEC_WITHOUT_IRQ;                       \
                                                                               \
	for (i = 0; i < nr_loops_l; i++) {                                     \
		/*                                                             
		 * might be able to replace this with post-processing.  let the
		 * test run, and discard all entries after the first finisher  
		 */                                                            \
		if (!READ_ONCE(run_locktest))                                  \
			break;                                                 \
                                                                               \
		pre_lock = read_tsc_serialized();                              \
                                                                               \
		lock_cmd                                                       \
                                                                               \
		acq_lock = read_tsc_serialized();                              \
                                                                               \
		if (hold_time_l)                                               \
			simple_ndelay(hold_time_l);                            \
                                                                               \
		unlock_cmd                                                     \
                                                                               \
		un_lock = read_tsc_serialized();                               \
                                                                               \
		this_time = &times[thread_id][i];                              \
		this_time->pre = pre_lock;                                     \
		this_time->acq = acq_lock;                                     \
		this_time->un = un_lock;                                       \
		/* Can turn these on/off to control which samples we gather */ \
		this_time->valid = true;                                       \
		if (delay_time_l)                                              \
			simple_ndelay(delay_time_l);                           \
		/*                                                             
		 * This can throw off your delay_time.  Think of delay_time as 
		 * the least amount of time we'll wait between reacquiring the 
		 * lock.                                                       
		 */                                                            \
		if (next_irq < un_lock) {                                      \
			local_irq_enable();                                    \
			cond_resched();		/* since we're here. */        \
			local_irq_disable();                                   \
			next_irq = rdtsc() + tsc_khz * MSEC_WITHOUT_IRQ;       \
		}                                                              \
	}                                                                      \
                                                                               \
	local_irq_enable();                                                    \
                                                                               \
	/* First thread to finish stops the test */                            \
	WRITE_ONCE(run_locktest, false);                                       \
	/*                                                                     
	 * Wakes the controller thread.  The others will be done soon, to      
	 * complete the hokey thread join.                                     
	 */                                                                    \
	complete(&test_done);                                                  \
                                                                               \
	WRITE_ONCE(retvals[thread_id], (void*)(long)i);                        \
                                                                               \
	return 0;                                                              \
}

lock_func(mcs,
	  struct mcs_lock_qnode qn = MCS_QNODE_INIT;,
	  mcs_lock_lock(&mcs_l, &qn);,
	  mcs_lock_unlock(&mcs_l, &qn););
lock_func(queue,
	  ;,
	  queued_spin_lock(&qsl);,
	  queued_spin_unlock(&qsl););
lock_func(spin,
	  ;,
	  arch_spin_lock(&asl);,
	  arch_spin_unlock(&asl););

/* ID is for userspace, name is for the kthread, func is what runs */
struct lock_test {
	unsigned int id;
	const char *name;
	int (*func)(void *);
};

#define LOCKTEST_MCS 		1
#define LOCKTEST_QUEUE 		2
#define LOCKTEST_SPIN 		3

static struct lock_test tests[] = {
	{LOCKTEST_MCS,          "mcs", __mcs_thread},
	{LOCKTEST_QUEUE,      "queue", __queue_thread},
	{LOCKTEST_SPIN,        "spin", __spin_thread},
	{}
};

static struct lock_test *get_test(unsigned int id)
{
	struct lock_test *ret;

	for (ret = tests; ret->id; ret++)
		if (ret->id == id)
			return ret;
	return NULL;
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
		threads[i] = kthread_create_on_cpu(test->func,
						   (void*)(long)i, i, "mcs-""%u");
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

	if (!test) {
		mutex_unlock(&mtx);
		return -ENOLCK;	/* i'd kill for errstr */
	}
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
	unsigned int id, threads, loops, hold, delay;
	ssize_t ret;
	struct lock_test *t;

	/* TODO: check_mul_overflow and whatnot, esp for the result_sz buffer */
	ret = sscanf(buf, "%u %u %u %u %u", &id, &threads, &loops, &hold,
		     &delay);
	if (ret != 5)
		return -EINVAL;
	
	t = get_test(id);
	if (!t)
		return -ENOLCK;
	if (threads > num_online_cpus())
		return -ENODEV;
	if (threads == 0)
		threads = num_online_cpus();
	mutex_lock(&mtx);
	test = t;
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
