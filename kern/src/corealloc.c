/* Copyright (c) 2009, 2012, 2015 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/topology.h>
#include <sys/queue.h>
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
