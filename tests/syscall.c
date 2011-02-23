#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <event.h>
#include <vcore.h>
#include <rassert.h>
#include <ros/bcq.h>

struct syscall sysc = {0};
struct event_queue *ev_q;
void *core0_tls = 0;

int main(int argc, char** argv)
{
	int num_started, retval;
	unsigned int ev_type;
	printf("Trying to block\n");
	/* Not doing anything else to it: no EVENT_IPI yet, etc. */
	ev_q = get_big_event_q();
	/* issue the diagnostic block syscall */
	sysc.num = SYS_block;
	sysc.ev_q = ev_q;
	/* Trap */
	num_started = __ros_arch_syscall((long)&sysc, 1);
	if (!(sysc.flags & SC_DONE))
		printf("Not done, looping!\n");
	#if 0
	/* You could poll on this */
	while (!(sysc.flags & SC_DONE))
		cpu_relax();
	#endif
	/* But let's check on events... */
	while (!event_activity(ev_q->ev_mbox, ev_q->ev_flags))
		cpu_relax();
	ev_type = get_event_type(ev_q->ev_mbox);
	if (ev_type = EV_SYSCALL) {
		/* our syscall should be done (we ought to check the msg pointer) */
		if (sysc.flags & SC_DONE) 
			printf("Syscall is done, retval: %d\n", sysc.retval);
		else
			printf("BUG! Syscall wasn't done!\n");
	} else {
		printf("Whoa, got an unexpected event type %d!\n", ev_type);
	}

	/* Start MCP / IPI test */
	printf("Switching to _M mode and testing an IPI-d ev_q\n");
/* begin: stuff userspace needs to do before switching to multi-mode */
	/* Need to save this somewhere that you can find it again when restarting
	 * core0 */
	core0_tls = get_tls_desc(0);
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
	/* issue the diagnostic block syscall */
	sysc.num = SYS_block;
	sysc.ev_q = ev_q;
	num_started = __ros_arch_syscall((long)&sysc, 1);
	/* have this thread "wait" */
	if (!(sysc.flags & SC_DONE))
		printf("Not done, looping on a local variable!\n");
	while (sysc.u_data)
		cpu_relax();
	assert((sysc.flags & SC_DONE));
	printf("Syscall unblocked, IPI broke me out of the loop.\n");

	/* done */
	put_big_event_q(ev_q);
	printf("Syscall test exiting\n");
	return 0;
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	static bool first_time = TRUE;

/* begin: stuff userspace needs to do to handle notifications */

	struct vcore *vc = &__procinfo.vcoremap[vcoreid];
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* here is how you receive an event */
	struct event_msg ev_msg = {0};
	struct event_queue_big *indir_q;
	struct syscall *my_sysc;
	if (event_activity(&vcpd->ev_mbox, 0)) {
		/* Ought to while loop/dequeue, processing as they come in. */
		bcq_dequeue(&vcpd->ev_mbox.ev_msgs, &ev_msg, NR_BCQ_EVENTS);
		if (vcpd->ev_mbox.ev_overflows)
			printf("Had an overflow...\n");
		/* should do generic handling.  this is customized for the syscalls */
		if (ev_msg.ev_type == EV_EVENT) {
			indir_q = ev_msg.ev_arg3;	/* convention */
			printf("Detected EV_EVENT, ev_q is %08p (%08p)\n", indir_q, ev_q);
			assert(indir_q);
			/* Ought to loop/dequeue, processing as they come in. */
			bcq_dequeue(&indir_q->ev_mbox->ev_msgs, &ev_msg, NR_BCQ_EVENTS);
			/* should have received a syscall off the indirect ev_q */
			if (ev_msg.ev_type == EV_SYSCALL) {
				my_sysc = ev_msg.ev_arg3;
				printf("Handling syscall event for sysc %08p (%08p)\n",
				       my_sysc, &sysc);
				/* signal to thread 0 that the sysc is done, just to show this
				 * is getting done in vcore context. */
				my_sysc->u_data = 0;
			} else {
				printf("Got a different event, type %d\n", ev_msg.ev_type);
			}
		}
	}

	/* how we tell a preemption is pending (regardless of notif/events) */
	if (vc->preempt_pending) {
		printf("Oh crap, vcore %d is being preempted!  Yielding\n", vcoreid);
		sys_yield(TRUE);
		printf("After yield on vcore %d. I wasn't being preempted.\n", vcoreid);
	}
		
	/* Restart vcore0's context. */
	if (vcoreid == 0) {
		vcpd->notif_pending = 0;
		/* TODO: Do one last check for notifs after clearing pending */
		set_tls_desc(core0_tls, 0);
		/* Load silly state (Floating point) too */
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
		panic("should never see me!");
	}	
	/* unmask notifications once you can let go of the notif_tf and it is okay
	 * to clobber the transition stack.
	 * Check Documentation/processes.txt: 4.2.4.  In real code, you should be
	 * popping the tf of whatever user process you want (get off the x-stack) */
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do to handle notifications */
	/* if you have other vcores, they'll just chill here */
	while(1);
}
