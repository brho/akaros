#ifndef _HART_H
#define _HART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ros/error.h>
#include <ros/arch/hart.h>

#define HART_LOG2_MAX_MAX_HARTS 6
#define HART_MAX_MAX_HARTS (1 << HART_LOG2_MAX_MAX_HARTS)
#define HART_CL_SIZE 128

typedef struct hart_lock_qnode
{
	volatile struct hart_lock_qnode* volatile next;
	volatile int locked;
	char pad[HART_CL_SIZE-sizeof(void*)-sizeof(int)];
} hart_lock_qnode_t;

typedef struct
{
	hart_lock_qnode_t* lock;
	char pad[HART_CL_SIZE-sizeof(hart_lock_qnode_t*)];
	hart_lock_qnode_t qnode[HART_MAX_MAX_HARTS] __attribute__((aligned(8)));
} hart_lock_t;

#define HART_LOCK_INIT {0}

typedef struct
{
	volatile int myflags[2][HART_LOG2_MAX_MAX_HARTS];
	volatile int* partnerflags[2][HART_LOG2_MAX_MAX_HARTS];
	int parity;
	int sense;
	char pad[HART_CL_SIZE];
} hart_dissem_flags_t;

typedef struct
{
	size_t nprocs;
	hart_dissem_flags_t* allnodes;
	size_t logp;
} hart_barrier_t;

extern void hart_entry();

error_t hart_barrier_init(hart_barrier_t* b, size_t nprocs);
void hart_barrier_wait(hart_barrier_t* b, size_t vcoreid);

void hart_lock_init(hart_lock_t* lock);
void hart_lock_unlock(hart_lock_t* lock);
void hart_lock_lock(hart_lock_t* l);

int hart_self();
error_t hart_request(size_t k);
void hart_yield();
size_t hart_max_harts();
size_t hart_current_harts();

#ifdef __cplusplus
}
#endif

#endif
