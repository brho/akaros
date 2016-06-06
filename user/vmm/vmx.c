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
	fprintf(f, "  gva %p gpa %p cr3 %p\n", (void *)vm_tf->tf_guest_va,
	        (void *)vm_tf->tf_guest_pa, (void *)vm_tf->tf_cr3);

	fprintf(f, "  rax  0x%016lx\n",           vm_tf->tf_rax);
	fprintf(f, "  rbx  0x%016lx\n",           vm_tf->tf_rbx);
	fprintf(f, "  rcx  0x%016lx\n",           vm_tf->tf_rcx);
	fprintf(f, "  rdx  0x%016lx\n",           vm_tf->tf_rdx);
	fprintf(f, "  rbp  0x%016lx\n",           vm_tf->tf_rbp);
	fprintf(f, "  rsi  0x%016lx\n",           vm_tf->tf_rsi);
	fprintf(f, "  rdi  0x%016lx\n",           vm_tf->tf_rdi);
	fprintf(f, "  r8   0x%016lx\n",           vm_tf->tf_r8);
	fprintf(f, "  r9   0x%016lx\n",           vm_tf->tf_r9);
	fprintf(f, "  r10  0x%016lx\n",           vm_tf->tf_r10);
	fprintf(f, "  r11  0x%016lx\n",           vm_tf->tf_r11);
	fprintf(f, "  r12  0x%016lx\n",           vm_tf->tf_r12);
	fprintf(f, "  r13  0x%016lx\n",           vm_tf->tf_r13);
	fprintf(f, "  r14  0x%016lx\n",           vm_tf->tf_r14);
	fprintf(f, "  r15  0x%016lx\n",           vm_tf->tf_r15);
}

/* Convert a kernel guest virtual address to physical address.
 * Assumes that the guest VA is in the high negative address space.
 * TODO: Takes the vm_thread argument so that we can walk the page tables
 * instead of just coercing the pointer. Therefore, this is not in vmm.h
 * since it may get complex. */
uint64_t gvatogpa(struct guest_thread *vm_thread, uint64_t va)
{
	assert(vm_thread != NULL);
	assert(va >= 0xffffffffc0000000ULL);
	return va & 0x3fffffff;
}

/* Get the RIP as a physical address. */
uint64_t rippa(struct guest_thread *vm_thread)
{
	assert(vm_thread != NULL);
	return gvatogpa(vm_thread, gth_to_vmtf(vm_thread)->tf_rip);
}
