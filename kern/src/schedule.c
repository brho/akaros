/* Copyright (c) 2009, 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Scheduling and dispatching. */

#include <schedule.h>
#include <process.h>
#include <monitor.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>
#include <smp.h>
#include <manager.h>
#include <alarm.h>
#include <sys/queue.h>
#include <kmalloc.h>
#include <arsc_server.h>

/* Process Lists.  'unrunnable' is a holding list for SCPs that are running or
 * waiting or otherwise not considered for sched decisions. */
struct proc_list unrunnable_scps = TAILQ_HEAD_INITIALIZER(unrunnable_scps);
struct proc_list runnable_scps = TAILQ_HEAD_INITIALIZER(runnable_scps);
/* mcp lists.  we actually could get by with one list and a TAILQ_CONCAT, but
 * I'm expecting to want the flexibility of the pointers later. */
struct proc_list all_mcps_1 = TAILQ_HEAD_INITIALIZER(all_mcps_1);
struct proc_list all_mcps_2 = TAILQ_HEAD_INITIALIZER(all_mcps_2);
struct proc_list *primary_mcps = &all_mcps_1;
struct proc_list *secondary_mcps = &all_mcps_2;

/* The pcores in the system.  (array gets alloced in init()).  */
struct sched_pcore *all_pcores;

/* TAILQ of all unallocated, idle (CG) cores */
struct sched_pcore_tailq idlecores = TAILQ_HEAD_INITIALIZER(idlecores);

/* Helper, defined below */
static void __core_request(struct proc *p, uint32_t amt_needed);
static void __put_idle_cores(struct proc *p, uint32_t *pc_arr, uint32_t num);
static void add_to_list(struct proc *p, struct proc_list *list);
static void remove_from_list(struct proc *p, struct proc_list *list);
static void switch_lists(struct proc *p, struct proc_list *old,
                         struct proc_list *new);
static uint32_t spc2pcoreid(struct sched_pcore *spc);
static struct sched_pcore *pcoreid2spc(uint32_t pcoreid);
static bool is_ll_core(uint32_t pcoreid);
static void __prov_track_alloc(struct proc *p, uint32_t pcoreid);
static void __prov_track_dealloc(struct proc *p, uint32_t pcoreid);
static void __prov_track_dealloc_bulk(struct proc *p, uint32_t *pc_arr,
                                      uint32_t nr_cores);
static void __run_mcp_ksched(void *arg);	/* don't call directly */
static uint32_t get_cores_needed(struct proc *p);

/* Locks / sync tools */

/* poke-style ksched - ensures the MCP ksched only runs once at a time.  since
 * only one mcp ksched runs at a time, while this is set, the ksched knows no
 * cores are being allocated by other code (though they could be dealloc, due to
 * yield). 
 *
 * The main value to this sync method is to make the 'make sure the ksched runs
 * only once at a time and that it actually runs' invariant/desire wait-free, so
 * that it can be called anywhere (deep event code, etc).
 *
 * As the ksched gets smarter, we'll probably embedd this poker in a bigger
 * struct that can handle the posting of different types of work. */
struct poke_tracker ksched_poker = POKE_INITIALIZER(__run_mcp_ksched);

/* this 'big ksched lock' protects a bunch of things, which i may make fine
 * grained: */
/* - protects the integrity of proc tailqs/structures, as well as the membership
 * of a proc on those lists.  proc lifetime within the ksched but outside this
 * lock is protected by the proc kref. */
//spinlock_t proclist_lock = SPINLOCK_INITIALIZER; /* subsumed by bksl */
/* - protects the provisioning assignment, membership of sched_pcores in
 * provision lists, and the integrity of all prov lists (the lists of each
 * proc).  does NOT protect spc->alloc_proc. */
//spinlock_t prov_lock = SPINLOCK_INITIALIZER;
/* - protects allocation structures: spc->alloc_proc, the integrity and
 * membership of the idelcores tailq. */
//spinlock_t alloc_lock = SPINLOCK_INITIALIZER;
spinlock_t sched_lock = SPINLOCK_INITIALIZER;

/* Alarm struct, for our example 'timer tick' */
struct alarm_waiter ksched_waiter;

#define TIMER_TICK_USEC 10000 	/* 10msec */

/* Helper: Sets up a timer tick on the calling core to go off 10 msec from now.
 * This assumes the calling core is an LL core, etc. */
static void set_ksched_alarm(void)
{
	set_awaiter_rel(&ksched_waiter, TIMER_TICK_USEC);
	set_alarm(&per_cpu_info[core_id()].tchain, &ksched_waiter);
}

/* Need a kmsg to just run the sched, but not to rearm */
static void __just_sched(uint32_t srcid, long a0, long a1, long a2)
{
	run_scheduler();
}

/* RKM alarm, to run the scheduler tick (not in interrupt context) and reset the
 * alarm.  Note that interrupts will be disabled, but this is not the same as
 * interrupt context.  We're a routine kmsg, which means the core is in a
 * quiescent state. */
static void __ksched_tick(struct alarm_waiter *waiter)
{
	/* TODO: imagine doing some accounting here */
	run_scheduler();
	/* Set our alarm to go off, incrementing from our last tick (instead of
	 * setting it relative to now, since some time has passed since the alarm
	 * first went off.  Note, this may be now or in the past! */
	set_awaiter_inc(&ksched_waiter, TIMER_TICK_USEC);
	set_alarm(&per_cpu_info[core_id()].tchain, &ksched_waiter);
}

void schedule_init(void)
{
	spin_lock(&sched_lock);
	/* init provisioning stuff */
	all_pcores = kmalloc(sizeof(struct sched_pcore) * num_cpus, 0);
	memset(all_pcores, 0, sizeof(struct sched_pcore) * num_cpus);
	assert(!core_id());		/* want the alarm on core0 for now */
	init_awaiter(&ksched_waiter, __ksched_tick);
	set_ksched_alarm();
	/* init the idlecore list.  if they turned off hyperthreading, give them the
	 * odds from 1..max-1.  otherwise, give them everything by 0 (default mgmt
	 * core).  TODO: (CG/LL) better LL/CG mgmt */
#ifndef CONFIG_DISABLE_SMT
	for (int i = 1; i < num_cpus; i++)
		TAILQ_INSERT_TAIL(&idlecores, pcoreid2spc(i), alloc_next);
#else
	assert(!(num_cpus % 2));
	for (int i = 1; i < num_cpus; i += 2)
		TAILQ_INSERT_TAIL(&idlecores, pcoreid2spc(i), alloc_next);
#endif /* CONFIG_DISABLE_SMT */
	spin_unlock(&sched_lock);

#ifdef CONFIG_ARSC_SERVER
	int arsc_coreid = get_any_idle_core();
	assert(arsc_coreid >= 0);
	send_kernel_message(arsc_coreid, arsc_server, 0, 0, 0, KMSG_ROUTINE);
	printk("Using core %d for the ARSC server\n", arsc_coreid);
#endif /* CONFIG_ARSC_SERVER */
}

static uint32_t spc2pcoreid(struct sched_pcore *spc)
{
	return spc - all_pcores;
}

static struct sched_pcore *pcoreid2spc(uint32_t pcoreid)
{
	return &all_pcores[pcoreid];
}

/* Round-robins on whatever list it's on */
static void add_to_list(struct proc *p, struct proc_list *new)
{
	assert(!(p->ksched_data.cur_list));
	TAILQ_INSERT_TAIL(new, p, ksched_data.proc_link);
	p->ksched_data.cur_list = new;
}

static void remove_from_list(struct proc *p, struct proc_list *old)
{
	assert(p->ksched_data.cur_list == old);
	TAILQ_REMOVE(old, p, ksched_data.proc_link);
	p->ksched_data.cur_list = 0;
}

static void switch_lists(struct proc *p, struct proc_list *old,
                         struct proc_list *new)
{
	remove_from_list(p, old);
	add_to_list(p, new);
}

/* Removes from whatever list p is on */
static void remove_from_any_list(struct proc *p)
{
	if (p->ksched_data.cur_list) {
		TAILQ_REMOVE(p->ksched_data.cur_list, p, ksched_data.proc_link);
		p->ksched_data.cur_list = 0;
	}
}

/************** Process Management Callbacks **************/
/* a couple notes:
 * - the proc lock is NOT held for any of these calls.  currently, there is no
 *   lock ordering between the sched lock and the proc lock.  since the proc
 *   code doesn't know what we do, it doesn't hold its lock when calling our
 *   CBs.
 * - since the proc lock isn't held, the proc could be dying, which means we
 *   will receive a __sched_proc_destroy() either before or after some of these
 *   other CBs.  the CBs related to list management need to check and abort if
 *   DYING */
void __sched_proc_register(struct proc *p)
{
	assert(p->state != PROC_DYING);	/* shouldn't be abel to happen yet */
	/* one ref for the proc's existence, cradle-to-grave */
	proc_incref(p, 1);	/* need at least this OR the 'one for existing' */
	spin_lock(&sched_lock);
	TAILQ_INIT(&p->ksched_data.prov_alloc_me);
	TAILQ_INIT(&p->ksched_data.prov_not_alloc_me);
	add_to_list(p, &unrunnable_scps);
	spin_unlock(&sched_lock);
}

/* Returns 0 if it succeeded, an error code otherwise. */
void __sched_proc_change_to_m(struct proc *p)
{
	spin_lock(&sched_lock);
	/* Need to make sure they aren't dying.  if so, we already dealt with their
	 * list membership, etc (or soon will).  taking advantage of the 'immutable
	 * state' of dying (so long as refs are held). */
	if (p->state == PROC_DYING) {
		spin_unlock(&sched_lock);
		return;
	}
	/* Catch user bugs */
	if (!p->procdata->res_req[RES_CORES].amt_wanted) {
		printk("[kernel] process needs to specify amt_wanted\n");
		p->procdata->res_req[RES_CORES].amt_wanted = 1;
	}
	/* For now, this should only ever be called on an unrunnable.  It's
	 * probably a bug, at this stage in development, to do o/w. */
	remove_from_list(p, &unrunnable_scps);
	//remove_from_any_list(p); 	/* ^^ instead of this */
	add_to_list(p, primary_mcps);
	spin_unlock(&sched_lock);
	//poke_ksched(p, RES_CORES);
}

/* Helper for the destroy CB : unprovisions any pcores for the given list */
static void unprov_pcore_list(struct sched_pcore_tailq *list_head)
{
	struct sched_pcore *spc_i;
	/* We can leave them connected within the tailq, since the scps don't have a
	 * default list (if they aren't on a proc's list, then we don't care about
	 * them), and since the INSERTs don't care what list you were on before
	 * (chummy with the implementation).  Pretty sure this is right.  If there's
	 * suspected list corruption, be safer here. */
	TAILQ_FOREACH(spc_i, list_head, prov_next)
		spc_i->prov_proc = 0;
	TAILQ_INIT(list_head);
}

/* Sched callback called when the proc dies.  pc_arr holds the cores the proc
 * had, if any, and nr_cores tells us how many are in the array.
 *
 * An external, edible ref is passed in.  when we return and they decref,
 * __proc_free will be called (when the last one is done). */
void __sched_proc_destroy(struct proc *p, uint32_t *pc_arr, uint32_t nr_cores)
{
	spin_lock(&sched_lock);
	/* Unprovision any cores.  Note this is different than track_dealloc.
	 * The latter does bookkeeping when an allocation changes.  This is a
	 * bulk *provisioning* change. */
	unprov_pcore_list(&p->ksched_data.prov_alloc_me);
	unprov_pcore_list(&p->ksched_data.prov_not_alloc_me);
	/* Remove from whatever list we are on (if any - might not be on one if it
	 * was in the middle of __run_mcp_sched) */
	remove_from_any_list(p);
	if (nr_cores) {
		__put_idle_cores(p, pc_arr, nr_cores);
		__prov_track_dealloc_bulk(p, pc_arr, nr_cores);
	}
	spin_unlock(&sched_lock);
	/* Drop the cradle-to-the-grave reference, jet-li */
	proc_decref(p);
}

/* ksched callbacks.  p just woke up and is UNLOCKED. */
void __sched_mcp_wakeup(struct proc *p)
{
	spin_lock(&sched_lock);
	if (p->state == PROC_DYING) {
		spin_unlock(&sched_lock);
		return;
	}
	/* could try and prioritize p somehow (move it to the front of the list). */
	spin_unlock(&sched_lock);
	/* note they could be dying at this point too. */
	poke(&ksched_poker, p);
}

/* ksched callbacks.  p just woke up and is UNLOCKED. */
void __sched_scp_wakeup(struct proc *p)
{
	spin_lock(&sched_lock);
	if (p->state == PROC_DYING) {
		spin_unlock(&sched_lock);
		return;
	}
	/* might not be on a list if it is new.  o/w, it should be unrunnable */
	remove_from_any_list(p);
	add_to_list(p, &runnable_scps);
	spin_unlock(&sched_lock);
	/* we could be on a CG core, and all the mgmt cores could be halted.  if we
	 * don't tell one of them about the new proc, they will sleep until the
	 * timer tick goes off. */
	if (!management_core()) {
		/* TODO: pick a better core and only send if halted.
		 *
		 * FYI, a POKE on x86 might lose a rare race with halt code, since the
		 * poke handler does not abort halts.  if this happens, the next timer
		 * IRQ would wake up the core.
		 *
		 * ideally, we'd know if a specific mgmt core is sleeping and wake it
		 * up.  o/w, we could interrupt an already-running mgmt core that won't
		 * get to our new proc anytime soon.  also, by poking core 0, a
		 * different mgmt core could remain idle (and this process would sleep)
		 * until its tick goes off */
		send_ipi(0, I_POKE_CORE);
	}
}

/* Callback to return a core to the ksched, which tracks it as idle and
 * deallocated from p.  The proclock is held (__core_req depends on that).
 *
 * This also is a trigger, telling us we have more cores.  We could/should make
 * a scheduling decision (or at least plan to). */
void __sched_put_idle_core(struct proc *p, uint32_t coreid)
{
	struct sched_pcore *spc = pcoreid2spc(coreid);
	spin_lock(&sched_lock);
	TAILQ_INSERT_TAIL(&idlecores, spc, alloc_next);
	__prov_track_dealloc(p, coreid);
	spin_unlock(&sched_lock);
}

/* Helper for put_idle and core_req.  Note this does not track_dealloc.  When we
 * get rid of / revise proc_preempt_all and put_idle_cores, we can get rid of
 * this.  (the ksched will never need it - only external callers). */
static void __put_idle_cores(struct proc *p, uint32_t *pc_arr, uint32_t num)
{
	struct sched_pcore *spc_i;
	for (int i = 0; i < num; i++) {
		spc_i = pcoreid2spc(pc_arr[i]);
		TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
	}
}

/* Callback, bulk interface for put_idle.  Note this one also calls track_dealloc,
 * which the internal version does not.  The proclock is held for this. */
void __sched_put_idle_cores(struct proc *p, uint32_t *pc_arr, uint32_t num)
{
	spin_lock(&sched_lock);
	/* TODO: when we revise this func, look at __put_idle */
	__put_idle_cores(p, pc_arr, num);
	__prov_track_dealloc_bulk(p, pc_arr, num);
	spin_unlock(&sched_lock);
	/* could trigger a sched decision here */
}

/* mgmt/LL cores should call this to schedule the calling core and give it to an
 * SCP.  will also prune the dead SCPs from the list.  hold the lock before
 * calling.  returns TRUE if it scheduled a proc. */
static bool __schedule_scp(void)
{
	// TODO: sort out lock ordering (proc_run_s also locks)
	struct proc *p;
	uint32_t pcoreid = core_id();
	struct per_cpu_info *pcpui = &per_cpu_info[pcoreid];
	int8_t state = 0;
	/* if there are any runnables, run them here and put any currently running
	 * SCP on the tail of the runnable queue. */
	if ((p = TAILQ_FIRST(&runnable_scps))) {
		/* protect owning proc, cur_ctx, etc.  note this nests with the
		 * calls in proc_yield_s */
		disable_irqsave(&state);
		/* someone is currently running, dequeue them */
		if (pcpui->owning_proc) {
			spin_lock(&pcpui->owning_proc->proc_lock);
			/* process might be dying, with a KMSG to clean it up waiting on
			 * this core.  can't do much, so we'll attempt to restart */
			if (pcpui->owning_proc->state == PROC_DYING) {
				send_kernel_message(core_id(), __just_sched, 0, 0, 0,
				                    KMSG_ROUTINE);
				spin_unlock(&pcpui->owning_proc->proc_lock);
				enable_irqsave(&state);
				return FALSE;
			}
			printd("Descheduled %d in favor of %d\n", pcpui->owning_proc->pid,
			       p->pid);
			__proc_set_state(pcpui->owning_proc, PROC_RUNNABLE_S);
			/* Saving FP state aggressively.  Odds are, the SCP was hit by an
			 * IRQ and has a HW ctx, in which case we must save. */
			__proc_save_fpu_s(pcpui->owning_proc);
			__proc_save_context_s(pcpui->owning_proc, pcpui->cur_ctx);
			vcore_account_offline(pcpui->owning_proc, 0);
			__seq_start_write(&p->procinfo->coremap_seqctr);
			__unmap_vcore(p, 0);
			__seq_end_write(&p->procinfo->coremap_seqctr);
			spin_unlock(&pcpui->owning_proc->proc_lock);
			/* round-robin the SCPs (inserts at the end of the queue) */
			switch_lists(pcpui->owning_proc, &unrunnable_scps, &runnable_scps);
			clear_owning_proc(pcoreid);
			/* Note we abandon core.  It's not strictly necessary.  If
			 * we didn't, the TLB would still be loaded with the old
			 * one, til we proc_run_s, and the various paths in
			 * proc_run_s would pick it up.  This way is a bit safer for
			 * future changes, but has an extra (empty) TLB flush.  */
			abandon_core();
		} 
		/* Run the new proc */
		switch_lists(p, &runnable_scps, &unrunnable_scps);
		printd("PID of the SCP i'm running: %d\n", p->pid);
		proc_run_s(p);	/* gives it core we're running on */
		enable_irqsave(&state);
		return TRUE;
	}
	return FALSE;
}

/* Returns how many new cores p needs.  This doesn't lock the proc, so your
 * answer might be stale. */
static uint32_t get_cores_needed(struct proc *p)
{
	uint32_t amt_wanted, amt_granted;
	amt_wanted = p->procdata->res_req[RES_CORES].amt_wanted;
	/* Help them out - if they ask for something impossible, give them 1 so they
	 * can make some progress. (this is racy, and unnecessary). */
	if (amt_wanted > p->procinfo->max_vcores) {
		printk("[kernel] proc %d wanted more than max, wanted %d\n", p->pid,
		       amt_wanted);
		p->procdata->res_req[RES_CORES].amt_wanted = 1;
		amt_wanted = 1;
	}
	/* There are a few cases where amt_wanted is 0, but they are still RUNNABLE
	 * (involving yields, events, and preemptions).  In these cases, give them
	 * at least 1, so they can make progress and yield properly.  If they are
	 * not WAITING, they did not yield and may have missed a message. */
	if (!amt_wanted) {
		/* could ++, but there could be a race and we don't want to give them
		 * more than they ever asked for (in case they haven't prepped) */
		p->procdata->res_req[RES_CORES].amt_wanted = 1;
		amt_wanted = 1;
	}
	/* amt_granted is racy - they could be *yielding*, but currently they can't
	 * be getting any new cores if the caller is in the mcp_ksched.  this is
	 * okay - we won't accidentally give them more cores than they *ever* wanted
	 * (which could crash them), but our answer might be a little stale. */
	amt_granted = p->procinfo->res_grant[RES_CORES];
	/* Do not do an assert like this: it could fail (yield in progress): */
	//assert(amt_granted == p->procinfo->num_vcores);
	if (amt_wanted <= amt_granted)
		return 0;
	return amt_wanted - amt_granted;
}

/* Actual work of the MCP kscheduler.  if we were called by poke_ksched, *arg
 * might be the process who wanted special service.  this would be the case if
 * we weren't already running the ksched.  Sort of a ghetto way to "post work",
 * such that it's an optimization. */
static void __run_mcp_ksched(void *arg)
{
	struct proc *p, *temp;
	uint32_t amt_needed;
	struct proc_list *temp_mcp_list;
	/* locking to protect the MCP lists' integrity and membership */
	spin_lock(&sched_lock);
	/* 2-pass scheme: check each proc on the primary list (FCFS).  if they need
	 * nothing, put them on the secondary list.  if they need something, rip
	 * them off the list, service them, and if they are still not dying, put
	 * them on the secondary list.  We cull the entire primary list, so that
	 * when we start from the beginning each time, we aren't repeatedly checking
	 * procs we looked at on previous waves.
	 *
	 * TODO: we could modify this such that procs that we failed to service move
	 * to yet another list or something.  We can also move the WAITINGs to
	 * another list and have wakeup move them back, etc. */
	while (!TAILQ_EMPTY(primary_mcps)) {
		TAILQ_FOREACH_SAFE(p, primary_mcps, ksched_data.proc_link, temp) {
			if (p->state == PROC_WAITING) {	/* unlocked peek at the state */
				switch_lists(p, primary_mcps, secondary_mcps);
				continue;
			}
			amt_needed = get_cores_needed(p);
			if (!amt_needed) {
				switch_lists(p, primary_mcps, secondary_mcps);
				continue;
			}
			/* o/w, we want to give cores to this proc */
			remove_from_list(p, primary_mcps);
			/* now it won't die, but it could get removed from lists and have
			 * its stuff unprov'd when we unlock */
			proc_incref(p, 1);
			/* GIANT WARNING: __core_req will unlock the sched lock for a bit.
			 * It will return with it locked still.  We could unlock before we
			 * pass in, but they will relock right away. */
			// notionally_unlock(&ksched_lock);	/* for mouse-eyed viewers */
			__core_request(p, amt_needed);
			// notionally_lock(&ksched_lock);
			/* Peeking at the state is okay, since we hold a ref.  Once it is
			 * DYING, it'll remain DYING until we decref.  And if there is a
			 * concurrent death, that will spin on the ksched lock (which we
			 * hold, and which protects the proc lists). */
			if (p->state != PROC_DYING)
				add_to_list(p, secondary_mcps);
			proc_decref(p);			/* fyi, this may trigger __proc_free */
			/* need to break: the proc lists may have changed when we unlocked
			 * in core_req in ways that the FOREACH_SAFE can't handle. */
			break;
		}
	}
	/* at this point, we moved all the procs over to the secondary list, and
	 * attempted to service the ones that wanted something.  now just swap the
	 * lists for the next invocation of the ksched. */
	temp_mcp_list = primary_mcps;
	primary_mcps = secondary_mcps;
	secondary_mcps = temp_mcp_list;
	spin_unlock(&sched_lock);
}

/* Something has changed, and for whatever reason the scheduler should
 * reevaluate things. 
 *
 * Don't call this if you are processing a syscall or otherwise care about your
 * kthread variables, cur_proc/owning_proc, etc.
 *
 * Don't call this from interrupt context (grabs proclocks). */
void run_scheduler(void)
{
	/* MCP scheduling: post work, then poke.  for now, i just want the func to
	 * run again, so merely a poke is sufficient. */
	poke(&ksched_poker, 0);
	if (management_core()) {
		spin_lock(&sched_lock);
		__schedule_scp();
		spin_unlock(&sched_lock);
	}
}

/* A process is asking the ksched to look at its resource desires.  The
 * scheduler is free to ignore this, for its own reasons, so long as it
 * eventually gets around to looking at resource desires. */
void poke_ksched(struct proc *p, unsigned int res_type)
{
	/* ignoring res_type for now.  could post that if we wanted (would need some
	 * other structs/flags) */
	if (!__proc_is_mcp(p))
		return;
	poke(&ksched_poker, p);
}

/* The calling cpu/core has nothing to do and plans to idle/halt.  This is an
 * opportunity to pick the nature of that halting (low power state, etc), or
 * provide some other work (_Ss on LL cores).  Note that interrupts are
 * disabled, and if you return, the core will cpu_halt(). */
void cpu_bored(void)
{
	bool new_proc = FALSE;
	if (!management_core())
		return;
	spin_lock(&sched_lock);
	new_proc = __schedule_scp();
	spin_unlock(&sched_lock);
	/* if we just scheduled a proc, we need to manually restart it, instead of
	 * returning.  if we return, the core will halt. */
	if (new_proc) {
		proc_restartcore();
		assert(0);
	}
	/* Could drop into the monitor if there are no processes at all.  For now,
	 * the 'call of the giraffe' suffices. */
}

/* Available resources changed (plus or minus).  Some parts of the kernel may
 * call this if a particular resource that is 'quantity-based' changes.  Things
 * like available RAM to processes, bandwidth, etc.  Cores would probably be
 * inappropriate, since we need to know which specific core is now free. */
void avail_res_changed(int res_type, long change)
{
	printk("[kernel] ksched doesn't track any resources yet!\n");
}

int get_any_idle_core(void)
{
	struct sched_pcore *spc;
	int ret = -1;
	spin_lock(&sched_lock);
	while ((spc = TAILQ_FIRST(&idlecores))) {
		/* Don't take cores that are provisioned to a process */
		if (spc->prov_proc)
			continue;
		assert(!spc->alloc_proc);
		TAILQ_REMOVE(&idlecores, spc, alloc_next);
		ret = spc2pcoreid(spc);
		break;
	}
	spin_unlock(&sched_lock);
	return ret;
}

/* TODO: if we end up using this a lot, track CG-idleness as a property of the
 * SPC instead of doing a linear search. */
static bool __spc_is_idle(struct sched_pcore *spc)
{
	struct sched_pcore *i;
	TAILQ_FOREACH(i, &idlecores, alloc_next) {
		if (spc == i)
			return TRUE;
	}
	return FALSE;
}

int get_this_idle_core(int coreid)
{
	struct sched_pcore *spc = pcoreid2spc(coreid);
	int ret = -1;
	assert((0 <= coreid) && (coreid < num_cpus));
	spin_lock(&sched_lock);
	if (__spc_is_idle(pcoreid2spc(coreid)) && !spc->prov_proc) {
		assert(!spc->alloc_proc);
		TAILQ_REMOVE(&idlecores, spc, alloc_next);
		ret = coreid;
	}
	spin_unlock(&sched_lock);
	return ret;
}

/* similar to __sched_put_idle_core, but without the prov tracking */
void put_idle_core(int coreid)
{
	struct sched_pcore *spc = pcoreid2spc(coreid);
	assert((0 <= coreid) && (coreid < num_cpus));
	spin_lock(&sched_lock);
	TAILQ_INSERT_TAIL(&idlecores, spc, alloc_next);
	spin_unlock(&sched_lock);
}

/* Normally it'll be the max number of CG cores ever */
uint32_t max_vcores(struct proc *p)
{
/* TODO: (CG/LL) */
#ifdef CONFIG_DISABLE_SMT
	return num_cpus >> 1;
#else
	return num_cpus - 1;	/* reserving core 0 */
#endif /* CONFIG_DISABLE_SMT */
}

/* This deals with a request for more cores.  The amt of new cores needed is
 * passed in.  The ksched lock is held, but we are free to unlock if we want
 * (and we must, if calling out of the ksched to anything high-level).
 *
 * Side note: if we want to warn, then we can't deal with this proc's prov'd
 * cores until we wait til the alarm goes off.  would need to put all
 * alarmed cores on a list and wait til the alarm goes off to do the full
 * preempt.  and when those cores come in voluntarily, we'd need to know to
 * give them to this proc. */
static void __core_request(struct proc *p, uint32_t amt_needed)
{
	uint32_t nr_to_grant = 0;
	uint32_t corelist[num_cpus];
	struct sched_pcore *spc_i, *temp;
	struct proc *proc_to_preempt;
	bool success;
	/* we come in holding the ksched lock, and we hold it here to protect
	 * allocations and provisioning. */
	/* get all available cores from their prov_not_alloc list.  the list might
	 * change when we unlock (new cores added to it, or the entire list emptied,
	 * but no core allocations will happen (we hold the poke)). */
	while (!TAILQ_EMPTY(&p->ksched_data.prov_not_alloc_me)) {
		if (nr_to_grant == amt_needed)
			break;
		/* picking the next victim (first on the not_alloc list) */
		spc_i = TAILQ_FIRST(&p->ksched_data.prov_not_alloc_me);
		/* someone else has this proc's pcore, so we need to try to preempt.
		 * after this block, the core will be tracked dealloc'd and on the idle
		 * list (regardless of whether we had to preempt or not) */
		if (spc_i->alloc_proc) {
			proc_to_preempt = spc_i->alloc_proc;
			/* would break both preemption and maybe the later decref */
			assert(proc_to_preempt != p);
			/* need to keep a valid, external ref when we unlock */
			proc_incref(proc_to_preempt, 1);
			spin_unlock(&sched_lock);
			/* sending no warning time for now - just an immediate preempt. */
			success = proc_preempt_core(proc_to_preempt, spc2pcoreid(spc_i), 0);
			/* reaquire locks to protect provisioning and idle lists */
			spin_lock(&sched_lock);
			if (success) {
				/* we preempted it before the proc could yield or die.
				 * alloc_proc should not have changed (it'll change in death and
				 * idle CBs).  the core is not on the idle core list.  (if we
				 * ever have proc alloc lists, it'll still be on the old proc's
				 * list). */
				assert(spc_i->alloc_proc);
				/* regardless of whether or not it is still prov to p, we need
				 * to note its dealloc.  we are doing some excessive checking of
				 * p == prov_proc, but using this helper is a lot clearer. */
				__prov_track_dealloc(proc_to_preempt, spc2pcoreid(spc_i));
				/* here, we rely on the fact that we are the only preemptor.  we
				 * assume no one else preempted it, so we know it is available*/
				TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
			} else {
				/* the preempt failed, which should only happen if the pcore was
				 * unmapped (could be dying, could be yielding, but NOT
				 * preempted).  whoever unmapped it also triggered (or will soon
				 * trigger) a track_dealloc and put it on the idle list.  our
				 * signal for this is spc_i->alloc_proc being 0.  We need to
				 * spin and let whoever is trying to free the core grab the
				 * ksched lock.  We could use an 'ignore_next_idle' flag per
				 * sched_pcore, but it's not critical anymore.
				 *
				 * Note, we're relying on us being the only preemptor - if the
				 * core was unmapped by *another* preemptor, there would be no
				 * way of knowing the core was made idle *yet* (the success
				 * branch in another thread).  likewise, if there were another
				 * allocator, the pcore could have been put on the idle list and
				 * then quickly removed/allocated. */
				cmb();
				while (spc_i->alloc_proc) {
					/* this loop should be very rare */
					spin_unlock(&sched_lock);
					udelay(1);
					spin_lock(&sched_lock);
				}
			}
			/* no longer need to keep p_to_pre alive */
			proc_decref(proc_to_preempt);
			/* might not be prov to p anymore (rare race).  spc_i is idle - we
			 * might get it later, or maybe we'll give it to its rightful proc*/
			if (spc_i->prov_proc != p)
				continue;
		}
		/* at this point, the pcore is idle, regardless of how we got here
		 * (successful preempt, failed preempt, or it was idle in the first
		 * place.  the core is still provisioned.  lets pull from the idle list
		 * and add it to the pc_arr for p.  here, we rely on the fact that we
		 * are the only allocator (spc_i is still idle, despite unlocking). */
		TAILQ_REMOVE(&idlecores, spc_i, alloc_next);
		/* At this point, we have the core, ready to try to give it to the proc.
		 * It is on no alloc lists, and is track_dealloc'd() (regardless of how
		 * we got here).
		 *
		 * We'll give p its cores via a bulk list, which is better for the proc
		 * mgmt code (when going from runnable to running). */
		corelist[nr_to_grant] = spc2pcoreid(spc_i);
		nr_to_grant++;
		__prov_track_alloc(p, spc2pcoreid(spc_i));
	}
	/* Try to get cores from the idle list that aren't prov to me (FCFS) */
	TAILQ_FOREACH_SAFE(spc_i, &idlecores, alloc_next, temp) {
		if (nr_to_grant == amt_needed)
			break;
		TAILQ_REMOVE(&idlecores, spc_i, alloc_next);
		corelist[nr_to_grant] = spc2pcoreid(spc_i);
		nr_to_grant++;
		__prov_track_alloc(p, spc2pcoreid(spc_i));
	}
	/* Now, actually give them out */
	if (nr_to_grant) {
		/* Need to unlock before calling out to proc code.  We are somewhat
		 * relying on being the only one allocating 'thread' here, since another
		 * allocator could have seen these cores (if they are prov to some proc)
		 * and could be trying to give them out (and assuming they are already
		 * on the idle list). */
		spin_unlock(&sched_lock);
		/* give them the cores.  this will start up the extras if RUNNING_M. */
		spin_lock(&p->proc_lock);
		/* if they fail, it is because they are WAITING or DYING.  we could give
		 * the cores to another proc or whatever.  for the current type of
		 * ksched, we'll just put them back on the pile and return.  Note, the
		 * ksched could check the states after locking, but it isn't necessary:
		 * just need to check at some point in the ksched loop. */
		if (__proc_give_cores(p, corelist, nr_to_grant)) {
			spin_unlock(&p->proc_lock);
			/* we failed, put the cores and track their dealloc.  lock is
			 * protecting those structures. */
			spin_lock(&sched_lock);
			__put_idle_cores(p, corelist, nr_to_grant);
			__prov_track_dealloc_bulk(p, corelist, nr_to_grant);
		} else {
			/* at some point after giving cores, call proc_run_m() (harmless on
			 * RUNNING_Ms).  You can give small groups of cores, then run them
			 * (which is more efficient than interleaving runs with the gives
			 * for bulk preempted processes). */
			__proc_run_m(p);
			spin_unlock(&p->proc_lock);
			/* main mcp_ksched wants this held (it came to __core_req held) */
			spin_lock(&sched_lock);
		}
	}
	/* note the ksched lock is still held */
}

/* TODO: need more thorough CG/LL management.  For now, core0 is the only LL
 * core.  This won't play well with the ghetto shit in schedule_init() if you do
 * anything like 'DEDICATED_MONITOR' or the ARSC server.  All that needs an
 * overhaul. */
static bool is_ll_core(uint32_t pcoreid)
{
	if (pcoreid == 0)
		return TRUE;
	return FALSE;
}

/* Helper, makes sure the prov/alloc structures track the pcore properly when it
 * is allocated to p.  Might make this take a sched_pcore * in the future. */
static void __prov_track_alloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	assert(pcoreid < num_cpus);		/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	assert(spc->alloc_proc != p);	/* corruption or double-alloc */
	spc->alloc_proc = p;
	/* if the pcore is prov to them and now allocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.prov_not_alloc_me, spc, prov_next);
		TAILQ_INSERT_TAIL(&p->ksched_data.prov_alloc_me, spc, prov_next);
	}
}

/* Helper, makes sure the prov/alloc structures track the pcore properly when it
 * is deallocated from p. */
static void __prov_track_dealloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	assert(pcoreid < num_cpus);		/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	spc->alloc_proc = 0;
	/* if the pcore is prov to them and now deallocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.prov_alloc_me, spc, prov_next);
		/* this is the victim list, which can be sorted so that we pick the
		 * right victim (sort by alloc_proc reverse priority, etc).  In this
		 * case, the core isn't alloc'd by anyone, so it should be the first
		 * victim. */
		TAILQ_INSERT_HEAD(&p->ksched_data.prov_not_alloc_me, spc, prov_next);
	}
}

/* Bulk interface for __prov_track_dealloc */
static void __prov_track_dealloc_bulk(struct proc *p, uint32_t *pc_arr,
                                      uint32_t nr_cores)
{
	for (int i = 0; i < nr_cores; i++)
		__prov_track_dealloc(p, pc_arr[i]);
}

/* P will get pcore if it needs more cores next time we look at it */
int provision_core(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;
	struct sched_pcore_tailq *prov_list;
	/* Make sure we aren't asking for something that doesn't exist (bounds check
	 * on the pcore array) */
	if (!(pcoreid < num_cpus)) {
		set_errno(ENXIO);
		return -1;
	}
	/* Don't allow the provisioning of LL cores */
	if (is_ll_core(pcoreid)) {
		set_errno(EBUSY);
		return -1;
	}
	spc = pcoreid2spc(pcoreid);
	/* Note the sched lock protects the spc tailqs for all procs in this code.
	 * If we need a finer grained sched lock, this is one place where we could
	 * have a different lock */
	spin_lock(&sched_lock);
	/* If the core is already prov to someone else, take it away.  (last write
	 * wins, some other layer or new func can handle permissions). */
	if (spc->prov_proc) {
		/* the list the spc is on depends on whether it is alloced to the
		 * prov_proc or not */
		prov_list = (spc->alloc_proc == spc->prov_proc ?
		             &spc->prov_proc->ksched_data.prov_alloc_me :
		             &spc->prov_proc->ksched_data.prov_not_alloc_me);
		TAILQ_REMOVE(prov_list, spc, prov_next);
	}
	/* Now prov it to p.  Again, the list it goes on depends on whether it is
	 * alloced to p or not.  Callers can also send in 0 to de-provision. */
	if (p) {
		if (spc->alloc_proc == p) {
			TAILQ_INSERT_TAIL(&p->ksched_data.prov_alloc_me, spc, prov_next);
		} else {
			/* this is be the victim list, which can be sorted so that we pick
			 * the right victim (sort by alloc_proc reverse priority, etc). */
			TAILQ_INSERT_TAIL(&p->ksched_data.prov_not_alloc_me, spc,
			                  prov_next);
		}
	}
	spc->prov_proc = p;
	spin_unlock(&sched_lock);
	return 0;
}

/************** Debugging **************/
void sched_diag(void)
{
	struct proc *p;
	spin_lock(&sched_lock);
	TAILQ_FOREACH(p, &runnable_scps, ksched_data.proc_link)
		printk("Runnable _S PID: %d\n", p->pid);
	TAILQ_FOREACH(p, &unrunnable_scps, ksched_data.proc_link)
		printk("Unrunnable _S PID: %d\n", p->pid);
	TAILQ_FOREACH(p, primary_mcps, ksched_data.proc_link)
		printk("Primary MCP PID: %d\n", p->pid);
	TAILQ_FOREACH(p, secondary_mcps, ksched_data.proc_link)
		printk("Secondary MCP PID: %d\n", p->pid);
	spin_unlock(&sched_lock);
	return;
}

void print_idlecoremap(void)
{
	struct sched_pcore *spc_i;
	/* not locking, so we can look at this without deadlocking. */
	printk("Idle cores (unlocked!):\n");
	TAILQ_FOREACH(spc_i, &idlecores, alloc_next)
		printk("Core %d, prov to %d (%p)\n", spc2pcoreid(spc_i),
		       spc_i->prov_proc ? spc_i->prov_proc->pid : 0, spc_i->prov_proc);
}

void print_resources(struct proc *p)
{
	printk("--------------------\n");
	printk("PID: %d\n", p->pid);
	printk("--------------------\n");
	for (int i = 0; i < MAX_NUM_RESOURCES; i++)
		printk("Res type: %02d, amt wanted: %08d, amt granted: %08d\n", i,
		       p->procdata->res_req[i].amt_wanted, p->procinfo->res_grant[i]);
}

void print_all_resources(void)
{
	/* Hash helper */
	void __print_resources(void *item)
	{
		print_resources((struct proc*)item);
	}
	spin_lock(&pid_hash_lock);
	hash_for_each(pid_hash, __print_resources);
	spin_unlock(&pid_hash_lock);
}

void print_prov_map(void)
{
	struct sched_pcore *spc_i;
	/* Doing this unlocked, which is dangerous, but won't deadlock */
	printk("Which cores are provisioned to which procs:\n------------------\n");
	for (int i = 0; i < num_cpus; i++) {
		spc_i = pcoreid2spc(i);
		printk("Core %02d, prov: %d(%p) alloc: %d(%p)\n", i,
		       spc_i->prov_proc ? spc_i->prov_proc->pid : 0, spc_i->prov_proc,
		       spc_i->alloc_proc ? spc_i->alloc_proc->pid : 0,
		       spc_i->alloc_proc);
	}
}

void print_proc_prov(struct proc *p)
{
	struct sched_pcore *spc_i;
	if (!p)
		return;
	printk("Prov cores alloced to proc %d (%p)\n----------\n", p->pid, p);
	TAILQ_FOREACH(spc_i, &p->ksched_data.prov_alloc_me, prov_next)
		printk("Pcore %d\n", spc2pcoreid(spc_i));
	printk("Prov cores not alloced to proc %d (%p)\n----------\n", p->pid, p);
	TAILQ_FOREACH(spc_i, &p->ksched_data.prov_not_alloc_me, prov_next)
		printk("Pcore %d (alloced to %d (%p))\n", spc2pcoreid(spc_i),
		       spc_i->alloc_proc ? spc_i->alloc_proc->pid : 0,
		       spc_i->alloc_proc);
}

void next_core(uint32_t pcoreid)
{
	struct sched_pcore *spc_i;
	bool match = FALSE;
	spin_lock(&sched_lock);
	TAILQ_FOREACH(spc_i, &idlecores, alloc_next) {
		if (spc2pcoreid(spc_i) == pcoreid) {
			match = TRUE;
			break;
		}
	}
	if (match) {
		TAILQ_REMOVE(&idlecores, spc_i, alloc_next);
		TAILQ_INSERT_HEAD(&idlecores, spc_i, alloc_next);
		printk("Pcore %d will be given out next (from the idles)\n", pcoreid);
	}
	spin_unlock(&sched_lock);
}

void sort_idles(void)
{
	struct sched_pcore *spc_i, *spc_j, *temp;
	struct sched_pcore_tailq sorter = TAILQ_HEAD_INITIALIZER(sorter);
	bool added;
	spin_lock(&sched_lock);
	TAILQ_CONCAT(&sorter, &idlecores, alloc_next);
	TAILQ_FOREACH_SAFE(spc_i, &sorter, alloc_next, temp) {
		TAILQ_REMOVE(&sorter, spc_i, alloc_next);
		added = FALSE;
		/* don't need foreach_safe since we break after we muck with the list */
		TAILQ_FOREACH(spc_j, &idlecores, alloc_next) {
			if (spc_i < spc_j) {
				TAILQ_INSERT_BEFORE(spc_j, spc_i, alloc_next);
				added = TRUE;
				break;
			}
		}
		if (!added)
			TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
	}
	spin_unlock(&sched_lock);
}
