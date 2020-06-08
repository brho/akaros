/* basic locking code that compiles on linux.  #included directly into
 * lock_test.  It's a .h so that make tests doesn't build it. */

#define ARCH_CL_SIZE 64
#define SPINLOCK_INITIALIZER {FALSE}

typedef struct {
	bool locked;
} spinlock_t;

void __attribute__((noinline)) spinlock_init(spinlock_t *lock)
{
	lock->locked = FALSE;
}

/* Returns TRUE if we grabbed the lock */
bool __attribute__((noinline)) spinlock_trylock(spinlock_t *lock)
{
	if (lock->locked)
		return FALSE;
	return !__sync_lock_test_and_set(&lock->locked, TRUE);
}

void __attribute__((noinline)) spinlock_lock(spinlock_t *lock)
{
	while (!spinlock_trylock(lock))
		cpu_relax();
}

void __attribute__((noinline)) spinlock_unlock(spinlock_t *lock)
{
	__sync_lock_release(&lock->locked, FALSE);
}

#define MCS_LOCK_INIT {0}
#define MCS_QNODE_INIT {0, 0}

typedef struct mcs_lock_qnode
{
	struct mcs_lock_qnode *next;
	int locked;
}__attribute__((aligned(ARCH_CL_SIZE))) mcs_lock_qnode_t;

/* included for the dummy init in lock_thread */
struct mcs_pdro_qnode
{
	struct mcs_pdro_qnode *next;
	int locked;
	uint32_t vcoreid;
}__attribute__((aligned(ARCH_CL_SIZE)));

#define MCSPDRO_QNODE_INIT {0, 0, 0}
#define mcs_pdr_init(args...) {}

typedef struct mcs_lock
{
	mcs_lock_qnode_t *lock;
} mcs_lock_t;

void __attribute__((noinline)) mcs_lock_init(struct mcs_lock *lock)
{
	memset(lock, 0, sizeof(mcs_lock_t));
}

static inline mcs_lock_qnode_t *mcs_qnode_swap(mcs_lock_qnode_t **addr,
                                               mcs_lock_qnode_t *val)
{
	return (mcs_lock_qnode_t*) __sync_lock_test_and_set((void**)addr, val);
}

void __attribute__((noinline))
mcs_lock_lock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	qnode->next = 0;
	cmb();	/* swap provides a CPU mb() */
	mcs_lock_qnode_t *predecessor = mcs_qnode_swap(&lock->lock, qnode);
	if (predecessor) {
		qnode->locked = 1;
		wmb();
		predecessor->next = qnode;
		/* no need for a wrmb(), since this will only get unlocked
		 * after they read our previous write */
		while (qnode->locked)
			cpu_relax();
	}
	cmb();	/* just need a cmb, the swap handles the CPU wmb/wrmb() */
}

void __attribute__((noinline))
mcs_lock_unlock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb(); /* no need for CPU mbs, since there's an atomic_swap() */
		/* Unlock it */
		mcs_lock_qnode_t *old_tail = mcs_qnode_swap(&lock->lock,0);
		/* no one else was already waiting, so we successfully unlocked
		 * and can return */
		if (old_tail == qnode)
			return;
		/* someone else was already waiting on the lock (last one on
		 * the list), and we accidentally took them off.  Try and put
		 * it back. */
		mcs_lock_qnode_t *usurper = mcs_qnode_swap(&lock->lock,
							   old_tail);
		/* since someone else was waiting, they should have made
		 * themselves our next.  spin (very briefly!) til it happens.
		 * */
		while (qnode->next == 0)
			cpu_relax();
		if (usurper) {
			/* an usurper is someone who snuck in before we could
			 * put the old tail back.  They now have the lock.
			 * Let's put whoever is supposed to be next as their
			 * next one. */
			usurper->next = qnode->next;
		} else {
			/* No usurper meant we put things back correctly, so we
			 * should just pass the lock / unlock whoever is next */
			qnode->next->locked = 0;
		}
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}

/* CAS style mcs locks, kept around til we use them.  We're using the
 * usurper-style, since RISCV doesn't have a real CAS (yet?). */
void __attribute__((noinline))
mcs_lock_unlock_cas(struct mcs_lock *lock, struct mcs_lock_qnode *qnode)
{
	/* Check if someone is already waiting on us to unlock */
	if (qnode->next == 0) {
		cmb();	/* no need for CPU mbs, since there's an atomic_cas() */
		/* If we're still the lock, just swap it with 0 (unlock) and
		 * return */
		if (__sync_bool_compare_and_swap((void**)&lock->lock, qnode, 0))
			return;
		/* We failed, someone is there and we are some (maybe a
		 * different) thread's pred.  Since someone else was waiting,
		 * they should have made themselves our next.  Spin (very
		 * briefly!) til it happens. */
		while (qnode->next == 0)
			cpu_relax();
		/* Alpha wants a read_barrier_depends() here */
		/* Now that we have a next, unlock them */
		qnode->next->locked = 0;
	} else {
		/* mb()s necessary since we didn't call an atomic_swap() */
		/* need to make sure any previous writes don't pass unlocking */
		wmb();
		/* need to make sure any reads happen before the unlocking */
		rwmb();
		/* simply unlock whoever is next */
		qnode->next->locked = 0;
	}
}
