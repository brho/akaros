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

static void *page(void *addr, int count)
{
	void *v;
	unsigned long flags = MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE;

	if (addr)
		flags |= MAP_FIXED;
	return mmap(addr, count * 4096, PROT_READ | PROT_WRITE, flags, -1, 0);
}

static void vmsetup(void *arg)
{
	struct virtual_machine *vm = (struct virtual_machine *)arg;
	struct vm_trapframe *vm_tf;
	int i, ret;
	uint8_t *p;
	struct vmm_gpcore_init *gpcis;

	if (vm->nr_gpcs == 0)
		vm->nr_gpcs = 1;

	gpcis = calloc(vm->nr_gpcs, sizeof(struct vmm_gpcore_init));

	/* technically, we don't need these pages for the
	 * all guests. Currently, the kernel requires them. */
	for (i = 0; i < vm->nr_gpcs; i++) {
		p = page(NULL, 3);
		if (!p)
			panic("Can't allocate 3 pages for guest %d: %r", i);
		gpcis[i].posted_irq_desc = &p[0];
		gpcis[i].vapic_addr = &p[4096];
		gpcis[i].apic_addr = &p[8192];
		/* TODO: once we are making these GPCs at the same time as vthreads, we
		 * should set fsbase == the TLS desc of the vthread (if any). */
		gpcis[i].fsbase = 0;
		gpcis[i].gsbase = 0;
	}

	/* Set up default page mappings. */
	setup_paging(vm);

	ret = vmm_init(vm, gpcis, 0);
	assert(!ret);
	free(gpcis);

	for (i = 0; i < vm->nr_gpcs; i++) {
		vm_tf = gpcid_to_vmtf(vm, i);
		vm_tf->tf_cr3 = (uint64_t) vm->root;
	}
}

struct vthread *vthread_alloc(struct virtual_machine *vm, int guest)
{
	static parlib_once_t once = PARLIB_ONCE_INIT;

	parlib_run_once(&once, vmsetup, vm);

	if (guest > vm->nr_gpcs)
		return NULL;
	return (struct vthread*)gpcid_to_gth(vm, guest);
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
static uintptr_t alloc_stacktop(void)
{
	int ret;
	uintptr_t *stack, *tos;

	ret = posix_memalign((void **)&stack, PGSIZE, DEFAULT_STACK_SIZE);
	if (ret)
		return 0;
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
	stacktop = alloc_stacktop();
	assert(stacktop);
	/* Yes, an evil part of me thought of using the top of the stack for this
	 * struct's storage. */
	gth->user_data = malloc(sizeof(struct vthread_info));
	assert(gth->user_data);
	info = (struct vthread_info*)gth->user_data;
	info->stacktop = stacktop;
	return stacktop;
}

struct vthread *vthread_create(struct virtual_machine *vm, int guest,
                               void *entry, void *arg)
{
	struct vthread *vth;

	vth = vthread_alloc(vm, guest);
	if (!vth)
		return NULL;
	vthread_init_ctx(vth, (uintptr_t)entry, (uintptr_t)arg, vth_get_stack(vth));
	vthread_run(vth);
	return vth;
}
