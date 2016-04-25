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

/* Lookup helpers */

static struct virtual_machine *gth_to_vm(struct guest_thread *gth)
{
	/* TODO */
	return 0;
}
