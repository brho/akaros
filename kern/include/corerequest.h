/* Copyright (c) 2009, 2012, 2015 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#pragma once

#include <stdbool.h>
#include <arch/topology.h>
#if defined(CONFIG_COREALLOC_FCFS)
  #include <corealloc_fcfs.h>
#elif defined(CONFIG_COREALLOC_PACKED)
  #include <corealloc_packed.h>
#endif

/* Initialize any data assocaited with doing core allocation. */
void corealloc_init(void);

/* Initialize any data associated with allocating cores to a process. */
void corealloc_proc_init(struct proc *p);

/* Find the best core to allocate to a process as dictated by the core
 * allocation algorithm. If no core is found, return -1. This code assumes
 * that the scheduler that uses it holds a lock for the duration of the call.
 * */
uint32_t __find_best_core_to_alloc(struct proc *p);

/* Track the pcore properly when it is allocated to p. This code assumes that
 * the scheduler that uses it holds a lock for the duration of the call. */
void __track_core_alloc(struct proc *p, uint32_t pcoreid);

/* Track the pcore properly when it is deallocated from p. This code assumes
 * that the scheduler that uses it holds a lock for the duration of the call.
 * */
void __track_core_dealloc(struct proc *p, uint32_t pcoreid);

/* Bulk interface for __track_core_dealloc */
void __track_core_dealloc_bulk(struct proc *p, uint32_t *pc_arr,
                               uint32_t nr_cores);

/* One off functions to make 'pcoreid' the next core chosen by the core
 * allocation algorithm (so long as no provisioned cores are still idle), and
 * to sort the idle core list for debugging. This code assumes that the
 * scheduler that uses it holds a lock for the duration of the call. */
void __next_core_to_alloc(uint32_t pcoreid);
void __sort_idle_cores(void);

/* Provision a core to proc p. This code assumes that the scheduler that uses
 * it holds a lock for the duration of the call. */
void __provision_core(struct proc *p, uint32_t pcoreid);

/* Unprovision all cores from proc p. This code assumes that the scheduler
 * that uses * it holds a lock for the duration of the call. */
void __unprovision_all_cores(struct proc *p);

/* Print the map of idle cores that are still allocatable through our core
 * allocation algorithm. */
void print_idle_core_map(void);

/* Print a list of the cores currently provisioned to p. */
void print_proc_coreprov(struct proc *p);

/* Print the processes attached to each provisioned core. */
void print_coreprov_map(void);

static inline struct proc *get_alloc_proc(uint32_t pcoreid)
{
	extern struct sched_pcore *all_pcores;

	return all_pcores[pcoreid].alloc_proc;
}

static inline struct proc *get_prov_proc(uint32_t pcoreid)
{
	extern struct sched_pcore *all_pcores;

	return all_pcores[pcoreid].prov_proc;
}

/* TODO: need more thorough CG/LL management.  For now, core0 is the only LL
 * core.  This won't play well with the ghetto shit in schedule_init() if you do
 * anything like 'DEDICATED_MONITOR' or the ARSC server.  All that needs an
 * overhaul. */
static inline bool is_ll_core(uint32_t pcoreid)
{
	if (pcoreid == 0)
		return TRUE;
	return FALSE;
}

/* Normally it'll be the max number of CG cores ever */
static inline uint32_t max_vcores(struct proc *p)
{
/* TODO: (CG/LL) */
#ifdef CONFIG_DISABLE_SMT
	return num_cores >> 1;
#else
	return num_cores - 1;	/* reserving core 0 */
#endif /* CONFIG_DISABLE_SMT */
}
