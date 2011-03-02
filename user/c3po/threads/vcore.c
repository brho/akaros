#include <vcore.h>
#include <mcs.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <parlib.h>
#include <ros/event.h>
#include <arch/atomic.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <event.h>
#include <threadlib_internal.h>
#include <threadlib.h>

// Comment out, to enable debugging in this file
#ifndef DEBUG_threadlib_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif

// Thread local pointer to the user thread currently running on a given core. 
// This variable is only meaningful to the context running
// the scheduler code (i.e. vcore context in ROS, user thread or scheduler
// context on linux depending on whether we use a scheduler thread or not)
__thread thread_t* current_thread=NULL;

/**
 * Bootup constructors.  Make sure things are initialized in the proper order.
 * Ideally we would be able to use the contsructor priorities instead of having
 * to call them all back to back like this, but for some reason I couldn't get
 * them to work properly. I.e. the priorities weren't being honored.
 **/
extern void read_config();
extern void main_thread_init();
extern void vcore_startup();
void __attribute__ ((constructor)) ctors()
{
	
//	init_cycle_clock();
	read_config();
	main_thread_init();
	vcore_startup();
}

	
	//uint32_t vc = vcore_id();
	//uint32_t kvc = ros_syscall(SYS_getvcoreid, 0, 0, 0, 0, 0, 0);
	//set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	///* Verify that the currently running thread is the one that the vcore thought
	// * was running */
	//if(current_thread != t)
	//	printf("variable:\tthread:vcore\n"
	//	       "current_thread:\t%p:%p\n"
	//	       "vcore_id:\t%p:%p\n"
	//	       "SYS_getvcoreid:\t%p:%p\n",
	//	       t, current_thread, vc, vcore_id(), kvc, ros_syscall(SYS_getvcoreid, 0, 0, 0, 0, 0, 0)
	//	      );
	//assert(current_thread == t);

/**
 * Initialize the vcores, including jumping into muticore mode.
 **/
void vcore_startup()
{
	/* Initilize the bootstrap code for using the vcores */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");
	assert(vcore_id() == 0);

	/* Tell the kernel where and how we want to receive events.  This is just an
	 * example of what to do to have a notification turned on.  We're turning on
	 * USER_IPIs, posting events to vcore 0's vcpd, and telling the kernel to
	 * send to vcore 0.  Note sys_self_notify will ignore the vcoreid pref.
	 * Also note that enable_kevent() is just an example, and you probably want
	 * to use parts of event.c to do what you want. */
	enable_kevent(EV_USER_IPI, 0, EVENT_IPI);

	/* Don't forget to enable notifs on vcore0.  if you don't, the kernel will
	 * restart your _S with notifs disabled, which is a path to confusion. */
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;

	/* Grab a reference to the main_thread on the current stack (i.e.
	 * current_thread, since we know this has been set up for us properly by
	 * the fact that the constructor calls main_thread_init() before this
	 * function.  We will need this reference below. */
	thread_t *t = current_thread;

    /* Change temporarily to vcore0s tls region so we can save the main_thread
	 * into its thread local current_thread variable.  One minor issue is that
	 * vcore0's transition-TLS isn't TLS_INITed yet.  Until it is (right before
	 * vcore_entry(), don't try and take the address of any of its TLS vars. */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[0], 0);
	current_thread = t;
	set_tls_desc(t->context->tls_desc, 0);

	/* Jump into multi-core mode! */
	/* The next line of code that will run is inside vcore_entry().  When this
	 * thread is resumed, it will continue directly after this call to
	 * vcore_request() */
	vcore_request(1);
}

/**
 * Switch into vcore mode to run the scheduler code. 
 **/
void switch_to_vcore() {

	uint32_t vcoreid = vcore_id();

	/* Disable notifications.  Once we do this, we might miss a notif_pending,
	 * so we need to enter vcore entry later.  Need to disable notifs so we
	 * don't get in weird loops */
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	vcpd->notif_enabled = FALSE;

	/* Grab a reference to the currently running thread on this vcore */
	thread_t *t = current_thread; 

	/* Switch to the vcore's tls region */
	extern void** vcore_thread_control_blocks;
	set_tls_desc(vcore_thread_control_blocks[vcoreid], vcoreid);
	
	/* Verify that the thread the vcore thinks was running is the same as the thread
	 * that was actually running */
	assert(current_thread == t);

	/* Set the stack pointer to the stack of the vcore. 
	 * We know this function is always inlined because of the attribute we set
	 * on it, so there will be no stack unwinding when this function "returns".
	 * After this call, make sure you don't use local variables. */
	set_stack_pointer((void*)vcpd->transition_stack);
	assert(in_vcore_context());

	/* Leave the current vcore completely */
	current_thread = NULL; 
	
	/* Restart the vcore and run the scheduler code */
	vcore_entry();
	assert(0);
}

/**
 * Entry point for the vcore.  Basic job is to either resume the thread that
 * was interrupted in the case of a notification coming in, or to find a new
 * thread from the user level threading library and launch it.
 **/
void __attribute__((noreturn)) vcore_entry()
{
	/* Grab references to the current vcoreid vcore preemption data, and the
     * vcoremap */
	assert(in_vcore_context());
	uint32_t vcoreid = vcore_id();
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[vcoreid];
	struct vcore *vc = &__procinfo.vcoremap[vcoreid];

	tdebug("current=%s, vcore=%d\n",
	        current_thread?current_thread->name : "NULL", vcoreid);

	/* Assert that notifications are disabled. Should always have notifications
	 * disabled when coming in here. */
	assert(vcpd->notif_enabled == FALSE);

	/* Put this in the loop that deals with notifications.  It will return if
	 * there is no preempt pending. */ 
	if (vc->preempt_pending)
		sys_yield(TRUE);

	/* When running vcore_entry(), we are using the TLS of the vcore, not any
	 * particular thread.  If current_thread is set in the vcore's TLS, then 
	 * that means the thread did not yield voluntarily, and was, instead, 
	 * interrupted by a notification.  We therefore need to restore the thread
	 * context from the notification trapframe, not the one stored in the 
	 * thread struct itself. */
    if (unlikely(current_thread)) {
        vcpd->notif_pending = 0;
        /* Do one last check for notifs after clearing pending */
        // TODO: call the handle_notif() here (first)

		/* Copy the notification trapframe into the current 
		 * threads trapframe */
		memcpy(&current_thread->context->utf, &vcpd->notif_tf, 
		       sizeof(struct user_trapframe));

        /* Restore the context from the current_thread's trapframe */
        restore_context(current_thread->context);
        assert(0);
    }

	/* Otherwise either a vcore is coming up for the first time, or a thread
	 * has just yielded and vcore_entry() was called directly. In this case we 
	 * need to figure out which thread to schedule next on the vcore */
	run_next_thread();
	assert(0);
}

