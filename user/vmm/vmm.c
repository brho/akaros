/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Helper functions for virtual machines */

#include <vmm/vmm.h>
#include <errno.h>
#include <parlib/bitmask.h>
#include <parlib/uthread.h>
#include <sys/syscall.h>

/* Sends an interrupt with @vector to guest pcore @gpcoreid.  Returns 0 on
 * success, -1 with errstr set o/w. */
int vmm_interrupt_guest(struct virtual_machine *vm, unsigned int gpcoreid,
                        unsigned int vector)
{
	struct guest_thread *gth;
	struct vmm_gpcore_init *gpci;

	if (gpcoreid >= vm->nr_gpcs) {
		werrstr("Guest pcoreid %d out of range (%d gpcs)", gpcoreid,
		        vm->nr_gpcs);
		return -1;
	}
	gth = vm->gths[gpcoreid];
	gpci = gth_to_gpci(gth);
	/* The OUTSTANDING_NOTIF bit (256) is one greater than the last valid
	 * descriptor */
	if (vector >= VMX_POSTED_OUTSTANDING_NOTIF) {
		werrstr("Interrupt vector %d too high (max %d)", vector,
		        VMX_POSTED_OUTSTANDING_NOTIF - 1);
		return -1;
	}
	/* Syncing with halting guest threads.  The Mutex protects changes to the
	 * posted irq descriptor. */
	uth_mutex_lock(gth->halt_mtx);
	SET_BITMASK_BIT_ATOMIC(gpci->posted_irq_desc, vector);
	/* Atomic op provides the mb() btw writing the vector and mucking with
	 * OUTSTANDING_NOTIF.  If the notif was already set, then a previous thread
	 * poked the guest and signaled the CV. */
	if (!GET_BITMASK_BIT(gpci->posted_irq_desc, VMX_POSTED_OUTSTANDING_NOTIF)) {
		SET_BITMASK_BIT_ATOMIC(gpci->posted_irq_desc,
		                       VMX_POSTED_OUTSTANDING_NOTIF);
		ros_syscall(SYS_vmm_poke_guest, gpcoreid, 0, 0, 0, 0, 0);
		uth_cond_var_signal(gth->halt_cv);
	}
	uth_mutex_unlock(gth->halt_mtx);
	return 0;
}
