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
void vthread_join(struct vthread *vth, void **retval_loc);
/* Callback, here for sched.c */
void __vthread_exited(struct vthread *vth);

/* Vmcall support.
 *
 * Vthread apps can make their own vmcalls, either replacing this list or
 * growing their own.  vth_handle_vmcall() will handle all of these.  Apps can
 * start their list at VTH_VMCALL_NEXT. */

#define VTH_VMCALL_NULL			0
#define VTH_VMCALL_PRINTC		1
#define VTH_VMCALL_EXIT			2
#define VTH_VMCALL_NEXT			3

/* TODO: arch-specific */
static long raw_vmcall(long arg0, long arg1, long arg2, long arg3, long arg4,
                       unsigned int vmcall_nr)
{
	long ret;
	register long r8 asm ("r8") = arg4;

	asm volatile("vmcall"
	             : "=a"(ret)
	             : "a"(vmcall_nr), "D"(arg0), "S"(arg1), "d"(arg2), "c"(arg3),
	               "r"(r8));
	return ret;
}

long vmcall(unsigned int vmcall_nr, ...);
bool vth_handle_vmcall(struct guest_thread *gth, struct vm_trapframe *vm_tf);

__END_DECLS
