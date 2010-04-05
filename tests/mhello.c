#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/notification.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <stdio.h>
#include <hart.h>

hart_barrier_t b;

__thread int temp;

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	int retval;

	hart_barrier_init(&b,hart_max_harts()-1);

/* begin: stuff userspace needs to do before switching to multi-mode */
	if (hart_init())
		printf("Harts failed, we're fucked!\n");

	/* tell the kernel where and how we want to receive notifications */
	struct notif_method *nm;
	for (int i = 1; i < MAX_NR_NOTIF; i++) {
		nm = &__procdata.notif_methods[i];
		nm->flags |= NOTIF_WANTED | NOTIF_MSG | NOTIF_IPI;
		nm->vcoreid = i % 2; // vcore0 or 1, keepin' it fresh.
	}

	/* don't forget to enable notifs on vcore0 at some point */
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do before switching to multi-mode */

	if ((vcoreid = hart_self())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		temp = 0xdeadbeef;
		printf("Hello from vcore %d with temp addr = %p and temp = %p\n",
		       vcoreid, &temp, temp);
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		//retval = sys_resource_req(RES_CORES, 2, 0);
		//retval = hart_request(hart_max_harts()-2);
		retval = hart_request(2); // doesn't do what you think.  this gives 3.
		//debug("retval = %d\n", retval);
	}
	printf("Vcore %d Done!\n", vcoreid);

	hart_barrier_wait(&b,hart_self());

	printf("All Cores Done!\n", vcoreid);
	while(1); // manually kill from the monitor
	return 0;
}

void hart_entry(void)
{
	uint32_t vcoreid = hart_self();

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

	/* unmask notifications once you can let go of the notif_tf and it is okay
	 * to clobber the transition stack.
	 * Check Documentation/processes.txt: 4.2.4 */
	vcpd->notif_enabled = TRUE;
	
/* end: stuff userspace needs to do to handle notifications */

	temp = 0xcafebabe;
	printf("Hello from hart_entry in vcore %d with temp addr %p and temp %p\n",
	       vcoreid, &temp, temp);
	hart_barrier_wait(&b,hart_self());
	while(1);
}
