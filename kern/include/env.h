/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_ENV_H
#define ROS_KERN_ENV_H
/* Note that the old include/ros/env.h is merged into this file */

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/sysevent.h>
#include <ros/error.h>
#include <ros/procdata.h>
#include <ros/resource.h>
#include <arch/trap.h>
#include <ros/common.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <atomic.h>

struct Env;
typedef struct Env env_t;

// TODO: clean this up.
struct Env {
	TAILQ_ENTRY(Env) proc_link NOINIT;	// Free list link pointers
	spinlock_t proc_lock;
	trapframe_t env_tf; 						// Saved registers
	ancillary_state_t env_ancillary_state; 	// State saved when descheduled
	pid_t pid;
	pid_t ppid;                 // Parent's PID
	pid_t exitcode;				// exit() param or main() return value
	uint32_t state;				// Status of the process
	uint32_t env_refcnt;		// Reference count of kernel contexts using this
	uint32_t env_flags;
	uint32_t env_entry;
	/* Virtual coremap: each index is the virtual core id, the contents at that
	 * index is the physical core_id() corresponding to the vcore.  -1 means it
	 * is unused */
	int32_t vcoremap[MAX_NUM_CPUS];
	uint32_t num_vcores;

	/* Cache color map: bitmap of the cache colors currently allocated to this
	 * process */
	uint8_t* cache_colors_map;
	size_t next_cache_color;

	/* Info about this process's resources (granted, desired) for each type. */
	struct resource resources[MAX_NUM_RESOURCES];

	/* Keeps track of this process's current memory allocation 
     * (i.e. its heap pointer) */
	void* heap_bottom;
	void* heap_top;

	// Address space
	pde_t *COUNT(NPDENTRIES) env_pgdir;			// Kernel virtual address of page dir
	physaddr_t env_cr3;			// Physical address of page dir
//	struct memregion_list memregions;

	// Per process info and data pages
 	procinfo_t *SAFE env_procinfo;       // KVA of per-process shared info table (RO)
	procdata_t *SAFE env_procdata;       // KVA of per-process shared data table (RW)
	
	// The backring pointers for processing asynchronous system calls from the user
	// Note this is the actual backring, not a pointer to it somewhere else
	syscall_back_ring_t syscallbackring;
	
	// The front ring pointers for pushing asynchronous system events out to the user
	// Note this is the actual frontring, not a pointer to it somewhere else
	sysevent_front_ring_t syseventfrontring;
};

/* Process Flags */
#define PROC_TRANSITION_TO_M			0x0001

extern atomic_t num_envs;		// Number of envs

int		env_setup_vm(env_t *e);
void	env_push_ancillary_state(env_t* e);
void	env_pop_ancillary_state(env_t* e);
void	env_user_mem_free(env_t* e, void* start, size_t len);
void	env_pagetable_free(env_t* e);
void	env_segment_alloc(env_t *e, void *SNT va, size_t len);
void	env_segment_free(env_t *e, void *SNT va, size_t len);
void	env_load_icode(env_t* e, env_t* binary_env, uint8_t *COUNT(size) binary, size_t size);

typedef int (*mem_walk_callback_t)(env_t* e, pte_t* pte, void* va, void* arg);
int		env_user_mem_walk(env_t* e, void* start, size_t len, mem_walk_callback_t callback, void* arg);

// The following three functions do not return
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));


/* Helper handler for smp_call to dispatch jobs to other cores */
#ifdef __IVY__
void run_env_handler(trapframe_t *tf, env_t * data);
#else
void run_env_handler(trapframe_t *tf, void * data);
#endif

#endif // !ROS_KERN_ENV_H
