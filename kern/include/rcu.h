/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Junk, racy implementation of RCU
 */

#pragma once

#include <trap.h>

struct rcu_head {
};

static void __call_rcu_kmsg(uint32_t srcid, long a0, long a1, long a2)
{
	void (*f)(struct rcu_head *) = (void*)a0;

	f((void*)a1);
}

static void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head))
{
	send_kernel_message(core_id(), __call_rcu_kmsg, (long)func, (long)head, 0,
	                    KMSG_ROUTINE);
}

#define hlist_for_each_entry_rcu hlist_for_each_entry
#define hlist_add_head_rcu hlist_add_head
#define hlist_del_rcu hlist_del

#define rcu_read_lock()
#define rcu_read_unlock()
#define synchronize_rcu()
#define rcu_dereference(x) (x)
#define rcu_barrier() kthread_usleep(10000)
