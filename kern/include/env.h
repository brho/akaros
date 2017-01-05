/* See COPYRIGHT for copyright information. */

#pragma once
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
#include <devalarm.h>
#include <ns.h>
#include <arch/vmm/vmm.h>

TAILQ_HEAD(vcore_tailq, vcore);
/* 'struct proc_list' declared in sched.h (not ideal...) */

struct username {
	char name[128];
	spinlock_t name_lock;
};
void __set_username(struct username *u, char *name);
void set_username(struct username *u, char *name);

#define PROC_PROGNAME_SZ 20
// TODO: clean this up.
struct proc {
	TAILQ_ENTRY(proc) proc_arsc_link;
	TAILQ_ENTRY(proc) sibling_link;
	spinlock_t proc_lock;
	struct user_context scp_ctx; 	/* context for an SCP.  TODO: move to vc0 */
	struct username user;

	/* This is effectively a (potentially short) version of argv[0].
	 */
	char progname[PROC_PROGNAME_SZ];

	/* This is the full path of the binary which the current proc structure
	 * is tracking.
	 */
	char *binary_path;

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
	/* Lists of vcores */
	struct vcore_tailq online_vcs;
	struct vcore_tailq bulk_preempted_vcs;
	struct vcore_tailq inactive_vcs;
	/* Scheduler mgmt (info, data, whatever) */
	struct sched_proc_data ksched_data;

	/* The args_base pointer is a user pointer which points to the base of
	 * the executable boot block (where args, environment, aux vectors, ...)
	 * are stored.
	 */
	void *args_base;

	// Address space
	pgdir_t env_pgdir;			// Kernel virtual address of page dir
	physaddr_t env_cr3;			// Physical address of page dir
	spinlock_t vmr_lock;		/* Protects VMR tree (mem mgmt) */
	spinlock_t pte_lock;		/* Protects page tables (mem mgmt) */
	struct vmr_tailq vm_regions;
	int vmr_history;

	// Per process info and data pages
 	procinfo_t *procinfo;       // KVA of per-process shared info table (RO)
	procdata_t *procdata;       // KVA of per-process shared data table (RW)

	// The backring pointers for processing asynchronous system calls from the user
	// Note this is the actual backring, not a pointer to it somewhere else
	syscall_back_ring_t syscallbackring;

	// The front ring pointers for pushing asynchronous system events out to the user
	// Note this is the actual frontring, not a pointer to it somewhere else
	sysevent_front_ring_t syseventfrontring;

	/* Filesystem info */
	struct namespace			*ns;
	struct fs_struct			fs_env;
	struct fd_table				open_files;
	struct pgrp					*pgrp;
	struct chan					*slash;
	struct chan					*dot;


	/* UCQ hashlocks */
	struct hashlock				*ucq_hashlock;
	struct small_hashlock		ucq_hl_noref;	/* don't reference directly */
	/* For devalarm */
	struct proc_alarm_set		alarmset;
	struct cv_lookup_tailq		abortable_sleepers;
	spinlock_t					abort_list_lock;

	/* VMMCP */
	struct vmm vmm;

	struct strace				*strace;
	bool						strace_on;
	bool						strace_inherit;
};

/* Til we remove all Env references */
#define Env proc
typedef struct proc env_t;

/* Process Flags */
#define PROC_TRANSITION_TO_M	(1 << 0)
#define PROC_TRACED				(1 << 1)

extern atomic_t num_envs;		// Number of envs

int		env_setup_vm(env_t *e);
void	env_user_mem_free(env_t* e, void* start, size_t len);
void	env_pagetable_free(env_t* e);

typedef int (*mem_walk_callback_t)(env_t* e, pte_t pte, void* va, void* arg);
int		env_user_mem_walk(env_t* e, void* start, size_t len, mem_walk_callback_t callback, void* arg);

static inline void set_traced_proc(struct proc *p, bool traced)
{
	if (traced)
		p->env_flags |= PROC_TRACED;
	else
		p->env_flags &= ~PROC_TRACED;
}

static inline bool is_traced_proc(const struct proc *p)
{
	return (p->env_flags & PROC_TRACED) != 0;
}
