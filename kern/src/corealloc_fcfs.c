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
	for (int i = 0; i < num_cores; i++) {
		if (is_ll_core(i))
			continue;
#ifdef CONFIG_DISABLE_SMT
		/* Remove all odd cores from consideration for allocation. */
		if (i % 2 == 1)
			continue;
#endif /* CONFIG_DISABLE_SMT */
		TAILQ_INSERT_TAIL(&idlecores, pcoreid2spc(i), alloc_next);
	}
}

/* Initialize any data associated with allocating cores to a process. */
void corealloc_proc_init(struct proc *p)
{
	TAILQ_INIT(&p->ksched_data.crd.prov_alloc_me);
	TAILQ_INIT(&p->ksched_data.crd.prov_not_alloc_me);
}

/* Find the best core to allocate to a process as dictated by the core
 * allocation algorithm. This code assumes that the scheduler that uses it
 * holds a lock for the duration of the call. */
uint32_t __find_best_core_to_alloc(struct proc *p)
{
	struct sched_pcore *spc_i = NULL;

	spc_i = TAILQ_FIRST(&p->ksched_data.crd.prov_not_alloc_me);
	if (!spc_i)
		spc_i = TAILQ_FIRST(&idlecores);
	if (!spc_i)
		return -1;
	return spc2pcoreid(spc_i);
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

/* One off function to make 'pcoreid' the next core chosen by the core
 * allocation algorithm (so long as no provisioned cores are still idle).
 * This code assumes that the scheduler that uses it holds a lock for the
 * duration of the call. */
void __next_core_to_alloc(uint32_t pcoreid)
{
	struct sched_pcore *spc_i;
	bool match = FALSE;

	TAILQ_FOREACH(spc_i, &idlecores, alloc_next) {
		if (spc2pcoreid(spc_i) == pcoreid) {
			match = TRUE;
			break;
		}
	}
	if (match) {
		TAILQ_REMOVE(&idlecores, spc_i, alloc_next);
		TAILQ_INSERT_HEAD(&idlecores, spc_i, alloc_next);
		printk("Pcore %d will be given out next (from the idles)\n", pcoreid);
	}
}

/* One off function to sort the idle core list for debugging in the kernel
 * monitor. This code assumes that the scheduler that uses it holds a lock
 * for the duration of the call. */
void __sort_idle_cores(void)
{
	struct sched_pcore *spc_i, *spc_j, *temp;
	struct sched_pcore_tailq sorter = TAILQ_HEAD_INITIALIZER(sorter);
	bool added;

	TAILQ_CONCAT(&sorter, &idlecores, alloc_next);
	TAILQ_FOREACH_SAFE(spc_i, &sorter, alloc_next, temp) {
		TAILQ_REMOVE(&sorter, spc_i, alloc_next);
		added = FALSE;
		/* don't need foreach_safe since we break after we muck with the list */
		TAILQ_FOREACH(spc_j, &idlecores, alloc_next) {
			if (spc_i < spc_j) {
				TAILQ_INSERT_BEFORE(spc_j, spc_i, alloc_next);
				added = TRUE;
				break;
			}
		}
		if (!added)
			TAILQ_INSERT_TAIL(&idlecores, spc_i, alloc_next);
	}
}

/* Print the map of idle cores that are still allocatable through our core
 * allocation algorithm. */
void print_idle_core_map(void)
{
	struct sched_pcore *spc_i;
	/* not locking, so we can look at this without deadlocking. */
	printk("Idle cores (unlocked!):\n");
	TAILQ_FOREACH(spc_i, &idlecores, alloc_next)
		printk("Core %d, prov to %d (%p)\n", spc2pcoreid(spc_i),
		       spc_i->prov_proc ? spc_i->prov_proc->pid : 0, spc_i->prov_proc);
}
