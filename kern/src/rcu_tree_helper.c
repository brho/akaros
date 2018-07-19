/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright IBM Corporation, 2008
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *	    Manfred Spraul <manfred@colorfullife.com>
 *	    Paul E. McKenney <paulmck@linux.vnet.ibm.com> Hierarchical version
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 *	Documentation/RCU
 *
 * AKAROS_PORT: was kernel/rcu/tree.c.
 */


#include <rcu.h>
#include <assert.h>
#include <smp.h>

/* Control rcu_node-tree auto-balancing at boot time. */
bool rcu_fanout_exact;
/* Increase (but not decrease) the RCU_FANOUT_LEAF at boot time. */
int rcu_fanout_leaf = RCU_FANOUT_LEAF;
int rcu_num_lvls = RCU_NUM_LVLS;
/* Number of rcu_nodes at specified level. */
int num_rcu_lvl[] = NUM_RCU_LVL_INIT;
int rcu_num_nodes = NUM_RCU_NODES; /* Total # rcu_nodes in use. */
/* Number of cores RCU thinks exist.  Set to 0 or nothing to use 'num_cores'.
 * The extra cores are called 'fake cores' in rcu.c, and are used for testing
 * the tree. */
int rcu_num_cores;

/* in rcu.c */
extern struct rcu_state rcu_state;

/**
 * get_state_synchronize_rcu - Snapshot current RCU state
 *
 * Returns a cookie that is used by a later call to cond_synchronize_rcu()
 * to determine whether or not a full grace period has elapsed in the
 * meantime.
 */
unsigned long get_state_synchronize_rcu(void)
{
	/*
	 * Any prior manipulation of RCU-protected data must happen
	 * before the load from ->gpnum.
	 */
	mb();  /* ^^^ */

	/*
	 * Make sure this load happens before the purportedly
	 * time-consuming work between get_state_synchronize_rcu()
	 * and cond_synchronize_rcu().
	 */
	return smp_load_acquire(&rcu_state.gpnum);
}

/**
 * cond_synchronize_rcu - Conditionally wait for an RCU grace period
 *
 * @oldstate: return value from earlier call to get_state_synchronize_rcu()
 *
 * If a full RCU grace period has elapsed since the earlier call to
 * get_state_synchronize_rcu(), just return.  Otherwise, invoke
 * synchronize_rcu() to wait for a full grace period.
 *
 * Yes, this function does not take counter wrap into account.  But
 * counter wrap is harmless.  If the counter wraps, we have waited for
 * more than 2 billion grace periods (and way more on a 64-bit system!),
 * so waiting for one additional grace period should be just fine.
 */
void cond_synchronize_rcu(unsigned long oldstate)
{
	unsigned long newstate;

	/*
	 * Ensure that this load happens before any RCU-destructive
	 * actions the caller might carry out after we return.
	 */
	newstate = smp_load_acquire(&rcu_state.completed);
	if (ULONG_CMP_GE(oldstate, newstate))
		synchronize_rcu();
}


/*
 * Helper function for rcu_init() that initializes one rcu_state structure.
 */
void rcu_init_one(struct rcu_state *rsp)
{
	int levelspread[RCU_NUM_LVLS];		/* kids/node in each level. */
	int cpustride = 1;
	int i;
	int j;
	struct rcu_node *rnp;
	struct rcu_pcpui *rpi;

	rendez_init(&rsp->gp_ktask_rv);
	rsp->gp_ktask_ctl = 0;
	/* To test wraparound early */
	rsp->gpnum = ULONG_MAX - 20;
	rsp->completed = ULONG_MAX - 20;

	/* Silence gcc 4.8 false positive about array index out of range. */
	if (rcu_num_lvls <= 0 || rcu_num_lvls > RCU_NUM_LVLS)
		panic("rcu_init_one: rcu_num_lvls out of range");

	/* Initialize the level-tracking arrays. */

	rsp->level[0] = &rsp->node[0];
	for (i = 1; i < rcu_num_lvls; i++)
		rsp->level[i] = rsp->level[i - 1] + num_rcu_lvl[i - 1];
	rcu_init_levelspread(levelspread, num_rcu_lvl);

	/* Initialize the elements themselves, starting from the leaves. */

	for (i = rcu_num_lvls - 1; i >= 0; i--) {
		cpustride *= levelspread[i];
		rnp = rsp->level[i];
		for (j = 0; j < num_rcu_lvl[i]; j++, rnp++) {
			rnp->qsmask = 0;
			rnp->qsmaskinit = 0;
			rnp->grplo = j * cpustride;
			rnp->grphi = (j + 1) * cpustride - 1;
			if (rnp->grphi >= rcu_num_cores)
				rnp->grphi = rcu_num_cores - 1;
			if (i == 0) {
				rnp->grpnum = 0;
				rnp->grpmask = 0;
				rnp->parent = NULL;
			} else {
				rnp->grpnum = j % levelspread[i - 1];
				rnp->grpmask = 1UL << rnp->grpnum;
				rnp->parent = rsp->level[i - 1] +
						  j / levelspread[i - 1];
			}
			rnp->level = i;
		}
	}

	rnp = rsp->level[rcu_num_lvls - 1];
	for_each_core(i) {
		while (i > rnp->grphi)
			rnp++;
		rpi = _PERCPU_VARPTR(rcu_pcpui, i);
		rpi->my_node = rnp;
		rcu_init_pcpui(rsp, rpi, i);
	}
	/* Akaros has static rnps, so we can init these once. */
	rcu_for_each_node_breadth_first(rsp, rnp)
		if (rnp->parent)
			rnp->parent->qsmaskinit |= rnp->grpmask;
}

/*
 * Compute the rcu_node tree geometry from kernel parameters.  This cannot
 * replace the definitions in tree.h because those are needed to size
 * the ->node array in the rcu_state structure.
 */
void rcu_init_geometry(void)
{
	int i;
	int rcu_capacity[RCU_NUM_LVLS];
	int nr_cpu_ids;

	if (!rcu_num_cores)
		rcu_num_cores = num_cores;
	nr_cpu_ids = rcu_num_cores;
	/* If the compile-time values are accurate, just leave. */
	if (rcu_fanout_leaf == RCU_FANOUT_LEAF &&
		nr_cpu_ids == NR_CPUS)
		return;
	printk("RCU: Adjusting geometry for rcu_fanout_leaf=%d, nr_cpu_ids=%d\n",
		rcu_fanout_leaf, nr_cpu_ids);

	/*
	 * The boot-time rcu_fanout_leaf parameter must be at least two
	 * and cannot exceed the number of bits in the rcu_node masks.
	 * Complain and fall back to the compile-time values if this
	 * limit is exceeded.
	 */
	if (rcu_fanout_leaf < 2 ||
		rcu_fanout_leaf > sizeof(unsigned long) * 8) {
		rcu_fanout_leaf = RCU_FANOUT_LEAF;
		warn_on(1);
		return;
	}

	/*
	 * Compute number of nodes that can be handled an rcu_node tree
	 * with the given number of levels.
	 */
	rcu_capacity[0] = rcu_fanout_leaf;
	for (i = 1; i < RCU_NUM_LVLS; i++)
		rcu_capacity[i] = rcu_capacity[i - 1] * RCU_FANOUT;

	/*
	 * The tree must be able to accommodate the configured number of CPUs.
	 * If this limit is exceeded, fall back to the compile-time values.
	 */
	if (nr_cpu_ids > rcu_capacity[RCU_NUM_LVLS - 1]) {
		rcu_fanout_leaf = RCU_FANOUT_LEAF;
		warn_on(1);
		return;
	}

	/* Calculate the number of levels in the tree. */
	for (i = 0; nr_cpu_ids > rcu_capacity[i]; i++) {
	}
	rcu_num_lvls = i + 1;

	/* Calculate the number of rcu_nodes at each level of the tree. */
	for (i = 0; i < rcu_num_lvls; i++) {
		int cap = rcu_capacity[(rcu_num_lvls - 1) - i];
		num_rcu_lvl[i] = DIV_ROUND_UP(nr_cpu_ids, cap);
	}

	/* Calculate the total number of rcu_node structures. */
	rcu_num_nodes = 0;
	for (i = 0; i < rcu_num_lvls; i++)
		rcu_num_nodes += num_rcu_lvl[i];
}

/*
 * Dump out the structure of the rcu_node combining tree associated
 * with the rcu_state structure referenced by rsp.
 */
void rcu_dump_rcu_node_tree(struct rcu_state *rsp)
{
	int level = 0;
	struct rcu_node *rnp;

	print_lock();
	printk("rcu_node tree layout dump\n");
	printk(" ");
	rcu_for_each_node_breadth_first(rsp, rnp) {
		if (rnp->level != level) {
			printk("\n");
			printk(" ");
			level = rnp->level;
		}
		printk("%d:%d ^%d ", rnp->grplo, rnp->grphi, rnp->grpnum);
	}
	printk("\n");
	print_unlock();
}
