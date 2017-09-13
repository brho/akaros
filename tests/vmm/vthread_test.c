/* Copyright (c) 2017 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * vthread_test: mostly create/join/vmcalls */

#include <vmm/vmm.h>
#include <vmm/vthread.h>
#include <parlib/stdio.h>
#include <parlib/uthread.h>

enum {
	MY_VMCALL_TEST1 = VTH_VMCALL_NEXT,
	MY_VMCALL_TEST2,
};

/* Here's how you can make your own vmcalls and still use the vth vmcalls for
 * the stuff you don't want to reimplement. */
static bool extended_handle_vmcall(struct guest_thread *gth,
                                   struct vm_trapframe *vm_tf)
{
	switch (vm_tf->tf_rax) {
	case MY_VMCALL_TEST1:
		goto out_ok;
	}
	return vth_handle_vmcall(gth, vm_tf);
out_ok:
	vm_tf->tf_rip += 3;
	return TRUE;
};

static struct virtual_machine vm = {.vmcall = extended_handle_vmcall,
                                    .mtx = UTH_MUTEX_INIT};

static void thread_entry(void *arg)
{
	const char nums[] = "123456789";

	for (int i = 0; i < sizeof(nums); i++)
		vmcall(VTH_VMCALL_PRINTC, nums[i]);
	vmcall(MY_VMCALL_TEST1);
	vmcall(VTH_VMCALL_EXIT, arg, 0, 0, 0);
}

int main(int argc, char **argv)
{
	#define NR_VTHS 5
	struct vthread *vths[NR_VTHS];
	void *retvals[NR_VTHS];

	/* Tests multiple threads at once */
	for (long i = 0; i < NR_VTHS; i++)
		vths[i] = vthread_create(&vm, thread_entry, (void*)i);
	for (long i = 0; i < NR_VTHS; i++) {
		vthread_join(vths[i], &retvals[i]);
		assert(retvals[i] == (void*)i);
	}

	/* Tests reuse / GPC leakage */
	for (long i = 0; i < NR_VTHS * 2; i++) {
		vths[0] = vthread_create(&vm, thread_entry, (void*)i);
		vthread_join(vths[0], &retvals[0]);
		assert(retvals[0] == (void*)i);
	}
	assert(vm.nr_gpcs == NR_VTHS);

	return 0;
}
