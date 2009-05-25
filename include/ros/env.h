/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ENV_H
#define ROS_INC_ENV_H

#include <arch/types.h>
#include <ros/queue.h>
#include <ros/trap.h>
#include <ros/memlayout.h>
#include <ros/syscall.h>

struct Env;
typedef struct Env env_t;

typedef int32_t envid_t;

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

#define LOG2NENV		10
#define NENV			(1 << LOG2NENV)
#define ENVX(envid)		((envid) & (NENV - 1))

// Values of env_status in struct Env
#define ENV_FREE			0
#define ENV_RUNNING			1
#define ENV_RUNNABLE		2
#define ENV_NOT_RUNNABLE	3
#define ENV_DYING			4

struct Env {
	uint32_t lock;
	trapframe_t env_tf;			// Saved registers
	LIST_ENTRY(env_t) env_link;	// Free list link pointers
	envid_t env_id;				// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;			// Number of times environment has run
	uint32_t env_refcnt;		// Reference count of kernel contexts using this
	// Note this is the actual backring, not a pointer to it somewhere else
	syscall_back_ring_t env_sysbackring;	// BackRing for generic syscalls

	// Address space
	pde_t *env_pgdir;			// Kernel virtual address of page dir
	physaddr_t env_cr3;			// Physical address of page dir
	// TODO - give these two proper types (pointers to structs)
	void* env_procinfo; 		// KVA of per-process shared info table (RO)
	void* env_procdata;  		// KVA of per-process shared data table (RW)
	// Eventually want to move this to a per-system shared-info page
	uint64_t env_tscfreq;		// Frequency of the TSC for measurements
};

#endif // !ROS_INC_ENV_H
