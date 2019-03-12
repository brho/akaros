/* Copyright (c) 2009-2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Kernel atomics and locking functions.
 *
 * The extern inline declarations are arch-dependent functions.  We do this
 * so that each arch can either static inline or just have a regular function,
 * whichever is appropriate. The actual implementation usually will be in
 * arch/atomic.h (for inlines).
 *
 * The static inlines are defined farther down in the file (as always). */

#pragma once

#include <ros/common.h>
#include <ros/atomic.h>
#include <arch/membar.h>
#include <arch/mmu.h>
#include <arch/arch.h>
#include <assert.h>

/* Atomics */
extern inline void atomic_init(atomic_t *number, long val);
extern inline long atomic_read(atomic_t *number);
extern inline void atomic_set(atomic_t *number, long val);
extern inline void atomic_add(atomic_t *number, long val);
extern inline void atomic_inc(atomic_t *number);
extern inline void atomic_dec(atomic_t *number);
extern inline long atomic_fetch_and_add(atomic_t *number, long val);
extern inline void atomic_and(atomic_t *number, long mask);
extern inline void atomic_or(atomic_t *number, long mask);
extern inline long atomic_swap(atomic_t *addr, long val);
extern inline bool atomic_cas(atomic_t *addr, long exp_val, long new_val);
extern inline bool atomic_cas_ptr(void **addr, void *exp_val, void *new_val);
extern inline bool atomic_cas_u32(uint32_t *addr, uint32_t exp_val,
                                  uint32_t new_val);
extern inline bool atomic_add_not_zero(atomic_t *number, long val);
extern inline bool atomic_sub_and_test(atomic_t *number, long val);

/* Spin locks */
struct spinlock {
	volatile uint32_t rlock;
#ifdef CONFIG_SPINLOCK_DEBUG
	uintptr_t call_site;
	uint32_t calling_core;
	bool irq_okay;
#endif
};
typedef struct spinlock spinlock_t;
#define SPINLOCK_INITIALIZER {0}

#ifdef CONFIG_SPINLOCK_DEBUG
#define SPINLOCK_INITIALIZER_IRQSAVE {0, .irq_okay = TRUE}
#else
#define SPINLOCK_INITIALIZER_IRQSAVE SPINLOCK_INITIALIZER
#endif

/* Arch dependent helpers/funcs: */
extern inline void __spinlock_init(spinlock_t *lock);
extern inline bool spin_locked(spinlock_t *lock);
extern inline void __spin_lock(spinlock_t *lock);
extern inline void __spin_unlock(spinlock_t *lock);

/* So we can inline a __spin_lock if we want.  Even though we don't need this
 * if we're debugging, its helpful to keep the include at the same place for
 * all builds. */
#include <arch/atomic.h>

#ifdef CONFIG_SPINLOCK_DEBUG
/* Arch indep, in k/s/atomic.c */
void spin_lock(spinlock_t *lock);
bool spin_trylock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
void spinlock_debug(spinlock_t *lock);

#else
/* Just inline the arch-specific __ versions */
static inline void spin_lock(spinlock_t *lock)
{
	__spin_lock(lock);
}

static inline bool spin_trylock(spinlock_t *lock)
{
	return __spin_trylock(lock);
}

static inline void spin_unlock(spinlock_t *lock)
{
	__spin_unlock(lock);
}

static inline void spinlock_debug(spinlock_t *lock)
{
}

#endif /* CONFIG_SPINLOCK_DEBUG */

/* Inlines, defined below */
static inline void spinlock_init(spinlock_t *lock);
static inline void spinlock_init_irqsave(spinlock_t *lock);
static inline void spin_lock_irqsave(spinlock_t *lock);
static inline bool spin_trylock_irqsave(spinlock_t *lock);
static inline void spin_unlock_irqsave(spinlock_t *lock);
static inline bool spin_lock_irq_enabled(spinlock_t *lock);

/* Hash locks (array of spinlocks).  Most all users will want the default one,
 * so point your pointer to one of them, though you could always kmalloc a
 * bigger one.  In the future, they might be growable, etc, which init code may
 * care about. */
struct hashlock {
	unsigned int			nr_entries;
	struct spinlock			locks[];
};
#define HASHLOCK_DEFAULT_SZ 53		/* nice prime, might be a bit large */
struct small_hashlock {
	unsigned int			nr_entries;
	struct spinlock			locks[HASHLOCK_DEFAULT_SZ];
};

void hashlock_init(struct hashlock *hl, unsigned int nr_entries);
void hashlock_init_irqsave(struct hashlock *hl, unsigned int nr_entries);
void hash_lock(struct hashlock *hl, long key);
void hash_unlock(struct hashlock *hl, long key);
void hash_lock_irqsave(struct hashlock *hl, long key);
void hash_unlock_irqsave(struct hashlock *hl, long key);

/* Seq locks */
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

/* Post work and poke synchronization.  This is a wait-free way to make sure
 * some code is run, usually by the calling core, but potentially by any core.
 * Under contention, everyone just posts work, and one core will carry out the
 * work.  Callers post work (the meaning of which is particular to their
 * subsystem), then call this function.  The function is not run concurrently
 * with itself.
 *
 * In the future, this may send RKMs to LL cores to ensure the work is done
 * somewhere, but not necessarily on the calling core.  Will reserve 'flags'
 * for that. */
struct poke_tracker {
	atomic_t			need_to_run;
	atomic_t			run_in_progress;
	void				(*func)(void *);
};
void poke(struct poke_tracker *tracker, void *arg);

static inline void poke_init(struct poke_tracker *tracker, void (*func)(void*))
{
	tracker->need_to_run = 0;
	tracker->run_in_progress = 0;
	tracker->func = func;
}
#define POKE_INITIALIZER(f) {.func = f}

/* Arch-specific implementations / declarations go here */
#include <arch/atomic.h>

#define MAX_SPINS 1000000000

/* Will spin for a little while, but not deadlock if it never happens */
#define spin_on(x)                                                             \
	for (int i = 0; (x); i++) {                                            \
		cpu_relax();                                                   \
		if (i == MAX_SPINS) {                                          \
			printk("Probably timed out/failed.\n");                \
			break;                                                 \
		}                                                              \
	}

/*********************** Checklist stuff **********************/
typedef struct checklist_mask {
	// only need an uint8_t, but we need the bits[] to be word aligned
	uint32_t size;
	volatile uint8_t bits[MAX_NUM_CORES];
} checklist_mask_t;

// mask contains an unspecified array, so it needs to be at the bottom
struct checklist {
	spinlock_t lock;
	checklist_mask_t mask;
	// eagle-eyed readers may know why this might have been needed.
	// 2009-09-04
	//volatile uint8_t (COUNT(BYTES_FOR_BITMASK(size)) bits)[];
};
typedef struct checklist checklist_t;

#define ZEROS_ARRAY(size) {[0 ... ((size)-1)] 0}

#define DEFAULT_CHECKLIST_MASK(sz) {(sz), ZEROS_ARRAY(BYTES_FOR_BITMASK(sz))}
#define DEFAULT_CHECKLIST(sz) {SPINLOCK_INITIALIZER_IRQSAVE,                   \
                               DEFAULT_CHECKLIST_MASK(sz)}
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
int checklist_is_full(checklist_t* list);
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

typedef struct barrier barrier_t;

void init_barrier(barrier_t *barrier, uint32_t count);
void reset_barrier(barrier_t* barrier);
void waiton_barrier(barrier_t* barrier);

/* Spinlock bit flags */
#define SPINLOCK_IRQ_EN			0x80000000

static inline void spinlock_init(spinlock_t *lock)
{
	__spinlock_init(lock);
#ifdef CONFIG_SPINLOCK_DEBUG
	lock->call_site = 0;
	lock->calling_core = 0;
	lock->irq_okay = FALSE;
#endif
}

static inline void spinlock_init_irqsave(spinlock_t *lock)
{
	__spinlock_init(lock);
#ifdef CONFIG_SPINLOCK_DEBUG
	lock->call_site = 0;
	lock->calling_core = 0;
	lock->irq_okay = TRUE;
#endif
}

// If ints are enabled, disable them and note it in the top bit of the lock
// There is an assumption about releasing locks in order here...
static inline void spin_lock_irqsave(spinlock_t *lock)
{
	uint32_t irq_en;
	irq_en = irq_is_enabled();
	disable_irq();
	spin_lock(lock);
	if (irq_en)
		lock->rlock |= SPINLOCK_IRQ_EN;
}

static inline bool spin_trylock_irqsave(spinlock_t *lock)
{
	uint32_t irq_en = irq_is_enabled();

	disable_irq();
	if (!spin_trylock(lock)) {
		if (irq_en)
			enable_irq();
		return FALSE;
	}
	if (irq_en)
		lock->rlock |= SPINLOCK_IRQ_EN;
	return TRUE;
}

// if the high bit of the lock is set, then re-enable interrupts
// (note from asw: you're lucky this works, you little-endian jerks)
static inline void spin_unlock_irqsave(spinlock_t *lock)
{
	if (spin_lock_irq_enabled(lock)) {
		spin_unlock(lock);
		enable_irq();
	} else
		spin_unlock(lock);
}

/* Returns whether or not unlocking this lock should enable interrupts or not.
 * Is meaningless on locks that weren't locked with irqsave. */
static inline bool spin_lock_irq_enabled(spinlock_t *lock)
{
	return lock->rlock & SPINLOCK_IRQ_EN;
}

/* Note, the seq_ctr is not a full seq lock - just the counter guts.  Write
 * access can be controlled by another lock (like the proc-lock).  start_ and
 * end_write are the writer's responsibility to signal the readers of a
 * concurrent write. */
static inline void __seq_start_write(seq_ctr_t *seq_ctr)
{
#ifdef CONFIG_SEQLOCK_DEBUG
	assert(*seq_ctr % 2 == 0);
#endif
	(*seq_ctr)++;
	/* We're the only writer, so we need to prevent the compiler (and some
	 * arches) from reordering writes before this point. */
	wmb();
}

static inline void __seq_end_write(seq_ctr_t *seq_ctr)
{
#ifdef CONFIG_SEQLOCK_DEBUG
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
	seq_ctr_t retval = lock->r_ctr;
	rmb();	/* don't want future reads to come before our ctr read */
	return retval;
}

static inline bool read_seqretry(seqlock_t *lock, seq_ctr_t ctr)
{
	return seqctr_retry(lock->r_ctr, ctr);
}
