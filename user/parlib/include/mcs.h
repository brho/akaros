#ifndef _MCS_H
#define _MCS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <vcore.h>
#include <arch/arch.h>

#define MCS_LOCK_INIT {0}

typedef struct mcs_lock_qnode
{
	volatile struct mcs_lock_qnode* volatile next;
	volatile int locked;
} mcs_lock_qnode_t;

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
/* If you lock the lock from vcore context, you must use these. */
void mcs_lock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);
void mcs_unlock_notifsafe(struct mcs_lock *lock, struct mcs_lock_qnode *qnode);

#ifdef __cplusplus
}
#endif

#endif
