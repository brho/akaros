/* Copyright (C) 1991-2017, the Linux Kernel authors */

/*
 * Fast batching percpu counters.
 */

#include <percpu_counter.h>
#include <linux_compat.h>

void percpu_counter_set(struct percpu_counter *fbc, int64_t amount)
{
	int cpu;
	unsigned long flags;

	spin_lock_irqsave(&fbc->lock);
	for_each_possible_cpu(cpu) {
		int32_t *pcount = _PERCPU_VARPTR(*fbc->counters, cpu);
		*pcount = 0;
	}
	fbc->count = amount;
	spin_unlock_irqsave(&fbc->lock);
}

/**
 * This function is both preempt and irq safe. The former is due to explicit
 * preemption disable. The latter is guaranteed by the fact that the slow path
 * is explicitly protected by an irq-safe spinlock whereas the fast patch uses
 * this_cpu_add which is irq-safe by definition. Hence there is no need muck
 * with irq state before calling this one
 */
void percpu_counter_add_batch(struct percpu_counter *fbc, int64_t amount,
			      int32_t batch)
{
	int64_t count;

	preempt_disable();
	count = __this_cpu_read(*fbc->counters) + amount;
	if (count >= batch || count <= -batch) {
		unsigned long flags;
		spin_lock_irqsave(&fbc->lock);
		fbc->count += count;
		__this_cpu_sub(*fbc->counters, count - amount);
		spin_unlock_irqsave(&fbc->lock);
	} else {
		this_cpu_add(*fbc->counters, amount);
	}
	preempt_enable();
}

/*
 * Add up all the per-cpu counts, return the result.  This is a more accurate
 * but much slower version of percpu_counter_read_positive()
 */
int64_t __percpu_counter_sum(struct percpu_counter *fbc)
{
	int64_t ret;
	int cpu;
	unsigned long flags;

	spin_lock_irqsave(&fbc->lock);
	ret = fbc->count;
	for_each_online_cpu(cpu) {
		int32_t *pcount = _PERCPU_VARPTR(*fbc->counters, cpu);
		ret += *pcount;
	}
	spin_unlock_irqsave(&fbc->lock);
	return ret;
}

int percpu_counter_init(struct percpu_counter *fbc, int64_t amount, gfp_t gfp)
{
	unsigned long flags __maybe_unused;

	spinlock_init_irqsave(&fbc->lock);
	fbc->count = amount;
	fbc->counters = alloc_percpu_gfp(int32_t, gfp);
	if (!fbc->counters)
		return -ENOMEM;
	return 0;
}

void percpu_counter_destroy(struct percpu_counter *fbc)
{
	unsigned long flags __maybe_unused;

	if (!fbc->counters)
		return;
	free_percpu(fbc->counters);
	fbc->counters = NULL;
}

int percpu_counter_batch __read_mostly = 32;

/*
 * Compare counter against given value.
 * Return 1 if greater, 0 if equal and -1 if less
 */
int __percpu_counter_compare(struct percpu_counter *fbc, int64_t rhs,
		             int32_t batch)
{
	int64_t	count;

	count = percpu_counter_read(fbc);
	/* Check to see if rough count will be sufficient for comparison */
	if (abs(count - rhs) > (batch * num_online_cpus())) {
		if (count > rhs)
			return 1;
		else
			return -1;
	}
	/* Need to use precise count */
	count = percpu_counter_sum(fbc);
	if (count > rhs)
		return 1;
	else if (count < rhs)
		return -1;
	else
		return 0;
}
