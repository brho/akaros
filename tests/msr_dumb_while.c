/* tests/msr_dumb_while.c
 *
 * This requests the max_vcores in the system, then just dumbly while loops.
 * If you send it an NE_ALARM, it'll print its TSC. */

#include <stdio.h>
#include <vcore.h>
#include <arch/arch.h>
#include <event.h>
#include <uthread.h>

int main(int argc, char** argv)
{
	/* Get EV_ALARM on vcore 1, with IPI. */
	enable_kevent(EV_ALARM, 1, EVENT_IPI | EVENT_VCORE_PRIVATE);

	vcore_request(max_vcores());

	/* should never make it here */
	return -1;
}

void vcore_entry(void)
{
	struct preempt_data *vcpd = &__procdata.vcore_preempt_data[0];
	vcpd->notif_disabled = FALSE;

	unsigned int ev_type = get_event_type(&vcpd->ev_mbox_private);
	if (ev_type == EV_ALARM)
		printf("[T]:009:E:%llu\n", read_tsc());
	while(1);
}

