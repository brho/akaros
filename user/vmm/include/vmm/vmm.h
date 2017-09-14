/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * VMM.h */

#pragma once

#include <ros/vmm.h>
#include <vmm/sched.h>
#include <vmm/linux_bootparam.h>
#include <parlib/stdio.h>
#include <libelf.h>

// We need to reserve an area of the low 4G for thinks like tables, APIC, and
// so on. So far, 256 MiB has been more than enough, so ...
#define MiB 0x100000ull
#define MinMemory (16*MiB)
#define GiB (0x40000000ULL)
#define _4GiB (0x100000000ULL)
// BIOS conventions from 1978 make it smart to reserve the low 64k
#define LOW64K 65536
// The RESERVED area is for all the random junk like devices, ACPI, etc.
// We just give it the top 1 GiB of the 32-bit address space, which
// nicely translates to one GiB PTE.
#define RESERVED 0xC0000000ULL
#define RESERVEDSIZE (_4GiB - RESERVED)
// Start the VM at 16 MiB, a standard number for 64 bit kernels on amd64
#define KERNSTART 0x1000000

#define VM_PAGE_FAULT			14

// APIC Guest Physical Address, a well known constant.
#define APIC_GPA			0xfee00000ULL

/* The listing of VIRTIO MMIO devices. We currently only expect to have 2,
 * console and network. Only the console is fully implemented right now.*/
enum {
	VIRTIO_MMIO_CONSOLE_DEV,
	VIRTIO_MMIO_NETWORK_DEV,
	VIRTIO_MMIO_BLOCK_DEV,

	/* This should always be the last entry. */
	VIRTIO_MMIO_MAX_NUM_DEV,
};

/* Structure to encapsulate all of the bookkeeping for a VM. */
struct virtual_machine {
	/* Big mutext for pagetables and __gths/ nr_gpcs */
	uth_mutex_t					mtx;
	struct guest_thread			**__gths;
	unsigned int				nr_gpcs;
	/* up_gpcs should not need synchronization. only the BSP should be making
	 * startup vmcalls. For security's sake we might still want to lock in the
	 * future. TODO(ganshun)
	 * up_gpcs refers to the number of guest pcores that have
	 * been started so far. */
	unsigned int				up_gpcs;

	/* TODO: put these in appropriate structures.  e.g., virtio things in
	 * something related to virtio.  low4k in something related to the guest's
	 * memory. */
	uint8_t						*low4k;
	struct virtio_mmio_dev		*virtio_mmio_devices[VIRTIO_MMIO_MAX_NUM_DEV];

	/* minimum and maximum physical memory addresses. When we set up the initial
	 * default page tables we use this range. Note that even if the "physical"
	 * memory has holes, we'll create PTEs for it. This seems enough for now but
	 * we shall see. */
	uintptr_t					minphys;
	uintptr_t					maxphys;

	/* Default root pointer to use if one is not set in a
	 * guest thread. We expect this to be the common case,
	 * where all guests share a page table. It's not required
	 * however. setup_paging now updates this to point to the initial set of
	 * page tables for the guest. */
	void						*root;

	/* Default value for whether guest threads halt on an exit. */
	bool						halt_exit;
	/* Override for vmcall (vthreads) */
	bool (*vmcall)(struct guest_thread *gth, struct vm_trapframe *);
};

struct elf_aux {
	unsigned long v[2];
};

char *regname(uint8_t reg);
int decode(struct guest_thread *vm_thread, uint64_t *gpa, uint8_t *destreg,
           uint64_t **regp, int *store, int *size, int *advance);
int io(struct guest_thread *vm_thread);
void showstatus(FILE *f, struct guest_thread *vm_thread);
int gvatogpa(struct guest_thread *vm_thread, uint64_t va, uint64_t *pa);
int rippa(struct guest_thread *vm_thread, uint64_t *pa);
int msrio(struct guest_thread *vm_thread, struct vmm_gpcore_init *gpci,
          uint32_t opcode);
int do_ioapic(struct guest_thread *vm_thread, uint64_t gpa,
              int destreg, uint64_t *regp, int store);
bool handle_vmexit(struct guest_thread *gth);
int __apic_access(struct guest_thread *vm_thread, uint64_t gpa, int destreg,
                  uint64_t *regp, int store);
int vmm_interrupt_guest(struct virtual_machine *vm, unsigned int gpcoreid,
                        unsigned int vector);
uintptr_t load_elf(char *filename, uint64_t offset, uint64_t *highest,
                   Elf64_Ehdr *ehdr_out);
ssize_t setup_initrd(char *filename, void *membase, size_t memsize);

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
	return &gth->gpci;
}

static struct guest_thread *gpcid_to_gth(struct virtual_machine *vm,
                                         unsigned int gpc_id)
{
	struct guest_thread **array;
	struct guest_thread *gth;

	/* Syncing with any dynamic growth of __gths */
	do {
		array = ACCESS_ONCE(vm->__gths);
		gth = array[gpc_id];
		rmb();	/* read ret before rereading array pointer */
	} while (array != ACCESS_ONCE(vm->__gths));
	return gth;
}

static struct vm_trapframe *gpcid_to_vmtf(struct virtual_machine *vm,
                                          unsigned int gpc_id)
{
	return gth_to_vmtf(gpcid_to_gth(vm, gpc_id));
}

static struct virtual_machine *get_my_vm(void)
{
	return ((struct vmm_thread*)current_uthread)->vm;
}

/* memory helpers */
void *init_e820map(struct virtual_machine *vm, struct boot_params *bp);
void checkmemaligned(uintptr_t memstart, size_t memsize);
void mmap_memory(struct virtual_machine *vm, uintptr_t memstart,
                 size_t memsize);
bool mmap_file(const char *path, uintptr_t memstart, uintptr_t memsize,
               uint64_t protections, uint64_t offset);
void add_pte_entries(struct virtual_machine *vm, uintptr_t start,
                     uintptr_t end);
void setup_paging(struct virtual_machine *vm);
void *setup_biostables(struct virtual_machine *vm,
                       void *a, void *smbiostable);
void *populate_stack(uintptr_t *stack, int argc, char *argv[],
                         int envc, char *envp[],
                         int auxc, struct elf_aux auxv[]);
