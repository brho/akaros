/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * VMM.h */

#pragma once

#include <ros/vmm.h>
#include <vmm/sched.h>

/* The listing of VIRTIO MMIO devices. We currently only expect to have 2,
 * console and network. Only the console is implemented right now.*/
enum {
	VIRTIO_MMIO_CONSOLE_DEV,

	/* This should always be the last entry. */
	VIRTIO_MMIO_MAX_NUM_DEV = 2,
};

/* Structure to encapsulate all of the bookkeeping for a VM. */
struct virtual_machine {
	struct guest_thread			**gths;
	unsigned int				nr_gpcs;
	struct vmm_gpcore_init		*gpcis;

	/* TODO: put these in appropriate structures.  e.g., virtio things in
	 * something related to virtio.  low4k in something related to the guest's
	 * memory. */
	uint8_t						*low4k;
	struct virtio_mmio_dev		*virtio_mmio_devices[VIRTIO_MMIO_MAX_NUM_DEV];
};

char *regname(uint8_t reg);
int decode(struct guest_thread *vm_thread, uint64_t *gpa, uint8_t *destreg,
           uint64_t **regp, int *store, int *size, int *advance);
bool io(struct guest_thread *vm_thread);
void showstatus(FILE *f, struct guest_thread *vm_thread);
uint64_t gvatogpa(struct guest_thread *vm_thread, uint64_t va);
uint64_t rippa(struct guest_thread *vm_thread);
int msrio(struct guest_thread *vm_thread, struct vmm_gpcore_init *gpci,
          uint32_t opcode);
int do_ioapic(struct guest_thread *vm_thread, uint64_t gpa,
              int destreg, uint64_t *regp, int store);
bool handle_vmexit(struct guest_thread *gth);
int __apic_access(struct guest_thread *vm_thread, uint64_t gpa, int destreg,
                  uint64_t *regp, int store);
int vmm_interrupt_guest(struct virtual_machine *vm, unsigned int gpcoreid,
                        unsigned int vector);

/* Lookup helpers */

static struct virtual_machine *gth_to_vm(struct guest_thread *gth)
{
	return ((struct vmm_thread*)gth)->vm;
}

static struct vm_trapframe *gth_to_vmtf(struct guest_thread *gth)
{
	return &gth->uthread.u_ctx.tf.vm_tf;
}

static struct vmm_gpcore_init *gth_to_gpci(struct guest_thread *gth)
{
	struct virtual_machine *vm = gth_to_vm(gth);

	return &vm->gpcis[gth->gpc_id];
}

static struct virtual_machine *get_my_vm(void)
{
	return ((struct vmm_thread*)current_uthread)->vm;
}
