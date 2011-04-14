/* tests/msr_get_singlecore.c
 *
 * Like msr_get_cores.c, but it only gets one core. */


#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <ros/procdata.h>
#include <ros/bcq.h>
#include <arch/arch.h>
#include <stdio.h>
#include <vcore.h>
#include <mcs.h>
#include <timing.h>
#include <rassert.h>
#include <uthread.h>

#ifdef __sparc_v8__
# define udelay(x) udelay((x)/2000)
#endif

mcs_barrier_t b;

void *core0_tls = 0;
uint64_t begin = 0, end = 0;
volatile bool core1_up = FALSE;

int main(int argc, char** argv)
{
	uint32_t vcoreid = vcore_id();
	int retval = 0;

	mcs_barrier_init(&b, max_vcores());

/* begin: stuff userspace needs to do before switching to multi-mode */
	if (vcore_init())
		printf("vcore_init() failed, we're fucked!\n");
	#if 0
	/* tell the kernel where and how we want to receive notifications */
	struct notif_method *nm;
	for (int i = 0; i < MAX_NR_NOTIF; i++) {
		nm = &__procdata.notif_methods[i];
		nm->flags |= NOTIF_WANTED | NOTIF_MSG | NOTIF_IPI;
		nm->vcoreid = i % 2; // vcore0 or 1, keepin' it fresh.
	}
	#endif
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

	/* get into multi mode */
	retval = vcore_request(1);
	if (retval)
		printf("Fucked!\n");

	printf("Proc %d requesting another vcore\n", getpid());
	begin = read_tsc();
	retval = vcore_request(1);
	if (retval)
		printf("Fucked!\n");
	while (!core1_up)
		cpu_relax;
	end = read_tsc();
	printf("Took %llu usec (%llu nsec) to receive 1 core (cold).\n",
	       udiff(begin, end), ndiff(begin, end));
	printf("[T]:002:%llu:%llu:1:C.\n",
	       udiff(begin, end), ndiff(begin, end));
	core1_up = FALSE;
	udelay(2000000);
	printf("Proc %d requesting the vcore again\n", getpid());
	begin = read_tsc();
	retval = vcore_request(1);
	if (retval)
		printf("Fucked!\n");
	while (!core1_up)
		cpu_relax();
	end = read_tsc();
	printf("Took %llu usec (%llu nsec) to receive 1 core (warm).\n",
	       udiff(begin, end), ndiff(begin, end));
	printf("[T]:002:%llu:%llu:1:W.\n",
	       udiff(begin, end), ndiff(begin, end));
	return 0;
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id();

/* begin: stuff userspace needs to do to handle notifications */
	struct vcore *vc = &__procinfo.vcoremap[vcoreid];
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* Lets try to restart vcore0's context.  Note this doesn't do anything to
	 * set the appropriate TLS.  On x86, this will involve changing the LDT
	 * entry for this vcore to point to the TCB of the new user-thread. */
	if (vcoreid == 0) {
		clear_notif_pending(vcoreid);
		set_tls_desc(core0_tls, 0);
		/* Load silly state (Floating point) too */
		pop_ros_tf(&vcpd->notif_tf, vcoreid);
		panic("should never see me!");
	}	
/* end: stuff userspace needs to do to handle notifications */

	/* all other vcores are down here */
	core1_up = TRUE;

	while (core1_up)
		cpu_relax();
	printf("Proc %d's vcore %d is yielding\n", getpid(), vcoreid);
	sys_yield(0);

	while(1);
}

