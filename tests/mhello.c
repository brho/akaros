#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/event.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <rstdio.h>
#include <vcore.h>
#include <mcs.h>
#include <timing.h>
#include <rassert.h>
#include <event.h>

#ifdef __sparc_v8__
# define udelay(x) udelay((x)/2000)
#endif

mcs_barrier_t b;

__thread int temp;
void *core0_tls = 0;

struct event_queue *indirect_q;

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;

	mcs_barrier_init(&b, max_vcores());

	/* prep indirect ev_q.  Note we grab a big one */
	indirect_q = get_big_event_q();
	indirect_q->ev_flags = EVENT_IPI;
	indirect_q->ev_vcore = 1;			/* IPI core 1 */
	indirect_q->ev_handler = 0;
	printf("Registering %08p for event type %d\n", indirect_q,
	       EV_FREE_APPLE_PIE);
	register_kevent_q(indirect_q, EV_FREE_APPLE_PIE);

/* begin: stuff userspace needs to do before switching to multi-mode */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");

	/* Set up event reception.  For example, this will allow us to receive an
	 * event and IPI for USER_IPIs on vcore 0.  Check event.c for more stuff. */
	enable_kevent(EV_USER_IPI, 0, TRUE);

	/* Need to save this somewhere that you can find it again when restarting
	 * core0 */
	core0_tls = get_tls_desc(0);
	/* Need to save our floating point state somewhere (like in the
	 * user_thread_tcb so it can be restarted too */

	/* don't forget to enable notifs on vcore0 at some point */
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do before switching to multi-mode */

	if ((vcoreid = vcore_id())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		temp = 0xdeadbeef;
		printf("Hello from vcore %d with temp addr = %p and temp = %p\n",
		       vcoreid, &temp, temp);
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		//retval = sys_resource_req(RES_CORES, 2, 0);
		printf("Requesting %d vcores\n",max_vcores());
		retval = vcore_request(max_vcores());
		//retval = vcore_request(5);
		printf("This is vcore0, right after vcore_request, retval=%d\n", retval);
	}

	/* test notifying my vcore2 */
	udelay(5000000);
	printf("Vcore 0 self-notifying vcore 2 with notif 4!\n");
	struct event_msg msg;
	msg.ev_type = 4;
	sys_self_notify(2, 4, &msg);
	udelay(5000000);
	printf("Vcore 0 notifying itself with notif 3!\n");
	msg.ev_type = 3;
	sys_notify(sys_getpid(), 3, &msg);
	udelay(1000000);

	/* test loop for restarting a notif_tf */
	if (vcoreid == 0) {
		int ctr = 0;
		while(1) {
			printf("Vcore %d Spinning (%d), temp = %08x!\n", vcoreid, ctr++, temp);
			udelay(5000000);
			//exit(0);
		}
	}

	printf("Vcore %d Done!\n", vcoreid);
	//mcs_barrier_wait(&b,vcore_id());

	printf("All Cores Done!\n", vcoreid);
	while(1); // manually kill from the monitor
	return 0;
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();
	static bool first_time = TRUE;

	temp = 0xcafebabe;
/* begin: stuff userspace needs to do to handle notifications */

	struct vcore *vc = &__procinfo.vcoremap[vcoreid];
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* here is how you receive an event */
	struct event_msg ev_msg = {0};
	if (event_activity(&vcpd->ev_mbox, 0)) {
		/* Ought to while loop/dequeue, processing as they come in. */
		bcq_dequeue(&vcpd->ev_mbox.ev_msgs, &ev_msg, NR_BCQ_EVENTS);
		printf("the queue is on vcore %d and has a ne with type %d\n", vcoreid,
		       ev_msg.ev_type);
		printf("Number of event overflows: %d\n", vcpd->ev_mbox.ev_overflows);
	}
	/* it might be in bitmask form too: */
	//printf("and the bitmask looks like: ");
	//PRINT_BITMASK(__procdata.vcore_preempt_data[vcoreid].notif_bmask, MAX_NR_NOTIF);

	/* How we handle indirection events: */
	struct event_queue_big *ev_q;
	struct event_msg indir_msg = {0};
	if (ev_msg.ev_type == EV_EVENT) {
		ev_q = ev_msg.ev_arg3;	/* convention */
		printf("Detected EV_EVENT, ev_q is %08p (%08p)\n", ev_q, indirect_q);
		/* Ought to loop/dequeue, processing as they come in. */
		bcq_dequeue(&ev_q->ev_mbox->ev_msgs, &indir_msg, NR_BCQ_EVENTS);
		printf("Message of type: %d (%d)\n", indir_msg.ev_type,
		       EV_FREE_APPLE_PIE);
	}
	/* how we tell a preemption is pending (regardless of notif/events) */
	if (vc->preempt_pending) {
		printf("Oh crap, vcore %d is being preempted!  Yielding\n", vcoreid);
		sys_yield(TRUE);
		printf("After yield on vcore %d. I wasn't being preempted.\n", vcoreid);
	}
		
	/* Lets try to restart vcore0's context.  Note this doesn't do anything to
	 * set the appropriate TLS.  On x86, this will involve changing the LDT
	 * entry for this vcore to point to the TCB of the new user-thread. */
	if (vcoreid == 0) {
		/* // test for preempting a notif_handler.  do it from the monitor
		int ctr = 0;
		while(ctr < 3) {
			printf("Vcore %d Spinning (%d), temp = %08x!\n", vcoreid, ctr++, temp);
			udelay(5000000);
		} */
		printf("restarting vcore0 from userspace\n");
		vcpd->notif_pending = 0;
		/* Do one last check for notifs after clearing pending */
		/* // testing for missing a notif
		if (first_time) {
			first_time = FALSE;
			printf("setting pending, trying to renotify etc\n");
			vcpd->notif_pending = 1;
		} */
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

	printf("Hello from vcore_entry in vcore %d with temp addr %p and temp %p\n",
	       vcoreid, &temp, temp);
	vcore_request(1);
	//mcs_barrier_wait(&b,vcore_id());
	udelay(vcoreid * 10000000);
	//exit(0);
	while(1);
}
