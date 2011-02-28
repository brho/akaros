/* See COPYRIGHT for copyright information. */

#ifdef __SHARC__
#pragma nosharc
#endif


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
spinlock_t arsc_proc_lock = SPINLOCK_INITIALIZER;

intreg_t inline syscall_async(struct proc *p, syscall_req_t *call)
{
	return syscall(p, call->num, call->args[0], call->args[1],
	               call->args[2], call->args[3], call->args[4], call->args[5]);
}

intreg_t sys_init_arsc(struct proc *p)
{
	proc_incref(p, 1);		/* we're storing an external ref here */
	spin_lock_irqsave(&arsc_proc_lock);
	TAILQ_INSERT_TAIL(&arsc_proc_list, p, proc_arsc_link);
	spin_unlock_irqsave(&arsc_proc_lock);
	return ESUCCESS;
}

void arsc_server(struct trapframe *tf)
{
	struct proc *p = NULL;
	TAILQ_INIT(&arsc_proc_list);
	while (1) {
		while (TAILQ_EMPTY(&arsc_proc_list))
			cpu_relax();

		TAILQ_FOREACH(p, &arsc_proc_list, proc_link) {
			/* Probably want to try to process a dying process's syscalls.  If
			 * not, just move it to an else case */
			process_generic_syscalls (p, MAX_ASRC_BATCH); 
			if (p->state == PROC_DYING) {
				TAILQ_REMOVE(&arsc_proc_list, p, proc_arsc_link);
				proc_decref(p);
				/* Need to break out, so the TAILQ_FOREACH doesn't flip out.
				 * It's not fair, but we're not dealing with that yet anyway */
				break;
			}
		}
	}
}

static intreg_t process_generic_syscalls(struct proc *p, size_t max)
{
	size_t count = 0;
	syscall_back_ring_t* sysbr = &p->syscallbackring;
	struct per_cpu_info* coreinfo = &per_cpu_info[core_id()];
	// looking at a process not initialized to perform arsc. 
	if (sysbr == NULL) 
		return count;

	// max is the most we'll process.  max = 0 means do as many as possible
	// TODO: check for initialization of the ring. 
	while (RING_HAS_UNCONSUMED_REQUESTS(sysbr) && ((!max)||(count < max)) ) {
		if (!count) {
			// ASSUME: one queue per process
			// only switch cr3 for the very first request for this queue
			// need to switch to the right context, so we can handle the user pointer
			// that points to a data payload of the syscall
			lcr3(p->env_cr3);
		}
		count++;
		//printk("DEBUG PRE: sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
		// might want to think about 0-ing this out, if we aren't
		// going to explicitly fill in all fields
		syscall_rsp_t rsp;
		// this assumes we get our answer immediately for the syscall.
		syscall_req_t* req = RING_GET_REQUEST(sysbr, ++(sysbr->req_cons));
		// print req
		printd("req no %d, req arg %c\n", req->num, *((char*)req->args[0]));
		
		/* TODO: when the remote syscall stuff can handle the new async
		 * syscalls, they need to use a real sysc.  This might at least stop it
		 * from crashing. */
		struct syscall sysc = {0};
		coreinfo->cur_sysc = &sysc;
		
		rsp.retval = syscall_async(p, req);
		rsp.syserr = sysc.err;
		// write response into the slot it came from
		memcpy(req, &rsp, sizeof(syscall_rsp_t));
		// update our counter for what we've produced (assumes we went in order!)
		(sysbr->rsp_prod_pvt)++;
		RING_PUSH_RESPONSES(sysbr);
		//printk("DEBUG POST: sring->req_prod: %d, sring->rsp_prod: %d\n",
		//	   sysbr->sring->req_prod, sysbr->sring->rsp_prod);
	}
	// load sane page tables (and don't rely on decref to do it for you).
	lcr3(boot_cr3);
	return (intreg_t)count;
}

