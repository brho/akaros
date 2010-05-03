#ifndef ROS_KERN_ATOMIC_H
#define ROS_KERN_ATOMIC_H

#include <ros/common.h>
#include <ros/atomic.h>
#include <arch/mmu.h>
#include <arch/atomic.h>
#include <arch/arch.h>
#include <assert.h>

static inline void
(SLOCK(0) spin_lock_irqsave)(spinlock_t RACY*SAFE lock);
static inline void
(SUNLOCK(0) spin_unlock_irqsave)(spinlock_t RACY*SAFE lock);
static inline bool spin_lock_irq_enabled(spinlock_t *SAFE lock);

/* An example seq lock, built from the counter.  I don't particularly like this,
 * since it forces you to use a specific locking type.  */
typedef struct seq_lock {
	spinlock_t			w_lock;
	seq_ctr_t			r_ctr;
} seqlock_t;

static inline void __seq_start_write(seq_ctr_t *seq_ctr);
static inline void __seq_end_write(seq_ctr_t *seq_ctr);
static inline void write_seqlock(seqlock_t *lock);
static inline void write_sequnlock(seqlock_t *lock);
static inline seq_ctr_t read_seqbegin(seqlock_t *lock);
static inline bool read_seqretry(seqlock_t *lock, seq_ctr_t ctr);

#define MAX_SPINS 1000000000

/* Will spin for a little while, but not deadlock if it never happens */
#define spin_on(x)                                                             \
	for (int i = 0; (x); i++) {                                                \
		cpu_relax();                                                           \
		if (i == MAX_SPINS) {                                                  \
			printk("Probably timed out/failed.\n");                            \
			break;                                                             \
		}                                                                      \
	}

/*********************** Checklist stuff **********************/
typedef struct checklist_mask {
	// only need an uint8_t, but we need the bits[] to be word aligned
	uint32_t size;
	volatile uint8_t (COUNT(BYTES_FOR_BITMASK(size)) bits)[MAX_NUM_CPUS];
} checklist_mask_t;

// mask contains an unspecified array, so it needs to be at the bottom
struct checklist {
	spinlock_t lock;
	checklist_mask_t mask;
	// eagle-eyed readers may know why this might have been needed. 2009-09-04
	//volatile uint8_t (COUNT(BYTES_FOR_BITMASK(size)) bits)[];
};
typedef struct checklist RACY checklist_t;

#define ZEROS_ARRAY(size) {[0 ... ((size)-1)] 0}

#define DEFAULT_CHECKLIST_MASK(sz) {(sz), ZEROS_ARRAY(BYTES_FOR_BITMASK(sz))}
#define DEFAULT_CHECKLIST(sz) {SPINLOCK_INITIALIZER, DEFAULT_CHECKLIST_MASK(sz)}
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
struct barrier {
	spinlock_t lock;
	uint32_t init_count;
	uint32_t current_count;
	volatile uint8_t ready;
};

typedef struct barrier RACY barrier_t;

void init_barrier(barrier_t*COUNT(1) barrier, uint32_t count);
void reset_barrier(barrier_t* barrier);
void waiton_barrier(barrier_t* barrier);

/* Spinlock bit flags */
#define SPINLOCK_IRQ_EN			0x80000000

// If ints are enabled, disable them and note it in the top bit of the lock
// There is an assumption about releasing locks in order here...
static inline void spin_lock_irqsave(spinlock_t *SAFE lock)
{
	uint32_t irq_en;
	irq_en = irq_is_enabled();
	disable_irq();
	spin_lock(lock);
	if (irq_en)
		lock->rlock |= SPINLOCK_IRQ_EN;
}

// if the high bit of the lock is set, then re-enable interrupts
// (note from asw: you're lucky this works, you little-endian jerks)
static inline void spin_unlock_irqsave(spinlock_t *SAFE lock)
{
	if (spin_lock_irq_enabled(lock)) {
		spin_unlock(lock);
		enable_irq();
	} else
		spin_unlock(lock);
}

/* Returns whether or not unlocking this lock should enable interrupts or not.
 * Is meaningless on locks that weren't locked with irqsave. */
static inline bool spin_lock_irq_enabled(spinlock_t *SAFE lock)
{
	return lock->rlock & SPINLOCK_IRQ_EN;
}

/* Note, the seq_ctr is not a full seq lock - just the counter guts.  Write
 * access can be controlled by another lock (like the proc-lock).  start_ and
 * end_write are the writer's responsibility to signal the readers of a
 * concurrent write. */
static inline void __seq_start_write(seq_ctr_t *seq_ctr)
{
#ifdef _CONFIG_SEQLOCK_DEBUG_
	assert(*seq_ctr % 2 == 0);
#endif
	(*seq_ctr)++;
	/* We're the only writer, so we need to prevent the compiler (and some
	 * arches) from reordering writes before this point. */
	wmb();
}

static inline void __seq_end_write(seq_ctr_t *seq_ctr)
{
#ifdef _CONFIG_SEQLOCK_DEBUG_
	assert(*seq_ctr % 2 == 1);
#endif
	/* Need to prevent the compiler (and some arches) from reordering older
	 * stores */
	wmb();
	(*seq_ctr)++;
}

/* Untested reference implementation of a seq lock.  As mentioned above, we
 * might need a variety of these (for instance, this doesn't do an irqsave).  Or
 * there may be other invariants that we need the lock to protect. */
static inline void write_seqlock(seqlock_t *lock)
{
	spin_lock(&lock->w_lock);
	__seq_start_write(&lock->r_ctr);
}

static inline void write_sequnlock(seqlock_t *lock)
{
	__seq_end_write(&lock->r_ctr);
	spin_unlock(&lock->w_lock);
}

static inline seq_ctr_t read_seqbegin(seqlock_t *lock)
{
	return lock->r_ctr;
}

static inline bool read_seqretry(seqlock_t *lock, seq_ctr_t ctr)
{
	return seqctr_retry(lock->r_ctr, ctr);
}

#endif /* !ROS_KERN_ATOMIC_H */
