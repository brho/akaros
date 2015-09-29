/* Copyright (c) 2009, 2012, 2015 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#pragma once

/* The core request algorithm maintains an internal array of these: the
 * global pcore map. Note the prov_proc and alloc_proc are weak (internal)
 * references, and should only be used as a ref source while the ksched has a
 * valid kref. */
struct sched_pcore {
	TAILQ_ENTRY(sched_pcore)   prov_next;    /* on a proc's prov list */
	TAILQ_ENTRY(sched_pcore)   alloc_next;   /* on an alloc list (idle)*/
	struct proc                *prov_proc;   /* who this is prov to */
	struct proc                *alloc_proc;  /* who this is alloc to */
};
TAILQ_HEAD(sched_pcore_tailq, sched_pcore);

struct core_request_data {
	struct sched_pcore_tailq  prov_alloc_me;      /* prov cores alloced us */
	struct sched_pcore_tailq  prov_not_alloc_me;  /* maybe alloc to others */
};

/* Initialize any data assocaited with doing core allocation. */
void corealloc_init(void);

/* Initialize any data associated with provisiong cores to a process. */
void coreprov_proc_init(struct proc *p);

/* Find the best core to allocate to a process as dictated by the core
 * allocation algorithm. This code assumes that the scheduler that uses it
 * holds a lock for the duration of the call. */
struct sched_pcore *__find_best_core_to_alloc(struct proc *p);

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

/* Get/Put an idle core from our pcore list and return its core_id. Don't
 * consider the chosen core in the future when handing out cores to a
 * process. This code assumes that the scheduler that uses it holds a lock
 * for the duration of the call. This will not give out provisioned cores.
 * The gets return the coreid on success, -1 or -error on failure. */
int __get_any_idle_core(void);
int __get_specific_idle_core(int coreid);
void __put_idle_core(int coreid);

/* Provision a core to proc p. This code assumes that the scheduler that uses
 * it holds a lock for the duration of the call. */
void __provision_core(struct proc *p, struct sched_pcore *spc);

/* Unprovision all cores from proc p. This code assumes that the scheduler
 * that uses * it holds a lock for the duration of the call. */
void __unprovision_all_cores(struct proc *p);

/* Print a list of the cores currently provisioned to p. */
void print_proc_coreprov(struct proc *p);

/* Print the processes attached to each provisioned core. */
void print_coreprov_map(void);

static inline uint32_t spc2pcoreid(struct sched_pcore *spc)
{
	extern struct sched_pcore *all_pcores;

	return spc - all_pcores;
}

static inline struct sched_pcore *pcoreid2spc(uint32_t pcoreid)
{
	extern struct sched_pcore *all_pcores;

	return &all_pcores[pcoreid];
}

static inline struct proc *get_alloc_proc(struct sched_pcore *c)
{
	return c->alloc_proc;
}

static inline struct proc *get_prov_proc(struct sched_pcore *c)
{
	return c->prov_proc;
}
