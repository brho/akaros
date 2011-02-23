#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>

#include <bitmask.h>
#include <atomic.h>
#include <error.h>
#include <string.h>
#include <assert.h>

// Must be called in a pair with waiton_checklist
int commit_checklist_wait(checklist_t* list, checklist_mask_t* mask)
{
	assert(list->mask.size == mask->size);
	// abort if the list is locked.  this will protect us from trying to commit
	// and thus spin on a checklist that we are already waiting on.  it is
	// still possible to not get the lock, but the holder is on another core.
	// Or, bail out if we can see the list is already in use.  This check is
	// just an optimization before we try to use the list for real.
	if ((checklist_is_locked(list)) || !checklist_is_clear(list))
		return -EBUSY;

	// possession of this lock means you can wait on it and set it
	spin_lock_irqsave(&list->lock);
	// wait til the list is available.  could have some adaptive thing here
	// where it fails after X tries (like 500), gives up the lock, and returns
	// an error code
	while (!checklist_is_clear(list))
		cpu_relax();

	// list is ours and clear, set it to the settings of our list
	COPY_BITMASK(list->mask.bits, mask->bits, mask->size); 
	return 0;
}

int commit_checklist_nowait(checklist_t* list, checklist_mask_t* mask)
{
	int e = 0;
	if ((e = commit_checklist_wait(list, mask)))
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
	extern atomic_t outstanding_calls;
	// can consider breakout out early, like above, and erroring out
	while (!checklist_is_clear(list))
		cpu_relax();
	spin_unlock_irqsave(&list->lock);
	// global counter of wrappers either waited on or being contended for.
	atomic_dec(&outstanding_calls);
	return 0;
}

// like waiton, but don't bother waiting either
int release_checklist(checklist_t* list)
{
	spin_unlock_irqsave(&list->lock);
	return 0;
}

// peaks in and sees if the list is locked with it's spinlock
int checklist_is_locked(checklist_t* list)
{
	return spin_locked(&list->lock);
}

// no synch guarantees - just looks at the list
int checklist_is_clear(checklist_t* list)
{
	return BITMASK_IS_CLEAR(list->mask.bits, list->mask.size);
}

// no synch guarantees - just resets the list to empty
void reset_checklist(checklist_t* list)
{
	CLR_BITMASK(list->mask.bits, list->mask.size);
}

// CPU mask specific - this is how cores report in
void down_checklist(checklist_t* list)
{
	CLR_BITMASK_BIT_ATOMIC(list->mask.bits, core_id());
}

/* Barriers */
void init_barrier(barrier_t* barrier, uint32_t count)
{
	spinlock_init(&barrier->lock);
	barrier->init_count = count;
	barrier->current_count = count;
	barrier->ready = 0;
}

void reset_barrier(barrier_t* barrier)
{
	barrier->current_count = barrier->init_count;
}

// primitive barrier function.  all cores call this.
void waiton_barrier(barrier_t* barrier)
{
	uint8_t local_ready = barrier->ready;

	spin_lock_irqsave(&barrier->lock);
	barrier->current_count--;
	if (barrier->current_count) {
		spin_unlock_irqsave(&barrier->lock);
		while (barrier->ready == local_ready)
			cpu_relax();
	} else {
		spin_unlock_irqsave(&barrier->lock);
		reset_barrier(barrier);
		wmb();
		barrier->ready++;
	}
}
