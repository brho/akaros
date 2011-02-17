/* tests/msr_dumb_while.c
 *
 * This requests the max_vcores in the system, then just dumbly while loops.
 * If you send it an NE_ALARM, it'll print its TSC. */

#include <rstdio.h>
#include <vcore.h>
#include <arch/arch.h>
#include <ros/bcq.h>

int main(int argc, char** argv)
{

	/* don't forget to enable notifs on vcore0.  if you don't, the kernel will
	 * restart your _S with notifs disabled, which is a path to confusion. */
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;

	/* Get EV_ALARM on vcore 1, with IPI.
	 * TODO: (PIN) this ev_q needs to be pinned */
	struct event_queue *ev_q = malloc(sizeof(struct event_queue));
	ev_q->ev_mbox = &__procdata.vcore_preempt_data[1].ev_mbox;
	ev_q->ev_flags = EVENT_IPI;
	ev_q->ev_vcore = 1;
	ev_q->ev_handler = 0;
	__procdata.kernel_evts[EV_ALARM] = ev_q;

	vcore_request(max_vcores());

	/* should never make it here */
	return -1;
}

void vcore_entry(void)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;

	struct event_msg ev_msg = {0};
	bcq_dequeue(&vcpd->ev_mbox.ev_msgs, &ev_msg, NR_BCQ_EVENTS);
	if (ev_msg.ev_type == EV_ALARM)
		printf("[T]:009:E:%llu\n", read_tsc());
	while(1);
}

