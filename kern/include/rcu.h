/* Copyright (c) 2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * RCU.  See rcu.c for notes.
 */

#pragma once

#include <arch/membar.h>
#include <arch/topology.h>
#include <percpu.h>
#include <list.h>

struct rcu_head;
typedef void (*rcu_callback_t)(struct rcu_head *head);
typedef void (*call_rcu_func_t)(struct rcu_head *head, rcu_callback_t func);

struct rcu_head {
	struct list_head			link;
	rcu_callback_t				func;
	unsigned long				gpnum;
};

#include <rcupdate.h>

/* This is a little nasty.  We can't undef it, since NUM_RCU_NODES is used in a
 * few places.  We could get this from Kconfig, like Linux does. */
#define NR_CPUS 4096

#include <rcu_helper.h>
#include <rculist.h>
#include <rendez.h>

struct rcu_node {
	struct rcu_node				*parent;
	unsigned long				qsmask;	/* cores that need to check in */
	unsigned long				qsmaskinit;
	unsigned int				level;
	unsigned int				grplo;	/* lowest nr CPU here */
	unsigned int				grphi;	/* highest nr CPU here */
	unsigned int				grpnum;	/* our number in our parent */
	unsigned long				grpmask;/* our bit in our parent */
};

struct rcu_state {
	struct rcu_node				node[NUM_RCU_NODES];
	struct rcu_node				*level[RCU_NUM_LVLS];

	/* These are read by everyone but only written by the GP kthread */
	unsigned long				gpnum;
	unsigned long				completed;

	/* These are written by anyone trying to wake the gp kthread, which can be
	 * any core whose CB list is long or does an rcu_barrier() */
	/* TODO: make a ktask struct and use a read-only pointer. */
	struct rendez				gp_ktask_rv;
	int							gp_ktask_ctl;
};

struct rcu_pcpui {
	struct rcu_state			*rsp;
	struct rcu_node				*my_node;
	int							coreid;
	unsigned int				grpnum;
	unsigned long				grpmask;
	bool						booted;

	spinlock_t					lock;
	struct list_head			cbs;
	unsigned int				nr_cbs;
	unsigned long				gp_acked;

	struct rendez				mgmt_ktask_rv;
	int							mgmt_ktask_ctl;
};
DECLARE_PERCPU(struct rcu_pcpui, rcu_pcpui);

void rcu_init(void);
void rcu_report_qs(void);
void rcu_barrier(void);
void rcu_force_quiescent_state(void);
unsigned long get_state_synchronize_rcu(void);
void cond_synchronize_rcu(unsigned long oldstate);
void kfree_call_rcu(struct rcu_head *head, rcu_callback_t off);

/* Internal Helpers (rcu.c) */
void rcu_init_pcpui(struct rcu_state *rsp, struct rcu_pcpui *rpi, int coreid);

/* Internal Helpers (rcu_tree_helper.c) */
void rcu_init_one(struct rcu_state *rsp);
void rcu_init_geometry(void);
void rcu_dump_rcu_node_tree(struct rcu_state *rsp);
