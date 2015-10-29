#pragma once

#include <parlib/vcore.h>
#include <parlib/arch/arch.h>

__BEGIN_DECLS

#define MCS_LOCK_INIT {0}
#define MCS_QNODE_INIT {0, 0}

typedef struct mcs_lock_qnode
{
	struct mcs_lock_qnode *next;
	int locked;
}__attribute__((aligned(ARCH_CL_SIZE))) mcs_lock_qnode_t;

typedef struct mcs_lock
{
	mcs_lock_qnode_t* lock;
} mcs_lock_t;

typedef struct
{
	volatile int myflags[2][LOG2_MAX_VCORES];
	volatile int* partnerflags[2][LOG2_MAX_VCORES];
	int parity;
	int sense;
	char pad[ARCH_CL_SIZE];
} mcs_dissem_flags_t;

typedef struct
{
	size_t nprocs;
	mcs_dissem_flags_t* allnodes;
	size_t logp;
} mcs_barrier_t;

int mcs_barrier_init(mcs_barrier_t* b, size_t nprocs);
void mcs_barrier_wait(mcs_barrier_t* b, size_t vcoreid);

void mcs_lock_init(struct mcs_lock *lock);
/* Caller needs to alloc (and zero) their own qnode to spin on.  The memory
 * should be on a cacheline that is 'per-thread'.  This could be on the stack,
 * in a thread control block, etc. */
void mcs_lock_lock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);
void mcs_lock_unlock(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);
void mcs_lock_unlock_cas(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);
/* If you lock the lock from vcore context, you must use these. */
void mcs_lock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);
void mcs_unlock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);

/* Preemption detection and recovering MCS locks.
 *
 * The basic idea is that when spinning, vcores make sure someone else is
 * making progress that will lead to them not spinning.  Usually, it'll be to
 * make sure the lock holder (if known) is running.  If we don't know the lock
 * holder, we ensure the end of whatever chain we can see is running, which
 * will make sure its predecessor runs, which will eventually unjam the system.
 * */

/* Old style.  Has trouble getting out of 'preempt/change-to storms' under
 * heavy contention and with preemption. */
struct mcs_pdro_qnode
{
	struct mcs_pdro_qnode *next;
	int locked;
	uint32_t vcoreid;
}__attribute__((aligned(ARCH_CL_SIZE)));

struct mcs_pdro_lock
{
	struct mcs_pdro_qnode *lock;
};

#define MCSPDRO_LOCK_INIT {0}
#define MCSPDRO_QNODE_INIT {0, 0, 0}

void mcs_pdro_init(struct mcs_pdro_lock *lock);
void mcs_pdro_fini(struct mcs_pdro_lock *lock);
void mcs_pdro_lock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode);
void mcs_pdro_unlock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode);

/* Only call these if you have notifs disabled and know your vcore's qnode.
 * Mostly used for debugging, benchmarks, or critical code. */
void __mcs_pdro_lock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode);
void __mcs_pdro_unlock(struct mcs_pdro_lock *lock, struct mcs_pdro_qnode *qnode);
void __mcs_pdro_unlock_no_cas(struct mcs_pdro_lock *lock,
                             struct mcs_pdro_qnode *qnode);

/* New style: under heavy contention with preemption, they won't enter the
 * 'preempt/change_to storm' that can happen to PDRs, at the cost of some
 * performance.  This is the default. */
struct mcs_pdr_qnode
{
	struct mcs_pdr_qnode *next;
	int locked;
}__attribute__((aligned(ARCH_CL_SIZE)));

struct mcs_pdr_lock
{
	struct mcs_pdr_qnode *lock __attribute__((aligned(ARCH_CL_SIZE)));
	uint32_t lockholder_vcoreid __attribute__((aligned(ARCH_CL_SIZE)));
	struct mcs_pdr_qnode *qnodes __attribute__((aligned(ARCH_CL_SIZE)));
};

#define MCSPDR_NO_LOCKHOLDER ((uint32_t)-1)

void mcs_pdr_init(struct mcs_pdr_lock *lock);
void mcs_pdr_fini(struct mcs_pdr_lock *lock);
void mcs_pdr_lock(struct mcs_pdr_lock *lock);
void mcs_pdr_unlock(struct mcs_pdr_lock *lock);

__END_DECLS
