/*
 * MSR emulation
 *
 * Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <ros/vmm.h>
#include <ros/arch/msr-index.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>
#include <vmm/sched.h>
#include <vmm/vmm.h>
#include <ros/arch/trapframe.h>
#include <parlib/alarm.h>

struct emmsr {
	uint32_t reg;
	char *name;
	int (*f)(struct guest_thread *vm_thread, struct emmsr *, uint32_t);
	bool written;
	uint32_t edx, eax;
};

static inline uint64_t read_msr(uint32_t reg)
{
	panic("Not implemented for userspace");
}

static inline void write_msr(uint32_t reg, uint64_t val)
{
	panic("Not implemented for userspace");
}

static int emsr_readonly(struct guest_thread *vm_thread, struct emmsr *,
                         uint32_t);
static int emsr_readzero(struct guest_thread *vm_thread, struct emmsr *,
                         uint32_t);
static int emsr_fakewrite(struct guest_thread *vm_thread, struct emmsr *,
                          uint32_t);
static int emsr_ok(struct guest_thread *vm_thread, struct emmsr *, uint32_t);

struct emmsr emmsrs[] = {
	{MSR_RAPL_POWER_UNIT, "MSR_RAPL_POWER_UNIT", emsr_readzero},
};

static inline uint32_t low32(uint64_t val)
{
	return val & 0xffffffff;
}

static inline uint32_t high32(uint64_t val)
{
	return val >> 32;
}

static int emsr_ok(struct guest_thread *vm_thread, struct emmsr *msr,
                   uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	uint64_t msr_val;

	if (opcode == EXIT_REASON_MSR_READ) {
		msr_val = read_msr(msr->reg);
		vm_tf->tf_rax = low32(msr_val);
		vm_tf->tf_rdx = high32(msr_val);
	} else {
		msr_val = (vm_tf->tf_rdx << 32) | vm_tf->tf_rax;
		write_msr(msr->reg, msr_val);
	}
	return 0;
}

static int emsr_readonly(struct guest_thread *vm_thread, struct emmsr *msr,
                         uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	uint64_t msr_val;

	msr_val = read_msr(msr->reg);
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = low32(msr_val);
		vm_tf->tf_rdx = high32(msr_val);
		return 0;
	}

	fprintf(stderr,"%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

static int emsr_readzero(struct guest_thread *vm_thread, struct emmsr *msr,
                         uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = 0;
		vm_tf->tf_rdx = 0;
		return 0;
	}

	fprintf(stderr,"%s: Tried to write a readonly register\n", msr->name);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

/* pretend to write it, but don't write it. */
static int emsr_fakewrite(struct guest_thread *vm_thread, struct emmsr *msr,
                          uint32_t opcode)
{
	uint32_t eax, edx;
	uint64_t msr_val;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (!msr->written) {
		msr_val = read_msr(msr->reg);
		eax = low32(msr_val);
		edx = high32(msr_val);
	} else {
		eax = msr->eax;
		edx = msr->edx;
	}
	if (opcode == EXIT_REASON_MSR_READ) {
		vm_tf->tf_rax = eax;
		vm_tf->tf_rdx = edx;
		return 0;
	} else {
		msr->edx = vm_tf->tf_rdx;
		msr->eax = vm_tf->tf_rax;
		msr->written = true;
	}
	return 0;
}

static int apic_icr_write(struct guest_thread *vm_thread,
                          struct vmm_gpcore_init *gpci)
{
	/* We currently only handle physical destinations.
	 * TODO(ganshun): Support logical destinations if needed. */
	struct virtual_machine *vm = gth_to_vm(vm_thread);
	struct vm_trapframe *vm_tf = gth_to_vmtf(vm_thread);
	uint32_t destination = vm_tf->tf_rdx & 0xffffffff;
	uint8_t vector = vm_tf->tf_rax & 0xff;
	uint8_t type = (vm_tf->tf_rax >> 8) & 0x7;
	int apic_offset = vm_tf->tf_rcx & 0xff;

	if (destination >= vm->nr_gpcs && destination != 0xffffffff) {
		fprintf(stderr, "UNSUPPORTED DESTINATION 0x%02x!\n",
				destination);
		return SHUTDOWN_UNHANDLED_EXIT_REASON;
	}
	switch (type) {
	case 0:
		/* Send IPI */
		if (destination == 0xffffffff) {
			/* Broadcast */
			for (int i = 0; i < vm->nr_gpcs; i++)
				vmm_interrupt_guest(vm, i, vector);
		} else {
			/* Send individual IPI */
			vmm_interrupt_guest(vm, destination, vector);
		}
		break;
	case 0x5:	/* INIT */
	case 0x6:	/* SIPI */
		/* We don't use INIT/SIPI for SMP boot.  The guest is still allowed to
		 * try to make them for now. */
		break;
	default:
		fprintf(stderr, "Unsupported IPI type %d!\n", type);
		break;
	}

	((uint32_t *)(gpci->vapic_addr))[apic_offset] =
	                                       (uint32_t)(vm_tf->tf_rax);
	((uint32_t *)(gpci->vapic_addr))[apic_offset + 1] =
	                                       (uint32_t)(vm_tf->tf_rdx);
	return 0;
}

static int apic_timer_write(struct guest_thread *gth,
                            struct vmm_gpcore_init *gpci)
{
	uint32_t multiplier;
	uint8_t vector;
	uint32_t initial_count;
	uint32_t divide_config_reg;
	struct alarm_waiter *timer_waiter;
	struct vm_trapframe *vm_tf = gth_to_vmtf(gth);
	int apic_offset = vm_tf->tf_rcx & 0xff;

	((uint32_t *)(gpci->vapic_addr))[apic_offset] =
                                       (uint32_t)(vm_tf->tf_rax);

	/* See if we can set the timer. */
	vector = ((uint32_t *)gpci->vapic_addr)[0x32] & 0xff;
	initial_count = ((uint32_t *)gpci->vapic_addr)[0x38];
	divide_config_reg = ((uint32_t *)gpci->vapic_addr)[0x3E];
	timer_waiter = (struct alarm_waiter*)gth->user_data;

	/* This is a precaution on my part, in case the guest tries to look at
	 * the current count on the lapic. I wanted it to be something other than
	 * 0 just in case. The current count will never be right short of us
	 * properly emulating it. */
	((uint32_t *)(gpci->vapic_addr))[0x39] = initial_count;

	if (!timer_waiter)
		panic("NO WAITER");

	/* Look at the intel manual Vol 3 10.5.4 APIC Timer */
	multiplier = (((divide_config_reg & 0x08) >> 1) |
	              (divide_config_reg & 0x03)) + 1;
	multiplier &= 0x07;

	unset_alarm(timer_waiter);

	if (vector && initial_count) {
		set_awaiter_rel(timer_waiter, initial_count << multiplier);
		set_alarm(timer_waiter);
	}
	return 0;
}

static int emsr_apic(struct guest_thread *vm_thread,
                     struct vmm_gpcore_init *gpci, uint32_t opcode)
{
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);
	int apic_offset = vm_tf->tf_rcx & 0xff;
	uint64_t value;
	int error;

	if (opcode == EXIT_REASON_MSR_READ) {
		if (vm_tf->tf_rcx != MSR_LAPIC_ICR) {
			vm_tf->tf_rax = ((uint32_t *)(gpci->vapic_addr))[apic_offset];
			vm_tf->tf_rdx = 0;
		} else {
			vm_tf->tf_rax = ((uint32_t *)(gpci->vapic_addr))[apic_offset];
			vm_tf->tf_rdx = ((uint32_t *)(gpci->vapic_addr))[apic_offset + 1];
		}
	} else {
		switch (vm_tf->tf_rcx) {
		case MSR_LAPIC_ICR:
			error = apic_icr_write(vm_thread, gpci);
			if (error != 0)
				return error;
			break;
		case MSR_LAPIC_DIVIDE_CONFIG_REG:
		case MSR_LAPIC_LVT_TIMER:
		case MSR_LAPIC_INITIAL_COUNT:
			apic_timer_write(vm_thread, gpci);
			break;
		default:
			((uint32_t *)(gpci->vapic_addr))[apic_offset] =
		                                       (uint32_t)(vm_tf->tf_rax);
		}
	}
	return 0;
}

int msrio(struct guest_thread *vm_thread, struct vmm_gpcore_init *gpci,
          uint32_t opcode)
{
	int i;
	struct vm_trapframe *vm_tf = &(vm_thread->uthread.u_ctx.tf.vm_tf);

	if (vm_tf->tf_rcx >= MSR_LAPIC_ID && vm_tf->tf_rcx < MSR_LAPIC_END)
		return emsr_apic(vm_thread, gpci, opcode);

	for (i = 0; i < sizeof(emmsrs)/sizeof(emmsrs[0]); i++) {
		if (emmsrs[i].reg != vm_tf->tf_rcx)
			continue;
		return emmsrs[i].f(vm_thread, &emmsrs[i], opcode);
	}
	printd("msrio for 0x%lx failed\n", vm_tf->tf_rcx);
	return SHUTDOWN_UNHANDLED_EXIT_REASON;
}

