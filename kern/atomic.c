#include <inc/string.h>
#include <inc/assert.h>

#include <kern/atomic.h>
#include <kern/apic.h>

// Must be called in a pair with waiton_checklist
int commit_checklist_wait(checklist_t* list, checklist_mask_t* mask)
{
	assert(list->mask.size == mask->size);
	// possession of this lock means you can wait on it and set it
	spin_lock_irqsave(&list->lock);
	// wait til the list is available.  could have some adaptive thing here
	// where it fails after X tries (like 500), gives up the lock, and returns
	// an error code
	while (!(BITMASK_IS_CLEAR(list->mask.bits, list->mask.size)))
		cpu_relax();

	// list is ours and clear, set it to the settings of our list
	COPY_BITMASK(list->mask.bits, mask->bits, mask->size); 
	return 0;
}

int commit_checklist_nowait(checklist_t* list, checklist_mask_t* mask)
{
	int e = 0;
	if (e = commit_checklist_wait(list, mask))
		return e;
	// give up the lock, since we won't wait for completion
	spin_unlock_irqsave(&list->lock);
	return e;
}
// The deal with the lock:
// what if two different actors are waiting on the list, but for different reasons?
// part of the problem is we are doing both set and check via the same path
//
// aside: we made this a lot more difficult than the usual barriers or even 
// the RCU grace-period checkers, since we have to worry about this construct
// being used by others before we are done with it.
//
// how about this: if we want to wait on this later, we just don't release the
// lock.  if we release it, then we don't care who comes in and grabs and starts
// checking the list.  
// 	- regardless, there are going to be issues with people looking for a free 
// 	item.  even if they grab the lock, they may end up waiting a while and 
// 	wantint to bail (like test for a while, give up, move on, etc).  
// 	- still limited in that only the setter can check, and only one person
// 	can spinwait / check for completion.  if someone else tries to wait (wanting
// 	completion), they may miss it if someone else comes in and grabs the lock
// 	to use it for a new checklist
// 		- if we had the ability to sleep and get woken up, we could have a 
// 		queue.  actually, we could do a queue anyway, but they all spin
// 		and it's the bosses responsibility to *wake* them

// Must be called after commit_checklist
// Assumed we held the lock if we ever call this
int waiton_checklist(checklist_t* list)
{
	// can consider breakout out early, like above, and erroring out
	while (!(BITMASK_IS_CLEAR(list->mask.bits, list->mask.size)))
		cpu_relax();
	spin_unlock_irqsave(&list->lock);
	return 0;
}

// CPU mask specific - this is how cores report in
void down_checklist(checklist_t* list)
{
	CLR_BITMASK_BIT_ATOMIC(list->mask.bits, lapic_get_id());
}

// byte per cpu, as mentioned below
void init_barrier_all(barrier_t* cpu_barrier)
{
	extern uint8_t num_cpus;
	uint8_t i;
	cpu_barrier->ready = 0;
	for(i = 0; i < num_cpus; i++)
		cpu_barrier->cpu_array[i] = 1;
}

// primitive barrier function.  all cores call this.
// consider changing this to use bits and lock bit ops.
// currently uses a byte per core, and assumes it was 
// initialized by a core such that num_cpus entries
// are all 1
void barrier_all(barrier_t* cpu_barrier)
{
	extern uint8_t num_cpus;
	uint8_t i;
	uint8_t local_ready = cpu_barrier->ready;

	cpu_barrier->cpu_array[lapic_get_id()] = 0;
	if (lapic_get_id())
		while(cpu_barrier->ready == local_ready)
			cpu_relax();
	else {
		for(i = 0; i < num_cpus; i++) {
			while(cpu_barrier->cpu_array[i]) 
				cpu_relax();
			cpu_barrier->cpu_array[i] = 1;
		}
		// if we need to wmb(), it'll be here
		cpu_barrier->ready++;
	}
}
