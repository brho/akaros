/* Copyright (c) 2009, 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching. */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <schedule.h>
#include <process.h>
#include <monitor.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <resource.h>
#include <smp.h>
#include <sys/queue.h>

/* Process Lists */
struct proc_list runnable_scps = TAILQ_HEAD_INITIALIZER(runnable_scps);
struct proc_list all_mcps = TAILQ_HEAD_INITIALIZER(all_mcps);
spinlock_t sched_lock = SPINLOCK_INITIALIZER;

// This could be useful for making scheduling decisions.  
/* Physical coremap: each index is a physical core id, with a proc ptr for
 * whoever *should be or is* running.  Very similar to current, which is what
 * process is *really* running there. */
struct proc *pcoremap[MAX_NUM_CPUS];

/* Tracks which cores are idle, similar to the vcoremap.  Each value is the
 * physical coreid of an unallocated core. */
spinlock_t idle_lock = SPINLOCK_INITIALIZER;
uint32_t idlecoremap[MAX_NUM_CPUS];
uint32_t num_idlecores = 0;
uint32_t num_mgmtcores = 1;

void schedule_init(void)
{
	TAILQ_INIT(&runnable_scps);
	TAILQ_INIT(&all_mcps);

	/* Ghetto old idle core init */
	/* Init idle cores. Core 0 is the management core. */
	spin_lock(&idle_lock);
#ifdef __CONFIG_DISABLE_SMT__
	/* assumes core0 is the only management core (NIC and monitor functionality
	 * are run there too.  it just adds the odd cores to the idlecoremap */
	assert(!(num_cpus % 2));
	// TODO: consider checking x86 for machines that actually hyperthread
	num_idlecores = num_cpus >> 1;
 #ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share wit NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(2, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
 #endif
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = (i * 2) + 1;
#else
	// __CONFIG_DISABLE_SMT__
	#ifdef __CONFIG_NETWORKING__
	num_mgmtcores++; // Next core is dedicated to the NIC
	assert(num_cpus >= num_mgmtcores);
	#endif
	#ifdef __CONFIG_APPSERVER__
	#ifdef __CONFIG_DEDICATED_MONITOR__
	num_mgmtcores++; // Next core dedicated to running the kernel monitor
	assert(num_cpus >= num_mgmtcores);
	// Need to subtract 1 from the num_mgmtcores # to get the cores index
	send_kernel_message(num_mgmtcores-1, (amr_t)monitor, 0,0,0, KMSG_ROUTINE);
	#endif
	#endif
 #ifdef __CONFIG_ARSC_SERVER__
	// Dedicate one core (core 2) to sysserver, might be able to share with NIC
	num_mgmtcores++;
	assert(num_cpus >= num_mgmtcores);
	send_kernel_message(num_mgmtcores-1, (amr_t)arsc_server, 0,0,0, KMSG_ROUTINE);
 #endif
	num_idlecores = num_cpus - num_mgmtcores;
	for (int i = 0; i < num_idlecores; i++)
		idlecoremap[i] = i + num_mgmtcores;
#endif /* __CONFIG_DISABLE_SMT__ */
	spin_unlock(&idle_lock);
	return;
}

/* _S procs are scheduled like in traditional systems */
void schedule_scp(struct proc *p)
{
	/* up the refcnt since we are storing the reference */
	proc_incref(p, 1);
	spin_lock(&sched_lock);
	printd("Scheduling PID: %d\n", p->pid);
	TAILQ_INSERT_TAIL(&runnable_scps, p, proc_link);
	spin_unlock(&sched_lock);
}

/* important to only call this on RUNNING_S, for now */
void register_mcp(struct proc *p)
{
	proc_incref(p, 1);
	spin_lock(&sched_lock);
	TAILQ_INSERT_TAIL(&all_mcps, p, proc_link);
	spin_unlock(&sched_lock);
	//poke_ksched(p, RES_CORES);
}

/* Something has changed, and for whatever reason the scheduler should
 * reevaluate things. 
 *
 * Don't call this from interrupt context (grabs proclocks). */
void schedule(void)
{
	struct proc *p, *temp;
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	spin_lock(&sched_lock);
	/* trivially try to handle the needs of all our MCPS.  smarter schedulers
	 * would do something other than FCFS */
	TAILQ_FOREACH_SAFE(p, &all_mcps, proc_link, temp) {
		printd("Ksched has MCP %08p (%d)\n", p, p->pid);
		/* If they are dying, abort.  There's a bit of a race here.  If they
		 * start dying right after the check, core_request/give_cores would
		 * start dealing with a DYING proc.  The code can handle it, but this
		 * will probably change. */
		if (p->state == PROC_DYING) {
			TAILQ_REMOVE(&all_mcps, p, proc_link);
			proc_decref(p);
			continue;
		}
		if (!num_idlecores)
			break;
		/* TODO: might use amt_wanted as a proxy.  right now, they have
		 * amt_wanted == 1, even though they are waiting.
		 * TODO: this is RACY too - just like with DYING. */
		if (p->state == PROC_WAITING)
			continue;
		core_request(p);
	}
	/* prune any dying SCPs at the head of the queue and maybe sched our core */
	while ((p = TAILQ_FIRST(&runnable_scps))) {
		if (p->state == PROC_DYING) {
			TAILQ_REMOVE(&runnable_scps, p, proc_link);
			proc_decref(p);
		} else {
			/* check our core to see if we can give it out to an SCP */
			if (!pcpui->owning_proc) {
				TAILQ_REMOVE(&runnable_scps, p, proc_link);
				printd("PID of the SCP i'm running: %d\n", p->pid);
				proc_run_s(p);	/* gives it core we're running on */
				proc_decref(p);
			}
			break;
		}
	}
	spin_unlock(&sched_lock);
}

/* A process is asking the ksched to look at its resource desires.  The
 * scheduler is free to ignore this, for its own reasons, so long as it
 * eventually gets around to looking at resource desires. */
void poke_ksched(struct proc *p, int res_type)
{
	/* TODO: probably want something to trigger all res_types */
	/* Consider races with core_req called from other pokes or schedule */
	switch (res_type) {
		case RES_CORES:
			/* TODO: issues with whether or not they are RUNNING.  Need to
			 * change core_request / give_cores. */
			core_request(p);
			break;
		default:
			break;
	}
}

/* Helper function to return a core to the idlemap.  It causes some more lock
 * acquisitions (like in a for loop), but it's a little easier.  Plus, one day
 * we might be able to do this without locks (for the putting). */
void put_idle_core(uint32_t coreid)
{
	spin_lock(&idle_lock);
	idlecoremap[num_idlecores++] = coreid;
	spin_unlock(&idle_lock);
}

/* Normally it'll be the max number of CG cores ever */
uint32_t max_vcores(struct proc *p)
{
#ifdef __CONFIG_DISABLE_SMT__
	return num_cpus >> 1;
#else
	return MAX(1, num_cpus - num_mgmtcores);
#endif /* __CONFIG_DISABLE_SMT__ */
}

/* Ghetto old interface, hacked out of resource.c.  It doesn't even care about
 * the proc yet, but in general the whole core_request bit needs reworked. */
uint32_t proc_wants_cores(struct proc *p, uint32_t *pc_arr, uint32_t amt_new)
{
	uint32_t num_granted;
	/* You should do something smarter than just giving the stuff out.  Like
	 * take in to account priorities, node locations, etc */
	spin_lock(&idle_lock);
	if (num_idlecores >= amt_new) {
		for (int i = 0; i < amt_new; i++) {
			// grab the last one on the list
			pc_arr[i] = idlecoremap[num_idlecores - 1];
			num_idlecores--;
		}
		num_granted = amt_new;
	} else {
		/* In this case, you might want to preempt or do other fun things... */
		num_granted = 0;
	}
	spin_unlock(&idle_lock);
	return num_granted;
}

/************** Debugging **************/
void sched_diag(void)
{
	struct proc *p;
	TAILQ_FOREACH(p, &runnable_scps, proc_link)
		printk("_S PID: %d\n", p->pid);
	TAILQ_FOREACH(p, &all_mcps, proc_link)
		printk("MCP PID: %d\n", p->pid);
	return;
}

void print_idlecoremap(void)
{
	spin_lock(&idle_lock);
	printk("There are %d idle cores.\n", num_idlecores);
	for (int i = 0; i < num_idlecores; i++)
		printk("idlecoremap[%d] = %d\n", i, idlecoremap[i]);
	spin_unlock(&idle_lock);
}
