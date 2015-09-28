/* Copyright (c) 2009, 2012, 2015 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <env.h>
#include <schedule.h>

/* Provision a core to proc p. This code assumes that the scheduler that uses
 * it holds a lock for the duration of the call. */
void __provision_core(struct proc *p, struct sched_pcore *spc)
{
	struct sched_pcore_tailq *prov_list;
	/* If the core is already prov to someone else, take it away.  (last write
	 * wins, some other layer or new func can handle permissions). */
	if (spc->prov_proc) {
		/* the list the spc is on depends on whether it is alloced to the
		 * prov_proc or not */
		prov_list = (spc->alloc_proc == spc->prov_proc ?
		             &spc->prov_proc->ksched_data.crd.prov_alloc_me :
		             &spc->prov_proc->ksched_data.crd.prov_not_alloc_me);
		TAILQ_REMOVE(prov_list, spc, prov_next);
	}
	/* Now prov it to p.  Again, the list it goes on depends on whether it is
	 * alloced to p or not.  Callers can also send in 0 to de-provision. */
	if (p) {
		if (spc->alloc_proc == p) {
			TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_alloc_me, spc,
			                  prov_next);
		} else {
			/* this is be the victim list, which can be sorted so that we pick
			 * the right victim (sort by alloc_proc reverse priority, etc). */
			TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_not_alloc_me, spc,
			                  prov_next);
		}
	}
	spc->prov_proc = p;
}

