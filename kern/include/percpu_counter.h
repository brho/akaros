#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H
/*
 * A simple "approximate counter" for use in ext2 and ext3 superblocks.
 *
 * WARNING: these things are HUGE.  4 kbytes per counter on 32-way P4.
 */

#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/gfp.h>

#ifdef CONFIG_SMP

struct percpu_counter {
	spinlock_t lock;
	int64_t count;
#ifdef CONFIG_HOTPLUG_CPU
	struct list_head list;	/* All percpu_counters are on a list */
#endif
	int32_t __percpu *counters;
};

extern int percpu_counter_batch;

int __percpu_counter_init(struct percpu_counter *fbc, int64_t amount, gfp_t gfp,
			  struct lock_class_key *key);

#define percpu_counter_init(fbc, value, gfp)				\
	({								\
		static struct lock_class_key __key;			\
									\
		__percpu_counter_init(fbc, value, gfp, &__key);		\
	})

void percpu_counter_destroy(struct percpu_counter *fbc);
void percpu_counter_set(struct percpu_counter *fbc, int64_t amount);
void percpu_counter_add_batch(struct percpu_counter *fbc, int64_t amount,
			      int32_t batch);
int64_t __percpu_counter_sum(struct percpu_counter *fbc);
int __percpu_counter_compare(struct percpu_counter *fbc, int64_t rhs,
			     int32_t batch);

static inline int percpu_counter_compare(struct percpu_counter *fbc,
					 int64_t rhs)
{
	return __percpu_counter_compare(fbc, rhs, percpu_counter_batch);
}

static inline void percpu_counter_add(struct percpu_counter *fbc,
				      int64_t amount)
{
	percpu_counter_add_batch(fbc, amount, percpu_counter_batch);
}

static inline int64_t percpu_counter_sum_positive(struct percpu_counter *fbc)
{
	int64_t ret = __percpu_counter_sum(fbc);
	return ret < 0 ? 0 : ret;
}

static inline int64_t percpu_counter_sum(struct percpu_counter *fbc)
{
	return __percpu_counter_sum(fbc);
}

static inline int64_t percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

/*
 * It is possible for the percpu_counter_read() to return a small negative
 * number for some counter which should never be negative.
 *
 */
static inline int64_t percpu_counter_read_positive(struct percpu_counter *fbc)
{
	int64_t ret = fbc->count;

	cmb();		/* Prevent reloads of fbc->count */
	if (ret >= 0)
		return ret;
	return 0;
}

static inline int percpu_counter_initialized(struct percpu_counter *fbc)
{
	return (fbc->counters != NULL);
}

#else /* !CONFIG_SMP */

struct percpu_counter {
	int64_t count;
};

static inline int percpu_counter_init(struct percpu_counter *fbc, int64_t amount,
				      gfp_t gfp)
{
	fbc->count = amount;
	return 0;
}

static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
}

static inline void percpu_counter_set(struct percpu_counter *fbc,
				      int64_t amount)
{
	fbc->count = amount;
}

static inline int percpu_counter_compare(struct percpu_counter *fbc,
				         int64_t rhs)
{
	if (fbc->count > rhs)
		return 1;
	else if (fbc->count < rhs)
		return -1;
	else
		return 0;
}

static inline int
__percpu_counter_compare(struct percpu_counter *fbc, int64_t rhs,
			 int32_t batch)
{
	return percpu_counter_compare(fbc, rhs);
}

static inline void
percpu_counter_add(struct percpu_counter *fbc, int64_t amount)
{
	preempt_disable();
	fbc->count += amount;
	preempt_enable();
}

static inline void
percpu_counter_add_batch(struct percpu_counter *fbc, int64_t amount,
			 int32_t batch)
{
	percpu_counter_add(fbc, amount);
}

static inline int64_t percpu_counter_read(struct percpu_counter *fbc)
{
	return fbc->count;
}

/*
 * percpu_counter is intended to track positive numbers. In the UP case the
 * number should never be negative.
 */
static inline int64_t percpu_counter_read_positive(struct percpu_counter *fbc)
{
	return fbc->count;
}

static inline int64_t percpu_counter_sum_positive(struct percpu_counter *fbc)
{
	return percpu_counter_read_positive(fbc);
}

static inline int64_t percpu_counter_sum(struct percpu_counter *fbc)
{
	return percpu_counter_read(fbc);
}

static inline int percpu_counter_initialized(struct percpu_counter *fbc)
{
	return 1;
}

#endif	/* CONFIG_SMP */

static inline void percpu_counter_inc(struct percpu_counter *fbc)
{
	percpu_counter_add(fbc, 1);
}

static inline void percpu_counter_dec(struct percpu_counter *fbc)
{
	percpu_counter_add(fbc, -1);
}

static inline void percpu_counter_sub(struct percpu_counter *fbc,
				      int64_t amount)
{
	percpu_counter_add(fbc, -amount);
}

#endif /* _LINUX_PERCPU_COUNTER_H */
