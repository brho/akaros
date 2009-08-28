#ifndef ROS_KERN_ATOMIC_H
#define ROS_KERN_ATOMIC_H

#include <arch/types.h>
#include <arch/mmu.h>
#include <arch/atomic.h>
#include <arch/arch.h>

static inline void spin_lock_irqsave(volatile uint32_t*SAFE lock);
static inline void spin_unlock_irqsave(volatile uint32_t*SAFE lock);

/*********************** Checklist stuff **********************/
typedef struct checklist_mask {
	// only need an uint8_t, but we need the bits[] to be word aligned
	uint32_t size;
	volatile uint8_t (COUNT(BYTES_FOR_BITMASK(size)) bits)[];
} checklist_mask_t;

// mask contains an unspecified array, so it needs to be at the bottom
typedef struct checklist {
	volatile uint32_t lock;
	checklist_mask_t mask;
} checklist_t;

#define ZEROS_ARRAY(size) {[0 ... ((size)-1)] 0}

#define DEFAULT_CHECKLIST_MASK(sz) {(sz), ZEROS_ARRAY(BYTES_FOR_BITMASK(sz))}
#define DEFAULT_CHECKLIST(sz) {0, DEFAULT_CHECKLIST_MASK(sz)}
#define INIT_CHECKLIST(nm, sz)	\
	checklist_t nm = DEFAULT_CHECKLIST(sz);
#define INIT_CHECKLIST_MASK(nm, sz)	\
	checklist_mask_t nm = DEFAULT_CHECKLIST_MASK(sz);

int commit_checklist_wait(checklist_t* list, checklist_mask_t* mask);
int commit_checklist_nowait(checklist_t* list, checklist_mask_t* mask);
int waiton_checklist(checklist_t* list);
int release_checklist(checklist_t* list);
int checklist_is_locked(checklist_t* list);
int checklist_is_clear(checklist_t* list);
void reset_checklist(checklist_t* list);
void down_checklist(checklist_t* list);
// TODO - do we want to adjust the size?  (YES, don't want to check it all)
// TODO - do we want to be able to call waiton without having called commit?
// 	- in the case of protected checklists
// TODO - want a destroy checklist (when we have kmalloc, or whatever)
// TODO - some sort of dynamic allocation of them in the future
// TODO - think about deadlock issues with one core spinning on a lock for
// something that it is the hold out for...
// 	- probably should have interrupts enabled, and never grab these locks
// 	from interrupt context (and not use irq_save)
/**************************************************************/

/* Barrier: currently made for everyone barriering.  Change to use checklist */
typedef struct barrier {
	volatile uint32_t lock;
	uint32_t init_count;
	uint32_t current_count;
    volatile uint8_t ready;
} barrier_t;

void init_barrier(barrier_t*COUNT(1) barrier, uint32_t count);
void reset_barrier(barrier_t* barrier);
void waiton_barrier(barrier_t* barrier);

// If ints are enabled, disable them and note it in the top bit of the lock
// There is an assumption about releasing locks in order here...
static inline void spin_lock_irqsave(volatile uint32_t*SAFE lock)
{
	uint32_t irq_en;
	irq_en = irq_is_enabled();
	disable_irq();
	spin_lock(lock);
	if (irq_en)
		*lock |= 0x80000000;
}

// if the high bit of the lock is set, then re-enable interrupts
// (note from asw: you're lucky this works, you little-endian jerks)
static inline void spin_unlock_irqsave(volatile uint32_t*SAFE lock)
{
	if (*lock & 0x80000000) {
		*lock = 0;
		enable_irq();
	} else
		*lock = 0;
}

#endif /* !ROS_KERN_ATOMIC_H */
