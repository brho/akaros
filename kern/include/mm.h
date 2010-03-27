/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Memory management for processes: syscall related functions, virtual memory
 * regions, etc.
 */

#ifndef ROS_KERN_MM_H
#define ROS_KERN_MM_H

#include <ros/common.h>
#include <process.h>
#include <atomic.h>
#include <sys/queue.h>
#include <slab.h>

/* Memory region for a process, consisting of linear(virtual) addresses.  This
 * is what the kernel allocates a process, and the physical mapping can be done
 * lazily (or not).  This way, if a page is swapped out, and the PTE says it
 * isn't present, we still have a way to account for how the whole region ought
 * to be dealt with.
 * Some things are per-region:
 * - probably something with shared memory
 * - mmaping files: we can have a logical connection to something other than
 *   anonymous memory
 * - on a fault, was this memory supposed to be there? (swap, lazy, etc), or is
 *   the region free?
 * Others are per-page:
 * - was this page supposed to be protected somehow(guard)? could be per-region
 * - where is this page in the swap?
 * If we try to store this info in the PTE, we only have 31 bits, and it's more
 * arch dependent.  Handling jumbos is a pain.  And it's replicated across all
 * pages for a coarse granularity things.  And we can't add things easily.
 *
 * so a process has a (sorted) list of these for it's VA space, hanging off it's
 * struct proc.  or off it's mm?
 * - we don't share an mm between processes anymore (tasks/threads)
 *   - though we share most everything with vpm.
 *   - want to be able to do all the same things with vpm as with regular mem
 *     (file back mmap, etc)
 *   - contexts or whatever share lots of mem things, like accounting, limits,
 *   overall process stuff, the rest of the page tables.
 *   	- so there should be some overall mm, and probably directly in the
 *   	struct proc (or just one other struct directly embedded, not a pointer
 *   	to one where a bunch of processes use it)
 *   		- if we embed, mm.h doesn't need to know about process.h
 *   so an mm can have a bunch of "address spaces" - or at least different
 *   contexts
 *
 * how does this change or where does this belong with virtual private memory?
 * will also affect get_free_va_range
 * 	- also, do we want a separate brk per?  or just support mmap on private mem?
 */
struct vm_region {
	TAILQ_ENTRY(vm_region) link; // actually, i'd like a sorted tree of these
	uintptr_t base;
	size_t len;
	int perm;
};
TAILQ_HEAD(vm_region_list, vm_region); // Declares 'struct memregion_list'

// at least for now, we aren't using vm regions. we're storing pointers
// to pfault_info_t inside the PTEs in an arch-specific way.
struct file;
typedef struct pfault_info {
	struct file* file; // or NULL for zero-fill
	size_t pgoff; // offset into file
	size_t read_len; // amount of file to read into this page (zero-fill rest)
	int perm;
} pfault_info_t;

void mmap_init(void);

pfault_info_t* pfault_info_alloc(struct file* file);
void pfault_info_free(pfault_info_t* pfi);

struct mm {
	spinlock_t mm_lock;
	// per-process memory management stuff
	// cr3(s), accounting, possibly handler methods for certain types of faults
	// lists of vm_regions for all contexts
	// base cr3 for all contexts
	// previous brk, last checked vm_region

};
// would rather this be a mm struct
void *mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
           int fd, size_t offset);
struct file;
void *do_mmap(struct proc *p, uintptr_t addr, size_t len, int prot, int flags,
             struct file* f, size_t offset);
int mprotect(struct proc* p, void* addr, size_t len, int prot);
int munmap(struct proc* p, void* addr, size_t len);

int handle_page_fault(struct proc* p, uintptr_t va, int prot);

#endif // !ROS_KERN_MM_H
