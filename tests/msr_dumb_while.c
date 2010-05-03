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

	vcore_request(max_vcores());

	/* should never make it here */
	return -1;
}

void vcore_entry(void)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_enabled = TRUE;

	struct notif_method *nm = &__procdata.notif_methods[NE_ALARM];
	nm->flags = NOTIF_WANTED | NOTIF_MSG | NOTIF_IPI;
	nm->vcoreid = 1;

	struct notif_event ne = {0};
	bcq_dequeue(&vcpd->notif_evts, &ne, NR_PERCORE_EVENTS);
	if (ne.ne_type == NE_ALARM)
		printf("[T]:009:E:%llu\n", read_tsc());
	while(1);
}

