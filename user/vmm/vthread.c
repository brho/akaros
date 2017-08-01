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

/* vmsetup is a basic helper function used by vthread_attr_init
 * and vthread_attr_kernel_init. */
static int vmsetup(struct virtual_machine *vm, int flags)
{
	unsigned long long *p512, *p1;
	struct vm_trapframe *vm_tf;
	int i, ret;
	uint8_t *p;

	if (vm->vminit)
		return -EBUSY;

	if (vm->nr_gpcs == 0)
		vm->nr_gpcs = 1;

	vm->gpcis = calloc(vm->nr_gpcs, sizeof(*vm->gpcis));

	/* Set up default page mappings. The common case,
	 * for user VM threads and kernel VM threads, is that
	 * they need some kind of initial page tables. The kernels
	 * will almost always throw them away; the user VM threads
	 * will almost always continue to use them. Using two
	 * pages and setting up an initial page table is
	 * cheap and makes users lives easier. This initial
	 * page table can grow to 512 GiB, which should be enough
	 * for now.
	 *
	 * At the same time, we allow users to select other
	 * arrangements if they wish.  Here's a simple example: is it
	 * possible someone will want a different guest page table for
	 * every guest? Yes.
	 *
	 * We lock the page table to 0x1000000 for now. We can't just
	 * let it pick anything as it may pick something the guest
	 * can't address (i.e. outside EPT range). */

	/* Allocate 2 pages for page table pages: a page of
	 * 512 GiB PTEs with only one entry filled to point to
	 * a page of 1 GiB PTEs; a page of 1 GiB PTEs with
	 * only one entry filled. */

	p512 = page((void *)0x1000000, 2);
	if (!p512) {
		werrstr("page table allocation failed: %r\n");
		return -1;
	}
	p1 = &p512[512];
	vm->root = p512;

	/* Set up a 1:1 ("identity") page mapping from host
	 * virtual to guest physical for 1 GiB.  This mapping
	 * is used unless the guest (e.g. Linux) sets up its
	 * own page tables. Be aware that the values stored in
	 * the table are physical addresses.  This is subtle
	 * and mistakes are easily disguised due to the
	 * identity mapping, so take care when manipulating
	 * these mappings. Note: we don't yet have symbols for
	 * "start of virtual address common to host and guest"
	 * so we just use  the first GiB for now. */
	p512[PML4(0x400000)] = (uint64_t)p1 | PTE_KERN_RW;
	p1[PML3(0x400000)] = PTE_PS | PTE_KERN_RW;

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
	}

	ret = vmm_init(vm, flags);
	if (ret)
		return ret;

	for (i = 0; i < vm->nr_gpcs; i++) {
		vm->gths[i]->halt_exit = vm->halt_exit;
		vm_tf = gth_to_vmtf(vm->gths[i]);
		vm_tf->tf_cr3 = (uint64_t)p512;
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

/* vthread_attr_kernel_init sets up minimum basic attributes for
 * running a kernel, as opposed to just user mode.  This setup
 * includes an APIC page at 0xfee00000, to be shared by all cores. */
int vthread_attr_kernel_init(struct virtual_machine *vm, int vmmflags)
{
	int ret;
	int i;
	uint32_t *apic;

	ret = vmsetup(vm, vmmflags);
	if (ret)
		return ret;

	for (i = 0; i < vm->nr_gpcs; i++) {
		apic = vm->gpcis[i].apic_addr;
		apic[0x30 / 4] = 0x01060015;
	}
	return 0;
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
