/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * 2LS for virtual machines */

#include <vmm/sched.h>
#include <vmm/vmm.h>
#include <vmm/vthread.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <assert.h>
#include <parlib/spinlock.h>
#include <parlib/event.h>
#include <parlib/ucq.h>
#include <parlib/arch/trap.h>
#include <parlib/ros_debug.h>
#include <parlib/vcore_tick.h>
#include <parlib/slab.h>

int vmm_sched_period_usec = 1000;

/* For now, we only have one VM managed by the 2LS.  If we ever expand that,
 * we'll need something analogous to current_uthread, so the 2LS knows which VM
 * it is working on. */
static struct virtual_machine *current_vm;

static struct spin_pdr_lock queue_lock = SPINPDR_INITIALIZER;
/* Runnable queues, broken up by thread type. */
static struct vmm_thread_tq rnbl_tasks = TAILQ_HEAD_INITIALIZER(rnbl_tasks);
static struct vmm_thread_tq rnbl_guests = TAILQ_HEAD_INITIALIZER(rnbl_guests);
static struct vmm_thread **greedy_rnbl_guests;
/* Counts of *unblocked* threads.  Unblocked = Running + Runnable. */
static atomic_t nr_unblk_tasks;
static atomic_t nr_unblk_guests;
/* Global evq for all syscalls.  Could make this per vcore or whatever. */
static struct event_queue *sysc_evq;
static struct kmem_cache *task_thread_cache;

static void vmm_sched_init(void);
static void vmm_sched_entry(void);
static void vmm_thread_runnable(struct uthread *uth);
static void vmm_thread_paused(struct uthread *uth);
static void vmm_thread_blockon_sysc(struct uthread *uth, void *sysc);
static void vmm_thread_has_blocked(struct uthread *uth, int flags);
static void vmm_thread_refl_fault(struct uthread *uth,
                                  struct user_context *ctx);
static void vmm_thread_exited(struct uthread *uth);
static struct uthread *vmm_thread_create(void *(*func)(void *), void *arg);

struct schedule_ops vmm_sched_ops = {
	.sched_init = vmm_sched_init,
	.sched_entry = vmm_sched_entry,
	.thread_runnable = vmm_thread_runnable,
	.thread_paused = vmm_thread_paused,
	.thread_blockon_sysc = vmm_thread_blockon_sysc,
	.thread_has_blocked = vmm_thread_has_blocked,
	.thread_refl_fault = vmm_thread_refl_fault,
	.thread_exited = vmm_thread_exited,
	.thread_create = vmm_thread_create,
};

struct schedule_ops *sched_ops = &vmm_sched_ops;

/* Helpers */
static void vmm_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data);
static void acct_thread_blocked(struct vmm_thread *vth);
static void acct_thread_unblocked(struct vmm_thread *vth);
static void enqueue_vmm_thread(struct vmm_thread *vth);
static int task_thread_ctor(void *obj, void *priv, int flags);
static void task_thread_dtor(void *obj, void *priv);
static struct vmm_thread *alloc_vmm_thread(struct virtual_machine *vm,
                                           int type);
static void *__alloc_stack(size_t stacksize);
static void __free_stack(void *stacktop, size_t stacksize);

static bool sched_is_greedy(void)
{
	return parlib_never_yield;
}

static unsigned int sched_nr_greedy_cores(void)
{
	if (!current_vm)
		return 1;
	return current_vm->nr_gpcs + 1;
}

static void restart_thread(struct syscall *sysc)
{
	struct uthread *ut_restartee = (struct uthread*)sysc->u_data;

	/* uthread stuff here: */
	assert(ut_restartee);
	assert(ut_restartee->sysc == sysc);	/* set in uthread.c */
	ut_restartee->sysc = 0;	/* so we don't 'reblock' on this later */
	vmm_thread_runnable(ut_restartee);
}

static void vmm_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type,
                               void *data)
{
	struct syscall *sysc;

	/* I think we can make this assert now.  If not, check pthread.c. (concern
	 * was having old ev_qs firing and running this handler). */
	assert(ev_msg);
	sysc = ev_msg->ev_arg3;
	assert(sysc);
	restart_thread(sysc);
}

/* Helper: allocates a UCQ-based event queue suitable for syscalls.  Will
 * attempt to route the notifs/IPIs to vcoreid */
static struct event_queue *setup_sysc_evq(int vcoreid)
{
	struct event_queue *evq;
	uintptr_t mmap_block;

	mmap_block = (uintptr_t)mmap(0, PGSIZE * 2,
	                             PROT_WRITE | PROT_READ,
	                             MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE,
	                             -1, 0);
	evq = get_eventq_raw();
	assert(mmap_block && evq);
	evq->ev_flags = EVENT_IPI | EVENT_INDIR | EVENT_SPAM_INDIR | EVENT_WAKEUP;
	evq->ev_vcore = vcoreid;
	evq->ev_mbox->type = EV_MBOX_UCQ;
	ucq_init_raw(&evq->ev_mbox->ucq, mmap_block, mmap_block + PGSIZE);
	return evq;
}

static void vmm_sched_init(void)
{
	struct task_thread *thread0;

	/* Note that thread0 doesn't belong to a VM.  We can set this during
	 * vmm_init() if we need to. */
	thread0 = (struct task_thread*)alloc_vmm_thread(0, VMM_THREAD_TASK);
	assert(thread0);
	acct_thread_unblocked((struct vmm_thread*)thread0);
	thread0->stacksize = USTACK_NUM_PAGES * PGSIZE;
	thread0->stacktop = (void*)USTACKTOP;
	/* for lack of a better vcore, might as well send to 0 */
	sysc_evq = setup_sysc_evq(0);
	uthread_2ls_init((struct uthread*)thread0, vmm_handle_syscall, NULL);
	task_thread_cache = kmem_cache_create("task threads",
	                                      sizeof(struct vmm_thread),
	                                      __alignof__(struct vmm_thread), 0,
	                                      task_thread_ctor, task_thread_dtor,
	                                      NULL);
}

/* The scheduling policy is encapsulated in the next few functions (from here
 * down to sched_entry()). */

static int desired_nr_vcores(void)
{
	/* Sanity checks on our accounting. */
	assert(atomic_read(&nr_unblk_guests) >= 0);
	assert(atomic_read(&nr_unblk_tasks) >= 0);
	/* Lockless peak.  This is always an estimate.  Some of our tasks busy-wait,
	 * so it's not enough to just give us one vcore for all tasks, yet. */
	return atomic_read(&nr_unblk_guests) + atomic_read(&nr_unblk_tasks);
}

static struct vmm_thread *__pop_first(struct vmm_thread_tq *tq)
{
	struct vmm_thread *vth;

	vth = TAILQ_FIRST(tq);
	if (vth)
		TAILQ_REMOVE(tq, vth, tq_next);
	return vth;
}

static struct vmm_thread *pick_a_thread_degraded(void)
{
	struct vmm_thread *vth;

	spin_pdr_lock(&queue_lock);
	vth = __pop_first(&rnbl_tasks);
	if (!vth)
		vth = __pop_first(&rnbl_guests);
	spin_pdr_unlock(&queue_lock);
	return vth;
}

/* We have plenty of cores - run whatever we want.  We'll prioritize tasks. */
static struct vmm_thread *pick_a_thread_plenty(void)
{
	struct vmm_thread *vth = 0;

	spin_pdr_lock(&queue_lock);
	if (!vth)
		vth = __pop_first(&rnbl_tasks);
	if (!vth)
		vth = __pop_first(&rnbl_guests);
	spin_pdr_unlock(&queue_lock);
	return vth;
}

static void yield_current_uth(void)
{
	struct vmm_thread *vth;

	if (!current_uthread)
		return;
	vth = (struct vmm_thread*)stop_current_uthread();
	enqueue_vmm_thread(vth);
}

/* Helper, tries to get the right number of vcores.  Returns TRUE if we think we
 * have enough, FALSE otherwise.
 *
 * TODO: this doesn't handle a lot of issues, like preemption, how to
 * run/yield our vcores, dynamic changes in the number of runnables, where
 * to send events, how to avoid interfering with gpcs, etc. */
static bool try_to_get_vcores(void)
{
	int nr_vcores_wanted;
	bool have_enough;

	if (sched_is_greedy())
		return num_vcores() == sched_nr_greedy_cores();
	nr_vcores_wanted = desired_nr_vcores();
	have_enough = nr_vcores_wanted <= num_vcores();
	if (have_enough) {
		vcore_tick_disable();
		return TRUE;
	}
	vcore_tick_enable(vmm_sched_period_usec);
	vcore_request_total(nr_vcores_wanted);
	return FALSE;
}

static void stats_run_vth(struct vmm_thread *vth)
{
	vth->nr_runs++;
	if (vth->prev_vcoreid != vcore_id()) {
		vth->prev_vcoreid = vcore_id();
		vth->nr_resched++;
	}
}

/* TODO: This assumes we get all of our vcores. */
static struct vmm_thread *sched_pick_thread_greedy(void)
{
	struct vmm_thread *vth;

	if (current_uthread) {
		stats_run_vth((struct vmm_thread*)current_uthread);
		run_current_uthread();
	}
	if (vcore_id() == 0) {
		spin_pdr_lock(&queue_lock);
		vth = __pop_first(&rnbl_tasks);
		spin_pdr_unlock(&queue_lock);
		return vth;
	}
	/* This races with enqueue_vmm_thread, which can run on another core.
	 * Here are the rules:
	 * - set when runnable (race free, only one state for the thread at a time)
	 * - cleared when we run it (race free, we're the only runners)
	 * - if we take an interrupt, we'll just run_current_uthread and not check
	 * - if we vmexit, we'll run the buddy directly */
	assert(vcore_id() <= current_vm->nr_gpcs);
	vth = greedy_rnbl_guests[vcore_id() - 1];
	if (vth)
		greedy_rnbl_guests[vcore_id() - 1] = NULL;
	return vth;
}

static struct vmm_thread *sched_pick_thread_nice(void)
{
	struct vmm_thread *vth;
	bool have_enough;

	have_enough = try_to_get_vcores();
	if (!have_enough && vcore_tick_poll()) {
		/* slightly less than ideal: we grab the queue lock twice */
		yield_current_uth();
	}
	if (current_uthread) {
		stats_run_vth((struct vmm_thread*)current_uthread);
		run_current_uthread();
	}
	if (have_enough)
		vth = pick_a_thread_plenty();
	else
		vth = pick_a_thread_degraded();
	return vth;
}

static void __attribute__((noreturn)) vmm_sched_entry(void)
{
	struct vmm_thread *vth;

	if (sched_is_greedy()) {
		vth = sched_pick_thread_greedy();
		if (!vth) {
			/* sys_halt_core will return, but we need to restart the vcore.  We
			 * might have woke due to an event, and we'll need to handle_events
			 * and other things dealt with by uthreads. */
			if (vcore_id() == 0)
				sys_halt_core(0);
			/* In greedy mode, yield will abort and we'll just restart */
			vcore_yield_or_restart();
		}
	} else {
		vth = sched_pick_thread_nice();
		if (!vth)
			vcore_yield_or_restart();
	}
	stats_run_vth(vth);
	run_uthread((struct uthread*)vth);
}

static void vmm_thread_runnable(struct uthread *uth)
{
	/* A thread that was blocked is now runnable.  This counts as becoming
	 * unblocked (running + runnable) */
	acct_thread_unblocked((struct vmm_thread*)uth);
	enqueue_vmm_thread((struct vmm_thread*)uth);
}

static void vmm_thread_paused(struct uthread *uth)
{
	/* The thread stopped for some reason, usually a preemption.  We'd like to
	 * just run it whenever we get a chance.  Note that it didn't become
	 * 'blocked' - it's still runnable. */
	enqueue_vmm_thread((struct vmm_thread*)uth);
}

static void vmm_thread_blockon_sysc(struct uthread *uth, void *syscall)
{
	struct syscall *sysc = (struct syscall*)syscall;

	acct_thread_blocked((struct vmm_thread*)uth);
	sysc->u_data = uth;
	if (!register_evq(sysc, sysc_evq)) {
		/* Lost the race with the call being done.  The kernel won't send the
		 * event.  Just restart him. */
		restart_thread(sysc);
	}
	/* GIANT WARNING: do not touch the thread after this point. */
}

static void vmm_thread_has_blocked(struct uthread *uth, int flags)
{
	/* The thread blocked on something like a mutex.  It's not runnable, so we
	 * don't need to put it on a list, but we do need to account for it not
	 * running.  We'll find out (via thread_runnable) when it starts up again.
	 */
	acct_thread_blocked((struct vmm_thread*)uth);
}

static void refl_error(struct uthread *uth, unsigned int trap_nr,
                       unsigned int err, unsigned long aux)
{
	printf("Thread has unhandled fault: %d, err: %d, aux: %p\n",
	       trap_nr, err, aux);
	/* Note that uthread.c already copied out our ctx into the uth
	 * struct */
	print_user_context(&uth->u_ctx);
	printf("Turn on printx to spew unhandled, malignant trap info\n");
	exit(-1);
}

static bool handle_page_fault(struct uthread *uth, unsigned int err,
                              unsigned long aux)
{
	if (!(err & PF_VMR_BACKED))
		return FALSE;
	syscall_async(&uth->local_sysc, SYS_populate_va, aux, 1);
	__block_uthread_on_async_sysc(uth);
	return TRUE;
}

static void vmm_thread_refl_hw_fault(struct uthread *uth,
                                     unsigned int trap_nr,
                                     unsigned int err, unsigned long aux)
{
	switch (trap_nr) {
	case HW_TRAP_PAGE_FAULT:
		if (!handle_page_fault(uth, err, aux))
			refl_error(uth, trap_nr, err, aux);
		break;
	default:
		refl_error(uth, trap_nr, err, aux);
	}
}

/* Yield callback for __ctlr_entry */
static void __swap_to_gth(struct uthread *uth, void *dummy)
{
	struct ctlr_thread *cth = (struct ctlr_thread*)uth;

	/* We just immediately run our buddy.  The ctlr and the guest are accounted
	 * together ("pass the token" back and forth). */
	current_uthread = NULL;
	stats_run_vth((struct vmm_thread*)cth->buddy);
	run_uthread((struct uthread*)cth->buddy);
	assert(0);
}

/* All ctrl threads start here, each time their guest has a fault.  They can
 * block and unblock along the way.  Once a ctlr does its final uthread_yield,
 * the next time it will start again from the top. */
static void __ctlr_entry(void)
{
	struct ctlr_thread *cth = (struct ctlr_thread*)current_uthread;
	struct virtual_machine *vm = gth_to_vm(cth->buddy);

	if (!handle_vmexit(cth->buddy)) {
		struct vm_trapframe *vm_tf = gth_to_vmtf(cth->buddy);

		fprintf(stderr, "vmm: handle_vmexit returned false\n");
		fprintf(stderr, "Note: this may be a kernel module, not the kernel\n");
		fprintf(stderr, "RSP was %p, ", (void *)vm_tf->tf_rsp);
		fprintf(stderr, "RIP was %p:\n", (void *)vm_tf->tf_rip);
		/* TODO: properly walk the kernel page tables to map the tf_rip
		 * to a physical address. For now, however, this hack is good
		 * enough.
		 */
		hexdump(stderr, (void *)(vm_tf->tf_rip & 0x3fffffff), 16);
		showstatus(stderr, cth->buddy);
		exit(0);
	}
	/* We want to atomically yield and start/reenqueue our buddy.  We do so in
	 * vcore context on the other side of the yield. */
	uthread_yield(FALSE, __swap_to_gth, 0);
}

static void vmm_thread_refl_vm_fault(struct uthread *uth)
{
	struct guest_thread *gth = (struct guest_thread*)uth;
	struct ctlr_thread *cth = gth->buddy;

	gth->nr_vmexits++;
	/* The ctlr starts frm the top every time we get a new fault. */
	cth->uthread.flags |= UTHREAD_SAVED;
	init_user_ctx(&cth->uthread.u_ctx, (uintptr_t)&__ctlr_entry,
	              (uintptr_t)(cth->stacktop));
	/* We just immediately run our buddy.  The ctlr and the guest are accounted
	 * together ("pass the token" back and forth). */
	current_uthread = NULL;
	stats_run_vth((struct vmm_thread*)cth);
	run_uthread((struct uthread*)cth);
	assert(0);
}

static void vmm_thread_refl_fault(struct uthread *uth,
                                  struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		/* Guests should only ever VM exit */
		assert(((struct vmm_thread*)uth)->type != VMM_THREAD_GUEST);
		vmm_thread_refl_hw_fault(uth, __arch_refl_get_nr(ctx),
		                         __arch_refl_get_err(ctx),
		                         __arch_refl_get_aux(ctx));
		break;
	case ROS_VM_CTX:
		vmm_thread_refl_vm_fault(uth);
		break;
	default:
		assert(0);
	}
}

static void task_thread_dtor(void *obj, void *priv)
{
	struct task_thread *tth = (struct task_thread*)obj;

	uthread_cleanup((struct uthread*)tth);
	__free_stack(tth->stacktop, tth->stacksize);
}

static void task_thread_exit(struct task_thread *tth)
{
	struct uthread *uth = (struct uthread*)tth;

	if (uth->flags & UTHREAD_IS_THREAD0)
		return;
	kmem_cache_free(task_thread_cache, tth);
}

static void ctlr_thread_exit(struct ctlr_thread *cth)
{
	__vthread_exited((struct vthread*)cth->buddy);
}

static void vmm_thread_exited(struct uthread *uth)
{
	struct vmm_thread *vth = (struct vmm_thread*)uth;

	assert(vth->type != VMM_THREAD_GUEST);

	acct_thread_blocked(vth);
	switch (vth->type) {
	case VMM_THREAD_TASK:
		task_thread_exit((struct task_thread*)uth);
		break;
	case VMM_THREAD_CTLR:
		ctlr_thread_exit((struct ctlr_thread*)uth);
		break;
	case VMM_THREAD_GUEST:
		panic("Guest threads shouldn't be able to exit");
	}
}

static void destroy_guest_thread(struct guest_thread *gth)
{
	struct ctlr_thread *cth = gth->buddy;

	__free_stack(cth->stacktop, cth->stacksize);
	uthread_cleanup((struct uthread*)cth);
	free(cth);
	uthread_cleanup((struct uthread*)gth);
	free(gth);
}

struct guest_thread *create_guest_thread(struct virtual_machine *vm,
                                         unsigned int gpcoreid,
                                         struct vmm_gpcore_init *gpci)
{
	struct guest_thread *gth;
	struct ctlr_thread *cth;
	/* Guests won't use TLS; they always operate in Ring V.  The controller
	 * might - not because of anything we do, but because of glibc calls. */
	struct uth_thread_attr gth_attr = {.want_tls = FALSE};
	struct uth_thread_attr cth_attr = {.want_tls = TRUE};

	gth = (struct guest_thread*)alloc_vmm_thread(vm, VMM_THREAD_GUEST);
	cth = (struct ctlr_thread*)alloc_vmm_thread(vm, VMM_THREAD_CTLR);
	if (!gth || !cth) {
		free(gth);
		free(cth);
		return 0;
	}
	gth->buddy = cth;
	cth->buddy = gth;
	gth->gpc_id = gpcoreid;
	gth->gpci = *gpci;
	cth->stacksize = VMM_THR_STACKSIZE;
	cth->stacktop = __alloc_stack(cth->stacksize);
	if (!cth->stacktop) {
		free(gth);
		free(cth);
		return 0;
	}
	gth->uthread.u_ctx.type = ROS_VM_CTX;
	gth->uthread.u_ctx.tf.vm_tf.tf_guest_pcoreid = gpcoreid;
	uthread_init((struct uthread*)gth, &gth_attr);
	uthread_init((struct uthread*)cth, &cth_attr);
	gth->halt_mtx = uth_mutex_alloc();
	gth->halt_cv = uth_cond_var_alloc();
	return gth;
}

static void ev_handle_diag(struct event_msg *ev_msg, unsigned int ev_type,
                           void *data)
{
	struct virtual_machine *vm = current_vm;
	struct guest_thread *gth;
	struct ctlr_thread *cth;
	bool reset = FALSE;

	if (ev_msg && (ev_msg->ev_arg1 == 1))
		reset = TRUE;

	fprintf(stderr, "\nSCHED stats:\n---------------\n");
	for (int i = 0; i < vm->nr_gpcs; i++) {
		gth = gpcid_to_gth(vm, i);
		cth = gth->buddy;
		fprintf(stderr, "\tGPC %2d: %lu resched, %lu gth runs, %lu ctl runs, %lu user-handled vmexits\n",
				i,
		        ((struct vmm_thread*)gth)->nr_resched,
		        ((struct vmm_thread*)gth)->nr_runs,
		        ((struct vmm_thread*)cth)->nr_runs,
		        gth->nr_vmexits);
		if (reset) {
		    ((struct vmm_thread*)gth)->nr_resched = 0;
		    ((struct vmm_thread*)gth)->nr_runs = 0;
		    ((struct vmm_thread*)cth)->nr_runs = 0;
		    gth->nr_vmexits = 0;
		}
	}
	fprintf(stderr, "\n\tNr unblocked gpc %lu, Nr unblocked tasks %lu\n",
	        atomic_read(&nr_unblk_guests), atomic_read(&nr_unblk_tasks));
}

int vmm_init(struct virtual_machine *vm, struct vmm_gpcore_init *gpcis,
             int flags)
{
	struct guest_thread **gths;

	if (current_vm)
		return -1;
	current_vm = vm;
	/* We should tell the kernel to create all of the GPCs we'll need in
	 * advance.
	 *
	 * We could create the others on the fly, but the kernel's answer for
	 * CPUID[0x1] will not have to total number of cores.  If we move that
	 * handler to userspace, we can create the SMP-booted GPCs on the fly.
	 *
	 * We'd also have to deal with gths[] growing dynamically, which would
	 * require synchronization. */
	if (syscall(SYS_vmm_add_gpcs, vm->nr_gpcs, gpcis) != vm->nr_gpcs)
		return -1;
	if (flags) {
		if (syscall(SYS_vmm_ctl, VMM_CTL_SET_FLAGS, flags))
			return -1;
	}
	gths = malloc(vm->nr_gpcs * sizeof(struct guest_thread *));
	if (!gths)
		return -1;
	for (int i = 0; i < vm->nr_gpcs; i++) {
		gths[i] = create_guest_thread(vm, i, &gpcis[i]);
		if (!gths[i]) {
			for (int j = 0; j < i; j++)
				destroy_guest_thread(gths[j]);
			free(gths);
			return -1;
		}
	}
	wmb(); /* All gths posted before advertising. */
	vm->__gths = gths;
	uthread_mcp_init();
	register_ev_handler(EV_FREE_APPLE_PIE, ev_handle_diag, NULL);
	if (sched_is_greedy()) {
		greedy_rnbl_guests = calloc(vm->nr_gpcs, sizeof(struct vmm_thread *));
		assert(greedy_rnbl_guests);
		vcore_request_total(sched_nr_greedy_cores());
		syscall(SYS_vmm_ctl, VMM_CTL_SET_EXITS,
		        syscall(SYS_vmm_ctl, VMM_CTL_GET_EXITS) &
				~(VMM_CTL_EXIT_HALT | VMM_CTL_EXIT_MWAIT));
	}
	return 0;
}

void start_guest_thread(struct guest_thread *gth)
{
	acct_thread_unblocked((struct vmm_thread*)gth);
	enqueue_vmm_thread((struct vmm_thread*)gth);
}

static void __task_thread_run(void)
{
	struct task_thread *tth = (struct task_thread*)current_uthread;

	uth_2ls_thread_exit(tth->func(tth->arg));
}

static int task_thread_ctor(void *obj, void *priv, int flags)
{
	struct vmm_thread *vth = (struct vmm_thread*)obj;
	struct task_thread *tth = (struct task_thread*)obj;

	memset(vth, 0, sizeof(struct vmm_thread));
	vth->type = VMM_THREAD_TASK;
	vth->vm = current_vm;
	tth->stacksize = VMM_THR_STACKSIZE;
	tth->stacktop = __alloc_stack(tth->stacksize);
	if (!tth->stacktop)
		return -1;
	return 0;
}

/* Helper, creates and starts a task thread. */
static struct task_thread *__vmm_run_task(struct virtual_machine *vm,
                                          void *(*func)(void *), void *arg,
                                          struct uth_thread_attr *tth_attr)
{
	struct task_thread *tth;

	tth = kmem_cache_alloc(task_thread_cache, 0);
	tth->func = func;
	tth->arg = arg;
	init_user_ctx(&tth->uthread.u_ctx, (uintptr_t)&__task_thread_run,
	              (uintptr_t)(tth->stacktop));
	uthread_init((struct uthread*)tth, tth_attr);
	acct_thread_unblocked((struct vmm_thread*)tth);
	enqueue_vmm_thread((struct vmm_thread*)tth);
	return tth;
}

struct task_thread *vmm_run_task(struct virtual_machine *vm,
                                 void *(*func)(void *), void *arg)
{
	struct uth_thread_attr tth_attr = {.want_tls = TRUE, .detached = TRUE};

	return __vmm_run_task(vm, func, arg, &tth_attr);
}

static struct uthread *vmm_thread_create(void *(*func)(void *), void *arg)
{
	struct uth_thread_attr tth_attr = {.want_tls = TRUE, .detached = FALSE};
	struct task_thread *tth;

	/* It's OK to not have a VM for a generic thread */
	tth = __vmm_run_task(NULL, func, arg, &tth_attr);
	/* But just in case, let's poison it */
	((struct vmm_thread*)tth)->vm = (void*)0xdeadbeef;
	return (struct uthread*)tth;
}

/* Helpers for tracking nr_unblk_* threads. */
static void acct_thread_blocked(struct vmm_thread *vth)
{
	switch (vth->type) {
	case VMM_THREAD_GUEST:
	case VMM_THREAD_CTLR:
		atomic_dec(&nr_unblk_guests);
		break;
	case VMM_THREAD_TASK:
		atomic_dec(&nr_unblk_tasks);
		break;
	}
}

static void acct_thread_unblocked(struct vmm_thread *vth)
{
	switch (vth->type) {
	case VMM_THREAD_GUEST:
	case VMM_THREAD_CTLR:
		atomic_inc(&nr_unblk_guests);
		break;
	case VMM_THREAD_TASK:
		atomic_inc(&nr_unblk_tasks);
		break;
	}
}

static void greedy_mark_guest_runnable(struct vmm_thread *vth)
{
	int gpcid;

	if (vth->type == VMM_THREAD_GUEST)
		gpcid = ((struct guest_thread*)vth)->gpc_id;
	else
		gpcid = ((struct ctlr_thread*)vth)->buddy->gpc_id;
	/* racing with the reader */
	greedy_rnbl_guests[gpcid] = vth;
}

static void enqueue_vmm_thread(struct vmm_thread *vth)
{
	switch (vth->type) {
	case VMM_THREAD_GUEST:
	case VMM_THREAD_CTLR:
		if (sched_is_greedy()) {
			greedy_mark_guest_runnable(vth);
		} else {
			spin_pdr_lock(&queue_lock);
			TAILQ_INSERT_TAIL(&rnbl_guests, vth, tq_next);
			spin_pdr_unlock(&queue_lock);
		}
		break;
	case VMM_THREAD_TASK:
		spin_pdr_lock(&queue_lock);
		TAILQ_INSERT_TAIL(&rnbl_tasks, vth, tq_next);
		spin_pdr_unlock(&queue_lock);
		if (sched_is_greedy())
			vcore_wake(0, false);
		break;
	default:
		panic("Bad vmm_thread type %p\n", vth->type);
	}
	try_to_get_vcores();
}

static struct vmm_thread *alloc_vmm_thread(struct virtual_machine *vm, int type)
{
	struct vmm_thread *vth;
	int ret;

	ret = posix_memalign((void**)&vth, __alignof__(struct vmm_thread),
	                     sizeof(struct vmm_thread));
	if (ret)
		return 0;
	memset(vth, 0, sizeof(struct vmm_thread));
	vth->type = type;
	vth->vm = vm;
	return vth;
}

static void __free_stack(void *stacktop, size_t stacksize)
{
	munmap(stacktop - stacksize, stacksize);
}

static void *__alloc_stack(size_t stacksize)
{
	int force_a_page_fault;
	void *stacktop;
	void *stackbot = mmap(0, stacksize, PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (stackbot == MAP_FAILED)
		return 0;
	stacktop = stackbot + stacksize;
	/* Want the top of the stack populated, but not the rest of the stack;
	 * that'll grow on demand (up to stacksize, then will clobber memory). */
	force_a_page_fault = ACCESS_ONCE(*(int*)(stacktop - sizeof(int)));
	return stacktop;
}
