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

struct Env;
typedef struct Env env_t;

// An environment ID 'envid_t' has three parts:
//
// +1+---------------21-----------------+--------10--------+
// |0|          Uniqueifier             |   Environment    |
// | |                                  |      Index       |
// +------------------------------------+------------------+
//                                       \--- ENVX(eid) --/
//
// The environment index ENVX(eid) equals the environment's offset in the
// 'envs[]' array.  The uniqueifier distinguishes environments that were
// created at different times, but share the same environment index.
//
// All real environments are greater than 0 (so the sign bit is zero).
// envid_ts less than 0 signify errors.  The envid_t == 0 is special, and
// stands for the current environment.

typedef int32_t envid_t;

#define LOG2NENV		10
#define NENV			(1 << LOG2NENV)
#define ENVX(envid)		((envid) & (NENV - 1))

// TODO: clean this up.
struct Env {
	TAILQ_ENTRY(Env) proc_link NOINIT;	// Free list link pointers
	spinlock_t proc_lock;
	trapframe_t env_tf; 						// Saved registers
	ancillary_state_t env_ancillary_state; 	// State saved when descheduled
	envid_t env_id;				// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	uint32_t state;				// Status of the process
	uint32_t env_runs;			// Number of times environment has run
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
	void* end_text_segment;
	void* end_data_segment;

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
// None yet

extern env_t *CT(NENV) RO envs;		// All environments
extern atomic_t num_envs;		// Number of envs
// TODO: consider moving this to struct per_cpu_info
extern env_t * (RO curenvs)[MAX_NUM_CPUS];

static inline env_t *
get_cpu_curenv() TRUSTED
{
	return curenvs[core_id()];
}

static inline void
set_cpu_curenv(env_t *p) TRUSTED
{
	curenvs[core_id()] = p;
}

void	env_init(void);
int		env_alloc(env_t *SAFE*SAFE e, envid_t parent_id);
void	env_push_ancillary_state(env_t* e);
void	env_pop_ancillary_state(env_t* e);
void	env_free(env_t *SAFE e);
void	env_user_mem_free(env_t* e);
void	env_segment_alloc(env_t *e, void *SNT va, size_t len);
void	env_segment_free(env_t *e, void *SNT va, size_t len);
env_t*	env_create(uint8_t *COUNT(size) binary, size_t size);

/*
 * Allows the kernel to figure out what process is running on its core.
 * Can be used just like a pointer to a struct process.
 */
#define current (get_cpu_curenv())
//#define current (curenvs[core_id()])

int	envid2env(envid_t envid, env_t **env_store, bool checkperm);
// The following three functions do not return
void	env_pop_tf(trapframe_t *tf) __attribute__((noreturn));


/* Helper handler for smp_call to dispatch jobs to other cores */
#ifdef __IVY__
void run_env_handler(trapframe_t *tf, env_t * data);
#else
void run_env_handler(trapframe_t *tf, void * data);
#endif

#endif // !ROS_KERN_ENV_H
