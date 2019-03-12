/* See COPYRIGHT for copyright information. */

#pragma once

#include <ros/memlayout.h>
#include <ros/common.h>
#include <ros/resource.h>
#include <ros/atomic.h>
#include <ros/arch/arch.h>
#include <ros/cpu_feat.h>
#include <string.h>

/* Process creation flags */
#define PROC_DUP_FGRP			1

#define PROCINFO_MAX_ARGP 32
#define PROCINFO_ARGBUF_SIZE 3072

#ifdef ROS_KERNEL
#include <sys/queue.h>
#endif /* ROS_KERNEL */

/* Not necessary to expose all of this, but it doesn't hurt, and is convenient
 * for the kernel.  Need to do some acrobatics for the TAILQ_ENTRY. */
struct vcore;
struct vcore {
#ifdef ROS_KERNEL
	TAILQ_ENTRY(vcore)		list;
#else /* userspace */
	void				*dummy_ptr1;
	void				*dummy_ptr2;
#endif /* ROS_KERNEL */
	uint32_t			pcoreid;
	bool				valid;
	/* these two differ when a preempt is in flight. */
	uint32_t			nr_preempts_sent;
	uint32_t			nr_preempts_done;
	uint64_t			preempt_pending;
	/* A process can see cumulative runtime as of the last resume, and can
	 * also calculate runtime in this interval, by adding (ns - resume) +
	 * total. */
	uint64_t			resume_ticks;	/* TSC at resume time */
	/* ticks up to last offlining */
	uint64_t			total_ticks;
};

struct pcore {
	uint32_t			vcoreid;
	bool 				valid;
};

typedef struct procinfo {
	pid_t pid;
	pid_t ppid;
	size_t max_vcores;	/* TODO: change to a uint32_t */
	uint64_t tsc_freq;
	uint64_t timing_overhead;
	uintptr_t program_end;
	/* glibc relies on stuff above this point.  if you change it, you need
	 * to rebuild glibc. */
	bool is_mcp;			/* is in multi mode */
	unsigned long 			res_grant[MAX_NUM_RESOURCES];
	struct vcore			vcoremap[MAX_NUM_CORES];
	uint32_t			num_vcores;
	struct pcore			pcoremap[MAX_NUM_CORES];
	seq_ctr_t			coremap_seqctr;
} procinfo_t;
#define PROCINFO_NUM_PAGES  ((sizeof(procinfo_t)-1)/PGSIZE + 1)

/* We align this so that the kernel can easily allocate it in the BSS */
struct proc_global_info {
	unsigned long cpu_feats[__NR_CPU_FEAT_BITS];
	uint64_t x86_default_xcr0;
	uint64_t tsc_freq;
	uint64_t tsc_overhead;
	uint64_t bus_freq;
	uint64_t walltime_ns_last;
	uint64_t tsc_cycles_last;
} __attribute__((aligned(PGSIZE)));
#define PROCGINFO_NUM_PAGES  (sizeof(struct proc_global_info) / PGSIZE)

#ifdef ROS_KERNEL

/* defined in init.c */
extern struct proc_global_info __proc_global_info;

#else /* Userland */

#define __procinfo (*(procinfo_t*)UINFO)
#define __proc_global_info (*(struct proc_global_info*)UGINFO)

#include <ros/common.h>
#include <ros/atomic.h>
#include <ros/syscall.h>

/* Figure out what your vcoreid is from your pcoreid and procinfo.  Only low
 * level or debugging code should call this. */
static inline uint32_t __get_vcoreid_from_procinfo(void)
{
	/* The assumption is that any IPIs/KMSGs would knock userspace into the
	 * kernel before it could read the closing of the seqctr.  Put another
	 * way, there is a 'memory barrier' between the IPI write and the seqctr
	 * write.  I think this is true. */
	uint32_t kpcoreid, kvcoreid;
	extern long __ros_syscall_noerrno(unsigned int _num, long _a0, long _a1,
	                                  long _a2, long _a3, long _a4,
					  long _a5);

	seq_ctr_t old_seq;
	do {
		cmb();
		old_seq = __procinfo.coremap_seqctr;
		kpcoreid = __ros_syscall_noerrno(SYS_getpcoreid, 0, 0, 0, 0, 0,
						 0);
		if (!__procinfo.pcoremap[kpcoreid].valid)
			continue;
		kvcoreid = __procinfo.pcoremap[kpcoreid].vcoreid;
	} while (seqctr_retry(old_seq, __procinfo.coremap_seqctr));
	return kvcoreid;
}

static inline uint32_t __get_vcoreid(void)
{
	/* since sys_getvcoreid could lie (and might never change) */
	return __get_vcoreid_from_procinfo();
}

#endif /* ifndef ROS_KERNEL */
