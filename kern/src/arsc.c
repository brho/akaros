/* See COPYRIGHT for copyright information. */

#include <ros/common.h>
#include <ros/ring_syscall.h>
#include <arch/types.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <error.h>

#include <syscall.h>
#include <kmalloc.h>
#include <pmap.h>
#include <stdio.h>
#include <hashtable.h>
#include <smp.h>
#include <arsc_server.h>
#include <kref.h>



struct proc_list arsc_proc_list = TAILQ_HEAD_INITIALIZER(arsc_proc_list);
spinlock_t arsc_proc_lock = SPINLOCK_INITIALIZER_IRQSAVE;

intreg_t inline syscall_async(struct proc *p, syscall_req_t *call)
{
	struct syscall* sc = call->sc;
	return syscall(p, sc->num, sc->arg0, sc->arg1,
	               sc->arg2, sc->arg3, sc->arg4, sc->arg5);
}

syscall_sring_t* sys_init_arsc(struct proc *p)
{
	kref_get(&p->p_kref, 1);	/* we're storing an external ref here */
	syscall_sring_t* sring;
	void * va;

	// TODO: need to pin this page in the future when swapping happens
	va = do_mmap(p,MMAP_LOWEST_VA, SYSCALLRINGSIZE, PROT_READ | PROT_WRITE,
	             MAP_ANONYMOUS | MAP_POPULATE | MAP_PRIVATE, NULL, 0);
	pte_t pte = pgdir_walk(p->env_pgdir, (void*)va, 0);
	assert(pte_walk_okay(pte));
	sring = (syscall_sring_t*) KADDR(pte_get_paddr(pte));
	/*make sure we are able to allocate the shared ring */
	assert(sring != NULL);
 	p->procdata->syscallring = sring;
	/* Initialize the generic syscall ring buffer */
	SHARED_RING_INIT(sring);

	BACK_RING_INIT(&p->syscallbackring,
	               sring,
	               SYSCALLRINGSIZE);

	spin_lock_irqsave(&arsc_proc_lock);
	TAILQ_INSERT_TAIL(&arsc_proc_list, p, proc_arsc_link);
	spin_unlock_irqsave(&arsc_proc_lock);
	return (syscall_sring_t*)va;
}

void arsc_server(uint32_t srcid, long a0, long a1, long a2)
{
	struct proc *p = NULL;

	TAILQ_INIT(&arsc_proc_list);
	while (1) {
		while (TAILQ_EMPTY(&arsc_proc_list))
			cpu_relax();

		TAILQ_FOREACH(p, &arsc_proc_list, proc_arsc_link) {
			/* Probably want to try to process a dying process's
			 * syscalls.  If not, just move it to an else case */
			process_generic_syscalls (p, MAX_ASRC_BATCH);
			if (proc_is_dying(p)) {
				TAILQ_REMOVE(&arsc_proc_list, p,
					     proc_arsc_link);
				proc_decref(p);
				/* Need to break out, so the TAILQ_FOREACH
				 * doesn't flip out.  It's not fair, but we're
				 * not dealing with that yet anyway */
				break;
			}
		}
	}
}

static intreg_t process_generic_syscalls(struct proc *p, size_t max)
{
	size_t count = 0;
	syscall_back_ring_t* sysbr = &p->syscallbackring;
	struct per_cpu_info* pcpui = &per_cpu_info[core_id()];
	uintptr_t old_proc;

	// looking at a process not initialized to perform arsc.
	if (sysbr == NULL)
		return count;
	/* Bail out if there is nothing to do */
	if (!RING_HAS_UNCONSUMED_REQUESTS(sysbr))
		return 0;
	/* Switch to the address space of the process, so we can handle their
	 * pointers, etc. */
	old_proc = switch_to(p);
	// max is the most we'll process.  max = 0 means do as many as possible
	// TODO: check for initialization of the ring.
	while (RING_HAS_UNCONSUMED_REQUESTS(sysbr) && ((!max)||(count < max))) {
		// ASSUME: one queue per process
		count++;
		//printk("DEBUG PRE sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
		// might want to think about 0-ing this out, if we aren't
		// going to explicitly fill in all fields
		syscall_rsp_t rsp;
		// this assumes we get our answer immediately for the syscall.
		syscall_req_t* req = RING_GET_REQUEST(sysbr, ++sysbr->req_cons);

		pcpui->cur_kthread->sysc = req->sc;
		// TODO: blocking call will block arcs as well.
		run_local_syscall(req->sc);

		// need to keep the slot in the ring buffer if it is blocked
		(sysbr->rsp_prod_pvt)++;
		req->status = RES_ready;
		RING_PUSH_RESPONSES(sysbr);

		//printk("DEBUG PST sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
	}
	/* switch back to whatever context we were in before */
	switch_back(p, old_proc);
	return (intreg_t)count;
}

