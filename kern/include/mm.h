/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management for processes: syscall related functions, virtual memory
 * regions, etc. */

#ifndef ROS_KERN_MM_H
#define ROS_KERN_MM_H

#include <ros/common.h>
#include <ros/mman.h>
#include <atomic.h>
#include <sys/queue.h>
#include <slab.h>

struct file;
struct proc;								/* preprocessor games */

/* Basic structure defining a region of a process's virtual memory.  Note we
 * don't refcnt these.  Either they are in the TAILQ/tree, or they should be
 * freed.  There should be no other references floating around.  We still need
 * to sort out how we share memory and how we'll do private memory with these
 * VMRs. */
struct vm_region {
	TAILQ_ENTRY(vm_region)		vm_link;
	TAILQ_ENTRY(vm_region)		vm_pm_link;
	struct proc					*vm_proc;	/* owning process, for now */
	uintptr_t					vm_base;
	uintptr_t					vm_end;
	int							vm_prot;	
	int							vm_flags;	
	struct file					*vm_file;
	size_t						vm_foff;
};
TAILQ_HEAD(vmr_tailq, vm_region);			/* Declares 'struct vmr_tailq' */

/* VM Region Management Functions.  For now, these just maintain themselves -
 * anything related to mapping needs to be done by the caller. */
void vmr_init(void);
struct vm_region *create_vmr(struct proc *p, uintptr_t va, size_t len);
struct vm_region *split_vmr(struct vm_region *vmr, uintptr_t va);
int merge_vmr(struct vm_region *first, struct vm_region *second);
struct vm_region *merge_me(struct vm_region *vmr);
int grow_vmr(struct vm_region *vmr, uintptr_t va);
int shrink_vmr(struct vm_region *vmr, uintptr_t va);
void destroy_vmr(struct vm_region *vmr);
struct vm_region *find_vmr(struct proc *p, uintptr_t va);
struct vm_region *find_first_vmr(struct proc *p, uintptr_t va);
void isolate_vmrs(struct proc *p, uintptr_t va, size_t len);
void unmap_and_destroy_vmrs(struct proc *p);
int duplicate_vmrs(struct proc *p, struct proc *new_p);
void print_vmrs(struct proc *p);

/* mmap() related functions.  These manipulate VMRs and change the hardware page
 * tables.  Any requests below the LOWEST_VA will silently be upped.  This may
 * be a dynamic proc-specific variable later. */
#define MMAP_LOWEST_VA PGSIZE
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset);
void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file *f, size_t offset);
int mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int munmap(struct proc *p, uintptr_t addr, size_t len);
int handle_page_fault(struct proc *p, uintptr_t va, int prot);
unsigned long populate_va(struct proc *p, uintptr_t va, unsigned long nr_pgs);

/* These assume the mm_lock is held already */
int __do_mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int __do_munmap(struct proc *p, uintptr_t addr, size_t len);

/* Kernel Dynamic Memory Mappings */
/* These two are just about reserving VA space */
uintptr_t get_vmap_segment(unsigned long num_pages);
uintptr_t put_vmap_segment(uintptr_t vaddr, unsigned long num_pages);
/* These two are about actually mapping stuff in some reserved space */
int map_vmap_segment(uintptr_t vaddr, uintptr_t paddr, unsigned long num_pages,
                     int perm);
int unmap_vmap_segment(uintptr_t vaddr, unsigned long num_pages);
/* Helper wrappers, since no one will probably call the *_segment funcs */
uintptr_t vmap_pmem(uintptr_t paddr, size_t nr_bytes);
uintptr_t vmap_pmem_nocache(uintptr_t paddr, size_t nr_bytes);
int vunmap_vmem(uintptr_t vaddr, size_t nr_bytes);

#endif /* !ROS_KERN_MM_H */
