/* Copyright (c) 2009, 2012, 2015 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/topology.h>
#include <sys/queue.h>
#include <env.h>
#include <corerequest.h>
#include <kmalloc.h>

/* The pcores in the system. (array gets alloced in init()).  */
struct sched_pcore *all_pcores;

/* TAILQ of all unallocated, idle (CG) cores */
struct sched_pcore_tailq idlecores = TAILQ_HEAD_INITIALIZER(idlecores);

/* Initialize any data assocaited with doing core allocation. */
void corealloc_init(void)
{
	/* Allocate all of our pcores. */
	all_pcores = kzmalloc(sizeof(struct sched_pcore) * num_cores, 0);
	/* init the idlecore list.  if they turned off hyperthreading, give them the
	 * odds from 1..max-1.  otherwise, give them everything by 0 (default mgmt
	 * core).  TODO: (CG/LL) better LL/CG mgmt */
#ifndef CONFIG_DISABLE_SMT
	for (int i = 1; i < num_cores; i++)
		TAILQ_INSERT_TAIL(&idlecores, pcoreid2spc(i), alloc_next);
#else
	assert(!(num_cores % 2));
	for (int i = 1; i < num_cores; i += 2)
		TAILQ_INSERT_TAIL(&idlecores, pcoreid2spc(i), alloc_next);
#endif /* CONFIG_DISABLE_SMT */
}

/* Track the pcore properly when it is allocated to p. This code assumes that
 * the scheduler that uses it holds a lock for the duration of the call. */
void __track_core_alloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;

	assert(pcoreid < num_cores);	/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	assert(spc->alloc_proc != p);	/* corruption or double-alloc */
	spc->alloc_proc = p;
	/* if the pcore is prov to them and now allocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.crd.prov_not_alloc_me, spc, prov_next);
		TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_alloc_me, spc, prov_next);
	}
	/* Actually allocate the core, removing it from the idle core list. */
	TAILQ_REMOVE(&idlecores, spc, alloc_next);
}

/* Track the pcore properly when it is deallocated from p. This code assumes
 * that the scheduler that uses it holds a lock for the duration of the call.
 * */
void __track_core_dealloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;

	assert(pcoreid < num_cores);	/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	spc->alloc_proc = 0;
	/* if the pcore is prov to them and now deallocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.crd.prov_alloc_me, spc, prov_next);
		/* this is the victim list, which can be sorted so that we pick the
		 * right victim (sort by alloc_proc reverse priority, etc).  In this
		 * case, the core isn't alloc'd by anyone, so it should be the first
		 * victim. */
		TAILQ_INSERT_HEAD(&p->ksched_data.crd.prov_not_alloc_me, spc,
		                  prov_next);
	}
	/* Actually dealloc the core, putting it back on the idle core list. */
	TAILQ_INSERT_TAIL(&idlecores, spc, alloc_next);
}

/* Bulk interface for __track_core_dealloc */
void __track_core_dealloc_bulk(struct proc *p, uint32_t *pc_arr,
                               uint32_t nr_cores)
{
	for (int i = 0; i < nr_cores; i++)
		__track_core_dealloc(p, pc_arr[i]);
}
