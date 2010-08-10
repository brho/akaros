/* Copyright (c) 2009, 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management for processes: syscall related functions, virtual memory
 * regions, etc. */

#ifndef ROS_KERN_MM_H
#define ROS_KERN_MM_H

#include <ros/common.h>
#include <atomic.h>
#include <sys/queue.h>
#include <slab.h>

struct file;
struct proc;								/* preprocessor games */

/* This might turn into a per-process mem management structure.  For now, we're
 * using the proc struct.  This would have things like the vmr list/tree, cr3,
 * mem usage stats, something with private memory, etc.  Not sure if we'll ever
 * need this. */
struct mm {
	spinlock_t mm_lock;
};

/* Basic structure defining a region of a process's virtual memory.  Note we
 * don't refcnt these.  Either they are in the TAILQ/tree, or they should be
 * freed.  There should be no other references floating around.  We still need
 * to sort out how we share memory and how we'll do private memory with these
 * VMRs. */
struct vm_region {
	TAILQ_ENTRY(vm_region)		vm_link;
	struct proc					*vm_proc;	/* owning process, for now */
	//struct mm 					*vm_mm;		/* owning address space */
	uintptr_t					vm_base;
	uintptr_t					vm_end;
	int							vm_prot;	
	int							vm_flags;	
	struct file					*vm_file;
	size_t						vm_foff;
};
TAILQ_HEAD(vmr_tailq, vm_region);			/* Declares 'struct vmr_tailq' */

#include <process.h>						/* preprocessor games */

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
void destroy_vmrs(struct proc *p);
void duplicate_vmrs(struct proc *p, struct proc *new_p);
void print_vmrs(struct proc *p);

/* mmap() related functions.  These manipulate VMRs and change the hardware page
 * tables.  Any requests below the LOWEST_VA will silently be upped.  This may
 * be a dynamic proc-specific variable later. */
#define MMAP_LOWEST_VA 0x00001000
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset);
void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
              struct file *f, size_t offset);
int mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int munmap(struct proc *p, uintptr_t addr, size_t len);
int handle_page_fault(struct proc *p, uintptr_t va, int prot);

/* These assume the memory/proc_lock is held already */
void *__do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
                struct file *f, size_t offset);
int __do_mprotect(struct proc *p, uintptr_t addr, size_t len, int prot);
int __do_munmap(struct proc *p, uintptr_t addr, size_t len);
int __handle_page_fault(struct proc* p, uintptr_t va, int prot);

#endif /* !ROS_KERN_MM_H */
