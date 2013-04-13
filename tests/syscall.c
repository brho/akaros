#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <event.h>
#include <vcore.h>
#include <rassert.h>
#include <ros/bcq.h>
#include <uthread.h>

/* Deprecated, don't use this in any serious way */

static void handle_syscall(struct event_msg *ev_msg, unsigned int ev_type);
struct syscall sysc = {0};
struct event_queue *ev_q;
void ghetto_vcore_entry(void);

struct schedule_ops ghetto_sched_ops = {
	.sched_entry = ghetto_vcore_entry,
};
struct schedule_ops *sched_ops = &ghetto_sched_ops;

int main(int argc, char** argv)
{
	int num_started, retval;
	unsigned int ev_type;

	/* register our syscall handler (2LS does this) */
	ev_handlers[EV_SYSCALL] = handle_syscall;

	printf("Trying to block\n");
	/* Not doing anything else to it: no EVENT_IPI yet, etc. */
	ev_q = get_big_event_q();
	/* issue the diagnostic block syscall */
	sysc.num = SYS_block;
	sysc.arg0 = 5000;	/* 5ms */
	sysc.ev_q = ev_q;
	/* Trap */
	num_started = __ros_arch_syscall((long)&sysc, 1);
	if (!(atomic_read(&sysc.flags) & SC_DONE))
		printf("Not done, looping!\n");
	/* You could poll on this.  This is really ghetto, but i got rid of
	 * event_activity, whose sole purpose was to encourage spinning. */
	while (!(atomic_read(&sysc.flags) & SC_DONE))
		cpu_relax();
	handle_event_q(ev_q);
	/* by now, we should have run our handler */
	/********************************************************/
	/* Start MCP / IPI test */
	printf("Switching to _M mode and testing an IPI-d ev_q\n");
	printf("Our indirect ev_q is %08p\n", ev_q);

/* begin: stuff userspace needs to do before switching to multi-mode */
	/* Note we don't need to set up event reception for any particular kevent.
	 * The ev_q in the syscall said to send an IPI to vcore 0 which means an
	 * EV_EVENT will be sent straight to vcore0. */
	/* Inits a thread for us, though we won't use it.  Just a hack to get into
	 * _M mode.  Note this requests one vcore for us */
	struct uthread dummy = {0};
	uthread_lib_init(&dummy);
	/* Need to save our floating point state somewhere (like in the
	 * user_thread_tcb so it can be restarted too */
	enable_notifs(0);
/* end: stuff userspace needs to do before switching to multi-mode */

	retval = vcore_request(1);
	if (retval < 0)
		printf("No cores granted, Rut Ro Raggy!\n");
	/* now we're back in thread 0 on vcore 0 */
	ev_q->ev_flags = EVENT_IPI;
	ev_q->ev_vcore = 0;
	sysc.u_data = (void*)1;	/* using this to loop on */
	/* issue the diagnostic blocking syscall */
	sysc.num = SYS_block;
	sysc.arg0 = 5000;	/* 5ms */
	sysc.ev_q = ev_q;
	num_started = __ros_arch_syscall((long)&sysc, 1);
	/* have this thread "wait" */
	if (!(atomic_read(&sysc.flags) & SC_DONE))
		printf("Not done, looping on a local variable!\n");
	while (sysc.u_data)
		cpu_relax();
	assert(atomic_read(&sysc.flags) & SC_DONE);
	printf("Syscall unblocked, IPI broke me out of the loop.\n");

	/* done */
	put_big_event_q(ev_q);
	printf("Syscall test exiting\n");
	return 0;
}

static void handle_syscall(struct event_msg *ev_msg, unsigned int ev_type)
{
	struct syscall *my_sysc;
	if (!ev_msg)
		return;
	my_sysc = ev_msg->ev_arg3;
	printf("Handling syscall event for sysc %08p (%08p)\n",
	       my_sysc, &sysc);
	/* our syscall should be done (we ought to check the msg pointer) */
	if (atomic_read(&sysc.flags) & SC_DONE) 
		printf("Syscall is done, retval: %d\n", sysc.retval);
	else
		printf("BUG! Syscall wasn't done!\n");
	/* signal to thread 0 that the sysc is done, just to show this
	 * is getting done in vcore context. */
	my_sysc->u_data = 0;
}

void ghetto_vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	static bool first_time = TRUE;

/* begin: stuff userspace needs to do to handle notifications */

	/* Restart vcore0's context. */
	if (vcoreid == 0) {
		run_current_uthread();
		panic("should never see me!");
	}	
	/* unmask notifications once you can let go of the uthread_ctx and it is
	 * okay to clobber the transition stack.
	 * Check Documentation/processes.txt: 4.2.4.  In real code, you should be
	 * popping the tf of whatever user process you want (get off the x-stack) */
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	vcpd->notif_disabled = FALSE;
	
/* end: stuff userspace needs to do to handle notifications */
	/* if you have other vcores, they'll just chill here */
	while(1);
}
