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
	char pad[ARCH_CL_SIZE-sizeof(void*)-sizeof(int)];
} mcs_lock_qnode_t;

typedef struct
{
	mcs_lock_qnode_t* lock;
	char pad[ARCH_CL_SIZE-sizeof(mcs_lock_qnode_t*)];
	mcs_lock_qnode_t qnode[MAX_VCORES] __attribute__((aligned(8)));
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

void mcs_lock_init(mcs_lock_t* lock);
void mcs_lock_unlock(mcs_lock_t* lock);
void mcs_lock_lock(mcs_lock_t* l);

#ifdef __cplusplus
}
#endif

#endif
