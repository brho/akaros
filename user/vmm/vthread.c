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


static void *page(void *addr, int count)
{
	void *v;
	unsigned long flags = MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE;

	if (addr)
		flags |= MAP_FIXED;
	return mmap(addr, count * 4096, PROT_READ | PROT_WRITE, flags, -1, 0);
}

/* vmsetup is a basic helper function used by vthread_attr_init */
static int vmsetup(struct virtual_machine *vm, int flags)
{
	struct vm_trapframe *vm_tf;
	int i, ret;
	uint8_t *p;

	if (vm->vminit)
		return -EBUSY;

	if (vm->nr_gpcs == 0)
		vm->nr_gpcs = 1;

	vm->gpcis = calloc(vm->nr_gpcs, sizeof(*vm->gpcis));

	/* technically, we don't need these pages for the
	 * all guests. Currently, the kernel requires them. */
	for (i = 0; i < vm->nr_gpcs; i++) {
		p = page(NULL, 3);
		if (!p) {
			werrstr("Can't allocate 3 pages for guest %d: %r", i);
			return -1;
		}
		vm->gpcis[i].posted_irq_desc = &p[0];
		vm->gpcis[i].vapic_addr = &p[4096];
		vm->gpcis[i].apic_addr = &p[8192];
		/* TODO: once we are making these GPCs at the same time as vthreads, we
		 * should set fsbase == the TLS desc of the vthread (if any). */
		vm->gpcis[i].fsbase = 0;
		vm->gpcis[i].gsbase = 0;
	}

	/* Set up default page mappings. */
	setup_paging(vm);

	ret = vmm_init(vm, flags);
	if (ret)
		return ret;

	for (i = 0; i < vm->nr_gpcs; i++) {
		vm->gths[i]->halt_exit = vm->halt_exit;
		vm_tf = gth_to_vmtf(vm->gths[i]);
		vm_tf->tf_cr3 = (uint64_t) vm->root;
	}
	vm->vminit = 1;

	return 0;
}

/* vthread_addr sets up a virtual_machine struct such that functions
 * can start up VM guests.  It is like pthread_attr in that it sets up
 * default attributes and can be used in vthread_create calls. If
 * vm->nrgpcs is not set then the vm will be set up for 1 guest. */
int vthread_attr_init(struct virtual_machine *vm, int vmmflags)
{
	return vmsetup(vm, vmmflags);
}

#define DEFAULT_STACK_SIZE 65536
/* vthread_create creates and starts a VM guest. The interface is intended
 * to be as much like pthread_create as possible. */
int vthread_create(struct virtual_machine *vm, int guest, void *rip, void *arg)
{
	struct vm_trapframe *vm_tf;
	int ret;
	uint64_t *stack, *tos;

	if (!vm->vminit) {
		return -EAGAIN;
	}

	if (guest > vm->nr_gpcs)
		return -ENOENT;

	vm_tf = gth_to_vmtf(vm->gths[guest]);

	/* For now we make the default VM stack pretty small.
	 * We can grow it as needed. */
	if (!vm_tf->tf_rsp) {
		ret = posix_memalign((void **)&stack, 4096, DEFAULT_STACK_SIZE);
		if (ret)
			return ret;
		/* touch the top word on the stack so we don't page fault
		 * on that in the VM. */
		tos = &stack[DEFAULT_STACK_SIZE/sizeof(uint64_t) - 1];
		*tos = 0;
		vm_tf->tf_rsp = (uint64_t) tos;
	}
	vm_tf->tf_rip = (uint64_t)rip;
	vm_tf->tf_rdi = (uint64_t)arg;
	start_guest_thread(vm->gths[guest]);
	return 0;
}
