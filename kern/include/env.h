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
#include <arch/trap.h>
#include <ros/common.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <atomic.h>
#include <mm.h>
#include <vfs.h>

// TODO: clean this up.
struct proc {
	TAILQ_ENTRY(proc) proc_link NOINIT;	// Free list link pointers
	TAILQ_ENTRY(proc) proc_arsc_link NOINIT; // Free list link pointers for the arsc list
	spinlock_t proc_lock;
	trapframe_t env_tf; 						// Saved registers
	ancillary_state_t env_ancillary_state; 	// State saved when descheduled
	pid_t pid;
	pid_t ppid;                 // Parent's PID
	pid_t exitcode;				// exit() param or main() return value
	uint32_t state;				// Status of the process
	struct kref p_kref;		/* Refcnt */
	uint32_t env_flags;
	uint32_t env_entry;

	/* Cache color map: bitmap of the cache colors currently allocated to this
	 * process */
	uint8_t* cache_colors_map;
	size_t next_cache_color;

	/* Info about this process's resources (granted, desired) for each type. */
	struct resource resources[MAX_NUM_RESOURCES];

	/* Keeps track of this process's current memory allocation 
     * (i.e. its heap pointer) */
	void* heap_top;

	// Address space
	pde_t *COUNT(NPDENTRIES) env_pgdir;			// Kernel virtual address of page dir
	physaddr_t env_cr3;			// Physical address of page dir
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
	struct files_struct			open_files;
};

/* Til we remove all Env references */
#define Env proc
typedef struct proc env_t;

/* Process Flags */
#define PROC_TRANSITION_TO_M			0x0001

extern atomic_t num_envs;		// Number of envs

int		env_setup_vm(env_t *e);
void	env_push_ancillary_state(env_t* e);
void	env_pop_ancillary_state(env_t* e);
void	env_user_mem_free(env_t* e, void* start, size_t len);
void	env_pagetable_free(env_t* e);

typedef int (*mem_walk_callback_t)(env_t* e, pte_t* pte, void* va, void* arg);
int		env_user_mem_walk(env_t* e, void* start, size_t len, mem_walk_callback_t callback, void* arg);

// The following three functions do not return
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));

#endif // !ROS_KERN_ENV_H
