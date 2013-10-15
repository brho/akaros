/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H
/* Note that the old include/ros/env.h is merged into this file */

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/sysevent.h>
#include <ros/procinfo.h>
#include <error.h>
#include <ros/procdata.h>
#include <ros/procinfo.h>
#include <ros/resource.h>
#include <trap.h>
#include <ros/common.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <atomic.h>
#include <mm.h>
#include <vfs.h>
#include <schedule.h>
#include <plan9.h>

TAILQ_HEAD(vcore_tailq, vcore);
/* 'struct proc_list' declared in sched.h (not ideal...) */

// TODO: clean this up.
struct proc {
	TAILQ_ENTRY(proc) proc_arsc_link;
	TAILQ_ENTRY(proc) sibling_link;
	spinlock_t proc_lock;
	struct user_context scp_ctx; 	/* context for an SCP.  TODO: move to vc0 */
	pid_t pid;
	/* Tempting to add a struct proc *parent, but we'd need to protect the use
	 * of that reference from concurrent parent-death (letting init inherit
	 * children, etc), which is basically what we do when we do pid2proc.  If we
	 * do add *parent, it'll be a weak ref, and you'll need to lock the child to
	 * kref_get or to remove the pointer. */
	pid_t ppid;					/* parent's pid, not a reference */
	struct proc_list children;	/* protected by the proc lock for now */
	int exitcode;				/* exit() param or main() return value */
	struct cond_var child_wait;	/* signal for dying or o/w waitable child */
	uint32_t state;				// Status of the process
	struct kref p_kref;		/* Refcnt */
	uint32_t env_flags;
	uintptr_t env_entry;
	/* Lists of vcores */
	struct vcore_tailq online_vcs;
	struct vcore_tailq bulk_preempted_vcs;
	struct vcore_tailq inactive_vcs;
	/* Scheduler mgmt (info, data, whatever) */
	struct sched_proc_data ksched_data;

	/* Cache color map: bitmap of the cache colors currently allocated to this
	 * process */
	uint8_t* cache_colors_map;
	size_t next_cache_color;

	/* Keeps track of this process's current memory allocation 
     * (i.e. its heap pointer) */
	void* heap_top;

	// Address space
	pde_t *COUNT(NPDENTRIES) env_pgdir;			// Kernel virtual address of page dir
	physaddr_t env_cr3;			// Physical address of page dir
	spinlock_t mm_lock;		/* Protects page tables and VMRs (mem mgmt) */
	struct vmr_tailq vm_regions;

	// Per process info and data pages
 	procinfo_t *SAFE procinfo;       // KVA of per-process shared info table (RO)
	procdata_t *SAFE procdata;       // KVA of per-process shared data table (RW)
	
	// The backring pointers for processing asynchronous system calls from the user
	// Note this is the actual backring, not a pointer to it somewhere else
	syscall_back_ring_t syscallbackring;
	
	// The front ring pointers for pushing asynchronous system events out to the user
	// Note this is the actual frontring, not a pointer to it somewhere else
	sysevent_front_ring_t syseventfrontring;

	/* Filesystem info */
	struct namespace			*ns;
	struct fs_struct			fs_env;
	/* TODO: Oh shit, this needs init'd, with plan9fd == -1 */
	struct files_struct			open_files;

	/* Plan 9 namespace. Evil plan: replace three things above. */
	struct chan *slash;
    struct chan *dot;
	struct pgrp *pgrp;
    struct fgrp *fgrp, *closingfgrp;
    char user[32]; /* hey! let's do user NAMES! I AM NOT A NUMBER! */
	/* UCQ hashlocks */
	struct hashlock				*ucq_hashlock;
	struct small_hashlock		ucq_hl_noref;	/* don't reference directly */
};

/* Til we remove all Env references */
#define Env proc
typedef struct proc env_t;

/* Process Flags */
#define PROC_TRANSITION_TO_M			0x0001

extern atomic_t num_envs;		// Number of envs

int		env_setup_vm(env_t *e);
void	env_user_mem_free(env_t* e, void* start, size_t len);
void	env_pagetable_free(env_t* e);

typedef int (*mem_walk_callback_t)(env_t* e, pte_t* pte, void* va, void* arg);
int		env_user_mem_walk(env_t* e, void* start, size_t len, mem_walk_callback_t callback, void* arg);

#endif // !ROS_KERN_ENV_H
