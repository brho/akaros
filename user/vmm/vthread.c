/* Copyright (c) 2016 Google Inc.
 *
 * See LICENSE for details.
 *
 * Helper functions for virtual machines */

#include <errno.h>
#include <stdlib.h>
#include <parlib/bitmask.h>
#include <parlib/uthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <vmm/vmm.h>
#include <vmm/vthread.h>

static void *pages(size_t count)
{
	void *v;
	unsigned long flags = MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE;

	return mmap(0, count * PGSIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
}

static void vmsetup(void *arg)
{
	struct virtual_machine *vm = (struct virtual_machine *)arg;

	setup_paging(vm);
	vm->nr_gpcs = 0;
	vm->__gths = NULL;
	vm->gth_array_elem = 0;
	uthread_mcp_init();
}

void gpci_init(struct vmm_gpcore_init *gpci)
{
	uint8_t *p;

	/* Technically, we don't need these pages for the all guests. Currently, the
	 * kernel requires them. */
	p = pages(3);
	if (!p)
		panic("Can't allocate 3 pages for guest: %r");
	gpci->posted_irq_desc = &p[0];
	gpci->vapic_addr = &p[4096];
	gpci->apic_addr = &p[8192];
	/* TODO: once we are making these GPCs at the same time as vthreads, we
	 * should set fsbase == the TLS desc of the vthread (if any). */
	gpci->fsbase = 0;
	gpci->gsbase = 0;
}

/* Helper, grows the array of guest_threads in vm.  Concurrent readers
 * (gpcid_to_gth()) need to use a seq-lock-style of concurrency.  They could
 * read the old array even after we free it.
 *
 * Unlike in the kernel, concurrent readers in userspace shouldn't even read
 * freed memory.  Electric fence could catch that fault.  Until we have a decent
 * userspace RCU, we can avoid these faults WHP by just sleeping. */
static void __grow_gth_array(struct virtual_machine *vm,
                             unsigned int new_nr_gths)
{
	struct guest_thread **new_array, **old_array;
	size_t new_nr_elem;

	if (new_nr_gths <= vm->gth_array_elem)
		return;
	/* TODO: (RCU) we could defer the free */
	old_array = vm->__gths;
	new_nr_elem = MAX(vm->gth_array_elem * 2, new_nr_gths);
	new_array = calloc(new_nr_elem, sizeof(void*));
	assert(new_array);
	memcpy(new_array, vm->__gths, sizeof(void*) * vm->nr_gpcs);
	wmb();	/* all elements written before changing pointer */
	vm->__gths = new_array;
	wmb();	/* ptr written before potentially clobbering freed memory. */
	uthread_usleep(1000);	/* hack for electric fence */
	free(old_array);
}

void __add_gth_to_vm(struct virtual_machine *vm, struct guest_thread *gth)
{
	__grow_gth_array(vm, vm->nr_gpcs + 1);
	vm->__gths[vm->nr_gpcs] = gth;
	wmb();	/* concurrent readers will check nr_gpcs first */
	vm->nr_gpcs++;
}

struct vthread *vthread_alloc(struct virtual_machine *vm,
                              struct vmm_gpcore_init *gpci)
{
	static parlib_once_t once = PARLIB_ONCE_INIT;
	struct guest_thread *gth;
	int ret;

	parlib_run_once(&once, vmsetup, vm);

	uth_mutex_lock(&vm->mtx);
	ret = syscall(SYS_vmm_add_gpcs, 1, gpci);
	assert(ret == 1);
	gth = create_guest_thread(vm, vm->nr_gpcs, gpci);
	assert(gth);
	__add_gth_to_vm(vm, gth);
	uth_mutex_unlock(&vm->mtx);
	/* TODO: somewhat arch specific */
	gth_to_vmtf(gth)->tf_cr3 = (uintptr_t)vm->root;
	return (struct vthread*)gth;
}

/* TODO: this is arch specific */
void vthread_init_ctx(struct vthread *vth, uintptr_t entry_pt, uintptr_t arg,
                      uintptr_t stacktop)
{
	struct vm_trapframe *vm_tf = vth_to_vmtf(vth);

	vm_tf->tf_rip = entry_pt;
	vm_tf->tf_rdi = arg;
	vm_tf->tf_rsp = stacktop;
}

void vthread_run(struct vthread *vthread)
{
	start_guest_thread((struct guest_thread*)vthread);
}

#define DEFAULT_STACK_SIZE 65536
static uintptr_t alloc_stacktop(struct virtual_machine *vm)
{
	int ret;
	uintptr_t *stack, *tos;

	ret = posix_memalign((void **)&stack, PGSIZE, DEFAULT_STACK_SIZE);
	if (ret)
		return 0;
	add_pte_entries(vm, (uintptr_t)stack,
	                (uintptr_t)stack + DEFAULT_STACK_SIZE);
	/* touch the top word on the stack so we don't page fault
	 * on that in the VM. */
	tos = &stack[DEFAULT_STACK_SIZE / sizeof(uint64_t) - 1];
	*tos = 0;
	return (uintptr_t)tos;
}

static uintptr_t vth_get_stack(struct vthread *vth)
{
	struct guest_thread *gth = (struct guest_thread*)vth;
	struct vthread_info *info = (struct vthread_info*)gth->user_data;
	uintptr_t stacktop;

	if (info) {
		assert(info->stacktop);
		return info->stacktop;
	}
	stacktop = alloc_stacktop(vth_to_vm(vth));
	assert(stacktop);
	/* Yes, an evil part of me thought of using the top of the stack for this
	 * struct's storage. */
	gth->user_data = malloc(sizeof(struct vthread_info));
	assert(gth->user_data);
	info = (struct vthread_info*)gth->user_data;
	info->stacktop = stacktop;
	return stacktop;
}

struct vthread *vthread_create(struct virtual_machine *vm, void *entry,
                               void *arg)
{
	struct vthread *vth;
	struct vmm_gpcore_init gpci[1];

	gpci_init(gpci);
	vth = vthread_alloc(vm, gpci);
	if (!vth)
		return NULL;
	vthread_init_ctx(vth, (uintptr_t)entry, (uintptr_t)arg, vth_get_stack(vth));
	vthread_run(vth);
	return vth;
}
