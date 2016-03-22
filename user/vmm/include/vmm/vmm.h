/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * VMM.h */

#pragma once

#include <ros/vmm.h>
#include <vmm/sched.h>

char *regname(uint8_t reg);
int decode(struct guest_thread *vm_thread, uint64_t *gpa, uint8_t *destreg,
           uint64_t **regp, int *store, int *size, int *advance);
int io(struct guest_thread *vm_thread);
void showstatus(FILE *f, struct guest_thread *vm_thread);
int msrio(struct guest_thread *vm_thread, uint32_t opcode);
int do_ioapic(struct guest_thread *vm_thread, uint64_t gpa,
              int destreg, uint64_t *regp, int store);



/* Intel VM Trap Injection Fields */
#define VM_TRAP_VALID               (1 << 31)
#define VM_TRAP_ERROR_CODE          (1 << 11)
#define VM_TRAP_HARDWARE            (3 << 8)
/* End Intel VM Trap Injection Fields */
