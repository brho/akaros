/* Copyright (c) 2017 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#pragma once

#include <vmm/sched.h>

__BEGIN_DECLS

/* This will hang off the gth->user_data for vthread_create, but not for
 * vthread_alloc.  You're on your own there.  Using this is a sign that we may
 * need our own 2LS for vthreads. */
struct vthread_info {
	uintptr_t					stacktop;
};

struct vthread {
	struct guest_thread			gth;
	/* Don't add to this structure without changing how these are allocated. */
};

static struct vm_trapframe *vth_to_vmtf(struct vthread *vth)
{
	return gth_to_vmtf((struct guest_thread*)vth);
}

static struct virtual_machine *vth_to_vm(struct vthread *vth)
{
	return gth_to_vm((struct guest_thread*)vth);
}

void gpci_init(struct vmm_gpcore_init *gpci);
struct vthread *vthread_alloc(struct virtual_machine *vm,
                              struct vmm_gpcore_init *gpci);
void vthread_init_ctx(struct vthread *vth, uintptr_t entry_pt, uintptr_t arg,
                      uintptr_t stacktop);
void vthread_run(struct vthread *vthread);
struct vthread *vthread_create(struct virtual_machine *vm, void *entry,
                               void *arg);

__END_DECLS
