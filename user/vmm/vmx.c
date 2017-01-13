#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/coreboot_tables.h>
#include <ros/common.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <ros/arch/vmx.h>
#include <vmm/sched.h>
#include <ros/arch/mmu.h>
#include <ros/arch/trapframe.h>

char *vmxexit[] = {
	VMX_EXIT_REASONS
};

void showstatus(FILE *f, struct guest_thread *vm_thread)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	int shutdown = vm_tf->tf_exit_reason;
	char *when = shutdown & VMX_EXIT_REASONS_FAILED_VMENTRY ? "entry" : "exit";
	shutdown &= ~VMX_EXIT_REASONS_FAILED_VMENTRY;
	char *reason = "UNKNOWN";
	if (shutdown < COUNT_OF(vmxexit) && vmxexit[shutdown])
		reason = vmxexit[shutdown];
	fprintf(f, "Shutdown: core %d, %s due to %s(0x%x); ret code 0x%x\n",
	        vm_tf->tf_guest_pcoreid, when, reason, shutdown,
			vm_tf->tf_exit_reason);
	fprintf_vm_tf(f, vm_tf);
}

/* Convert a kernel guest virtual address to physical address.
 * Assumes that the guest VA is in the high negative address space.
 * TODO: Takes the vm_thread argument so that we can walk the page tables
 * instead of just coercing the pointer. Therefore, this is not in vmm.h
 * since it may get complex. */
int gvatogpa(struct guest_thread *vm_thread, uint64_t va, uint64_t *pa)
{
	assert(vm_thread != NULL);
	struct vm_trapframe *vm_tf = gth_to_vmtf(vm_thread);
	uint64_t *ptptr = (uint64_t *)vm_tf->tf_cr3;
	uint64_t entry;

	for (int shift = PML4_SHIFT; shift >= PML1_SHIFT; shift -= BITS_PER_PML) {
		entry = ptptr[PMLx(va, shift)];

		if (!PAGE_PRESENT(entry))
			return -1;
		if ((entry & PTE_PS) != 0) {
			uint64_t bitmask = ((1 << shift) - 1);

			*pa = (((uint64_t)va & bitmask) | (entry & ~bitmask));
			return 0;
		}
		ptptr = (uint64_t *)PG_ADDR(entry);
	}
	*pa = ((uint64_t)va & 0xfff) | (uint64_t)ptptr;
	return 0;
}

/* Get the RIP as a physical address. */
int rippa(struct guest_thread *vm_thread, uint64_t *pa)
{
	assert(vm_thread != NULL);
	return gvatogpa(vm_thread, gth_to_vmtf(vm_thread)->tf_rip, pa);
}
