/* Copyright (c) 2015-2016 Google Inc.
 * See LICENSE for details. */

#include <parlib/common.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>
#include <vmm/vmm.h>
#include <parlib/arch/trap.h>
#include <stdio.h>

/* TODO: need infrastructure to handle GPC wakeup properly */
static bool consdata = FALSE;

static bool handle_ept_fault(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	struct virtual_machine *vm = gth_to_vm(gth);
	uint64_t gpa, *regp;
	uint8_t regx;
	int store, size;
	int advance;

	if (decode(gth, &gpa, &regx, &regp, &store, &size, &advance))
		return FALSE;
	assert(size >= 0);
	/* TODO use helpers for some of these addr checks.  the fee/fec ones might
	 * be wrong too. */
	for (int i = 0; i < VIRTIO_MMIO_MAX_NUM_DEV; i++) {
		if (vm->virtio_mmio_devices[i] == NULL)
			continue;
		if (PG_ADDR(gpa) != vm->virtio_mmio_devices[i]->addr)
			continue;
		/* TODO: can the guest cause us to spawn off infinite threads? */
		if (store)
			virtio_mmio_wr(vm, vm->virtio_mmio_devices[i], gpa, size,
			               (uint32_t *)regp);
		else
			*regp = virtio_mmio_rd(vm, vm->virtio_mmio_devices[i], gpa, size);
		vm_tf->tf_rip += advance;
		return TRUE;
	}
	if (PG_ADDR(gpa) == 0xfec00000) {
		do_ioapic(gth, gpa, regx, regp, store);
	} else if (PG_ADDR(gpa) == 0) {
		memmove(regp, &vm->low4k[gpa], size);
	} else {
		fprintf(stderr, "EPT violation: can't handle %p\n", gpa);
		fprintf(stderr, "RIP %p, exit reason 0x%x\n", vm_tf->tf_rip,
				vm_tf->tf_exit_reason);
		fprintf(stderr, "Returning 0xffffffff\n");
		showstatus(stderr, gth);
		/* Just fill the whole register for now. */
		*regp = (uint64_t) -1;
		return FALSE;
	}
	vm_tf->tf_rip += advance;
	return TRUE;
}

static bool handle_vmcall(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	uint8_t byte;

	byte = vm_tf->tf_rdi;
	printf("%c", byte);
	if (byte == '\n')
		printf("%c", '%');
	vm_tf->tf_rip += 3;
	return TRUE;
}

static bool handle_io(struct guest_thread *gth)
{
	io(gth);
	return TRUE;
}

static bool handle_msr(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	/* TODO: consider pushing the gth into msrio */
	if (msrio(gth, gth_to_gpci(gth), vm_tf->tf_exit_reason)) {
		/* Use event injection through vmctl to send a general protection fault
		 * vmctl.interrupt gets written to the VM-Entry Interruption-Information
		 * Field by vmx */
		vm_tf->tf_trap_inject = VM_TRAP_VALID
		                      | VM_TRAP_ERROR_CODE
		                      | VM_TRAP_HARDWARE
		                      | HW_TRAP_GP_FAULT;
	} else {
		vm_tf->tf_rip += 2;
	}
	return TRUE;
}

static bool handle_apic_access(struct guest_thread *gth)
{
	uint64_t gpa, *regp;
	uint8_t regx;
	int store, size;
	int advance;
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	if (decode(gth, &gpa, &regx, &regp, &store, &size, &advance))
		return FALSE;
	if (__apic_access(gth, gpa, regx, regp, store))
		return FALSE;
	vm_tf->tf_rip += advance;
	return TRUE;
}

static bool handle_halt(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	while (!consdata)
		;
	vm_tf->tf_rip += 1;
	return TRUE;
}

static bool handle_mwait(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	while (!consdata)
		;
	vm_tf->tf_rip += 3;
	return TRUE;
}

/* Is this a vmm specific thing?  or generic?
 *
 * what do we do when we want to kill the vm?  what are our other options? */
bool handle_vmexit(struct guest_thread *gth)
{
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);

	switch (vm_tf->tf_exit_reason) {
	case EXIT_REASON_EPT_VIOLATION:
		return handle_ept_fault(gth);
	case EXIT_REASON_VMCALL:
		return handle_vmcall(gth);
	case EXIT_REASON_IO_INSTRUCTION:
		return handle_io(gth);
	case EXIT_REASON_MSR_WRITE:
	case EXIT_REASON_MSR_READ:
		return handle_msr(gth);
	case EXIT_REASON_APIC_ACCESS:
		return handle_apic_access(gth);
	case EXIT_REASON_HLT:
		return handle_halt(gth);
	case EXIT_REASON_MWAIT_INSTRUCTION:
		return handle_mwait(gth);
	case EXIT_REASON_EXTERNAL_INTERRUPT:
	case EXIT_REASON_APIC_WRITE:
		/* TODO: just ignore these? */
		return TRUE;
	default:
		fprintf(stderr, "Don't know how to handle exit %d\n",
		        vm_tf->tf_exit_reason);
		fprintf(stderr, "RIP %p, shutdown 0x%x\n", vm_tf->tf_rip,
		        vm_tf->tf_exit_reason);
		return FALSE;
	}
}
