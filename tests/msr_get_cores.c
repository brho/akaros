/* tests/msr_get_cores.c
 *
 * This measures the time it takes to request and receive the max_vcores() in
 * the system.  The clock starts before vcore_request(), which includes the time
 * it takes to allocate transition stacks and TLS.  The clock stops after
 * barriering in vcore_entry().  Alternatively, you can make vcore0 pop back out
 * and measure there (comment some things out in vcore entry()). */

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

void *core0_tls = 0;
uint64_t begin = 0, end = 0;

int main(int argc, char** argv)
{
	uint32_t vcoreid = vcore_id();

	mcs_barrier_init(&b, max_vcores());

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

	/* should do this in the vcore entry */
	begin = read_tsc();
	vcore_request(max_vcores());
	mcs_barrier_wait(&b, vcoreid);
	end = read_tsc();

	printf("Took %llu usec (%llu nsec) to receive %d cores (restarting).\n",
	       udiff(begin, end), ndiff(begin, end), max_vcores());

	return 0;
}

void vcore_entry(void)
{
	uint32_t vcoreid = vcore_id(); // this will still be slow

	/* try testing immediately.  remove from here to the while(1) if you want to
	 * count vcore0 restarting. */
	mcs_barrier_wait(&b, vcoreid);
	if (vcoreid == 0) {
		end = read_tsc();
		printf("Took %llu usec (%llu nsec) to receive %d cores (no restart).\n",
		       udiff(begin, end), ndiff(begin, end), max_vcores());
		exit(0);
	}
	while(1);

/* begin: stuff userspace needs to do to handle notifications */

	struct vcore *vc = &__procinfo.vcoremap[vcoreid];
	struct preempt_data *vcpd;
	vcpd = &__procdata.vcore_preempt_data[vcoreid];
	
	/* Lets try to restart vcore0's context.  Note this doesn't do anything to
	 * set the appropriate TLS.  On x86, this will involve changing the LDT
	 * entry for this vcore to point to the TCB of the new user-thread. */
	if (vcoreid == 0) {
		vcpd->notif_pending = 0;
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
	mcs_barrier_wait(&b, vcoreid);
	while(1);

}

