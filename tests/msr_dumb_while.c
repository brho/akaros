/* tests/msr_dumb_while.c
 *
 * This requests the max_vcores in the system, then just dumbly while loops. */

#include <rstdio.h>
#include <vcore.h>

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
	while(1);
}

