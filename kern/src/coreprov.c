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
void __provision_core(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc = pcoreid2spc(pcoreid);
	struct sched_pcore_tailq *prov_list;

	/* If the core is already prov to someone else, take it away.  (last
	 * write wins, some other layer or new func can handle permissions). */
	if (spc->prov_proc) {
		/* the list the spc is on depends on whether it is alloced to
		 * the prov_proc or not */
		prov_list = spc->alloc_proc == spc->prov_proc ?
		            &spc->prov_proc->ksched_data.crd.prov_alloc_me :
		            &spc->prov_proc->ksched_data.crd.prov_not_alloc_me;
		TAILQ_REMOVE(prov_list, spc, prov_next);
	}
	/* Now prov it to p.  Again, the list it goes on depends on whether it
	 * is alloced to p or not.  Callers can also send in 0 to de-provision.
	 * */
	if (p) {
		if (spc->alloc_proc == p) {
			TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_alloc_me, spc,
			                  prov_next);
		} else {
			/* this is be the victim list, which can be sorted so
			 * that we pick the right victim (sort by alloc_proc
			 * reverse priority, etc). */
			TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_not_alloc_me,
					  spc, prov_next);
		}
	}
	spc->prov_proc = p;
}

/* Unprovisions any pcores for the given list */
static void __unprov_pcore_list(struct sched_pcore_tailq *list_head)
{
	struct sched_pcore *spc_i;

	/* We can leave them connected within the tailq, since the scps don't
	 * have a default list (if they aren't on a proc's list, then we don't
	 * care about them), and since the INSERTs don't care what list you were
	 * on before (chummy with the implementation).  Pretty sure this is
	 * right.  If there's suspected list corruption, be safer here. */
	TAILQ_FOREACH(spc_i, list_head, prov_next)
		spc_i->prov_proc = 0;
	TAILQ_INIT(list_head);
}

/* Unprovision all cores from proc p. This code assumes that the scheduler
 * that uses * it holds a lock for the duration of the call. */
void __unprovision_all_cores(struct proc *p)
{
	__unprov_pcore_list(&p->ksched_data.crd.prov_alloc_me);
	__unprov_pcore_list(&p->ksched_data.crd.prov_not_alloc_me);
}

/* Print a list of the cores currently provisioned to p. */
void print_proc_coreprov(struct proc *p)
{
	struct sched_pcore *spc_i;

	if (!p)
		return;
	print_lock();
	printk("Prov cores alloced to proc %d (%p)\n----------\n", p->pid, p);
	TAILQ_FOREACH(spc_i, &p->ksched_data.crd.prov_alloc_me, prov_next)
		printk("Pcore %d\n", spc2pcoreid(spc_i));
	printk("Prov cores not alloced to proc %d (%p)\n----------\n", p->pid,
	       p);
	TAILQ_FOREACH(spc_i, &p->ksched_data.crd.prov_not_alloc_me, prov_next)
		printk("Pcore %d (alloced to %d (%p))\n", spc2pcoreid(spc_i),
		       spc_i->alloc_proc ? spc_i->alloc_proc->pid : 0,
		       spc_i->alloc_proc);
	print_unlock();
}

/* Print the processes attached to each provisioned core. */
void print_coreprov_map(void)
{
	struct sched_pcore *spc_i;

	/* Doing this without a scheduler lock, which is dangerous, but won't
	 * deadlock */
	print_lock();
	printk("Which cores are provisioned to which procs:\n--------------\n");
	for (int i = 0; i < num_cores; i++) {
		spc_i = pcoreid2spc(i);
		printk("Core %02d, prov: %d(%p) alloc: %d(%p)\n", i,
		       spc_i->prov_proc ? spc_i->prov_proc->pid : 0,
		       spc_i->prov_proc,
		       spc_i->alloc_proc ? spc_i->alloc_proc->pid : 0,
		       spc_i->alloc_proc);
	}
	print_unlock();
}
