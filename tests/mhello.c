#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/notification.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <rstdio.h>
#include <vcore.h>
#include <mcs.h>
#include <timing.h>
#include <rassert.h>

mcs_barrier_t b;

__thread int temp;
void *core0_tls = 0;

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;

	mcs_barrier_init(&b, max_vcores() - 1);

/* begin: stuff userspace needs to do before switching to multi-mode */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");

	/* tell the kernel where and how we want to receive notifications */
	struct notif_method *nm;
	for (int i = 0; i < MAX_NR_NOTIF; i++) {
		nm = &__procdata.notif_methods[i];
		nm->flags |= NOTIF_WANTED | NOTIF_MSG | NOTIF_IPI;
		nm->vcoreid = i % 2; // vcore0 or 1, keepin' it fresh.
	}

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
		//retval = vcore_request(vcore_max_vcores()-2);
		retval = vcore_request(3);
		printf("This is vcore0, right after vcore_request, retval=%d\n", retval);
	}

#if 0
	/* test notifying my vcore2 */
	udelay(5000000);
	printf("Vcore 0 self-notifying vcore 2 with notif 4!\n");
	struct notif_event ne;
	ne.ne_type = 4;
	sys_self_notify(2, 4, &ne);
	udelay(5000000);
	printf("Vcore 0 notifying itself with notif 3!\n");
	ne.ne_type = 3;
	sys_notify(sys_getpid(), 3, &ne);
	udelay(1000000);
#endif

	/* test loop for restarting a notif_tf */
	if (vcoreid == 0) {
		int ctr = 0;
		while(1) {
			printf("Vcore %d Spinning (%d), temp = %08x!\n", vcoreid, ctr++, temp);
			udelay(5000000);
		}
	}

	printf("Vcore %d Done!\n", vcoreid);
	mcs_barrier_wait(&b,vcore_id());

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

	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* here is how you receive a notif_event */
	struct notif_event ne = {0};
	bcq_dequeue(&vcpd->notif_evts, &ne, NR_PERCORE_EVENTS);
	printf("the queue is on vcore %d and has a ne with type %d\n", vcoreid,
	       ne.ne_type);
	/* it might be in bitmask form too: */
	//printf("and the bitmask looks like: ");
	//PRINT_BITMASK(__procdata.vcore_preempt_data[vcoreid].notif_bmask, MAX_NR_NOTIF);
	/* can see how many messages had to be sent as bits */
	printf("Number of event overflows: %d\n", vcpd->event_overflows);

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
		/* Do one last check for notifs before clearing pending */
		vcpd->notif_pending = 0;
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
	mcs_barrier_wait(&b,vcore_id());
	while(1);
}
