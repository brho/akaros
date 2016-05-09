/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * VMM.h */

#pragma once

#include <ros/vmm.h>
#include <vmm/sched.h>

/* Structure to encapsulate all of the bookkeeping for a VM. */
struct virtual_machine {
	struct guest_thread			**gths;
	unsigned int				nr_gpcs;
	struct vmm_gpcore_init		*gpcis;

	/* TODO: put these in appropriate structures.  e.g., virtio things in
	 * something related to virtio.  low4k in something related to the guest's
	 * memory. */
	uintptr_t					virtio_mmio_base;
	int							virtio_irq;
	uint8_t						*low4k;
	struct virtio_mmio_dev		*cons_mmio_dev;
};

char *regname(uint8_t reg);
int decode(struct guest_thread *vm_thread, uint64_t *gpa, uint8_t *destreg,
           uint64_t **regp, int *store, int *size, int *advance);
int io(struct guest_thread *vm_thread);
void showstatus(FILE *f, struct guest_thread *vm_thread);
int msrio(struct guest_thread *vm_thread, struct vmm_gpcore_init *gpci,
          uint32_t opcode);
int do_ioapic(struct guest_thread *vm_thread, uint64_t gpa,
              int destreg, uint64_t *regp, int store);
bool handle_vmexit(struct guest_thread *gth);
int __apic_access(struct guest_thread *vm_thread, uint64_t gpa, int destreg,
                  uint64_t *regp, int store);

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
