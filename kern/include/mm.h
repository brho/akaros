/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management for processes: syscall related functions, virtual memory
 * regions, etc. */

#pragma once

#include <ros/common.h>
#include <ros/mman.h>
#include <atomic.h>
#include <sys/queue.h>
#include <slab.h>
#include <kref.h>
#include <rcu.h>

struct chan;
struct fd_table;
struct proc;	/* preprocessor games */

#define F_OR_C_CHAN 2

struct file_or_chan {
	int type;
	struct chan *chan;
	struct fs_file *fsf;	/* weak ref, set during mmap. */
	struct kref kref;
	struct rcu_head rcu;
};

char *foc_to_name(struct file_or_chan *foc);
char *foc_abs_path(struct file_or_chan *foc);
ssize_t foc_read(struct file_or_chan *foc, void *buf, size_t amt, off64_t off);
struct file_or_chan *foc_open(char *path, int omode, int perm);
struct file_or_chan *fd_to_foc(struct fd_table *fdt, int fd);
void foc_incref(struct file_or_chan *foc);
void foc_decref(struct file_or_chan *foc);
void *foc_pointer(struct file_or_chan *foc);
size_t foc_get_len(struct file_or_chan *foc);

/* Basic structure defining a region of a process's virtual memory.  Note we
 * don't refcnt these.  Either they are in the TAILQ/tree, or they should be
 * freed.  There should be no other references floating around.  We still need
 * to sort out how we share memory and how we'll do private memory with these
 * VMRs. */
struct vm_region {
	TAILQ_ENTRY(vm_region)		vm_link;
	TAILQ_ENTRY(vm_region)		vm_pm_link;
	struct proc			*vm_proc;
	uintptr_t			vm_base;
	uintptr_t			vm_end;
	int				vm_prot;
	int				vm_flags;
	struct file_or_chan		*__vm_foc;
	size_t				vm_foff;
	bool				vm_ready; /* racy, for the PM checks */
	bool				vm_shootdown_needed;
};
TAILQ_HEAD(vmr_tailq, vm_region);	/* Declares 'struct vmr_tailq' */

static inline bool vmr_has_file(struct vm_region *vmr)
{
	return vmr->__vm_foc ? true : false;
}

static inline char *vmr_to_filename(struct vm_region *vmr)
{
	assert(vmr_has_file(vmr));
	return foc_to_name(vmr->__vm_foc);
}

void vmr_init(void);
void unmap_and_destroy_vmrs(struct proc *p);
int duplicate_vmrs(struct proc *p, struct proc *new_p);
void print_vmrs(struct proc *p);
void enumerate_vmrs(struct proc *p, void (*func)(struct vm_region *vmr, void
						 *opaque), void *opaque);

/* mmap() related functions.  These manipulate VMRs and change the hardware page
 * tables.  Any requests below the LOWEST_VA will silently be upped.  This may
 * be a dynamic proc-specific variable later. */
#define MMAP_LOWEST_VA PAGE_SIZE
#define MMAP_LD_FIXED_VA 0x100000
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset);
void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file_or_chan *foc, size_t offset);
int mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int munmap(struct proc *p, uintptr_t addr, size_t len);
int handle_page_fault(struct proc *p, uintptr_t va, int prot);
int handle_page_fault_nofile(struct proc *p, uintptr_t va, int prot);
unsigned long populate_va(struct proc *p, uintptr_t va, unsigned long nr_pgs);

/* These assume the mm_lock is held already */
int __do_mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int __do_munmap(struct proc *p, uintptr_t addr, size_t len);

/* Kernel Dynamic Memory Mappings */
struct arena *vmap_arena;
void vmap_init(void);
/* Gets PML1 page-aligned virtual addresses for the kernel's dynamic vmap.
 * You'll need to map it to something.  When you're done, 'put-' will also unmap
 * the vaddr for you. */
uintptr_t get_vmap_segment(size_t nr_bytes);
void put_vmap_segment(uintptr_t vaddr, size_t nr_bytes);
int map_vmap_segment(uintptr_t vaddr, uintptr_t paddr, unsigned long num_pages,
                     int perm);
/* Helper wrappers for getting and mapping a specific paddr. */
uintptr_t vmap_pmem(uintptr_t paddr, size_t nr_bytes);
uintptr_t vmap_pmem_nocache(uintptr_t paddr, size_t nr_bytes);
uintptr_t vmap_pmem_writecomb(uintptr_t paddr, size_t nr_bytes);
int vunmap_vmem(uintptr_t vaddr, size_t nr_bytes);
