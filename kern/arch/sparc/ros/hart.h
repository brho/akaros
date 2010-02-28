#ifndef _ROS_ARCH_HART_H
#define _ROS_ARCH_HART_H

#define __RAMP__
double do_fdiv(double,double);
double do_fsqrt(double);
double do_recip(double);
double do_rsqrt(double);

#define HART_CL_SIZE 128
#define HART_ATOMIC_HASHTABLE_SIZE 17

#define __hart_self_on_entry (__hart_self())

static inline int
__hart_self()
{
	int id;
	asm ("mov %%asr13,%0" : "=r"(id));
	return id;
}

static inline void
__hart_set_stack_pointer(void* sp)
{
	__asm__ __volatile__ ("mov %0,%%sp" : : "r"(sp));
}

static inline void
__hart_relax()
{
	// TODO: relax
}

static inline int
__hart_swap(int* addr, int val)
{
	__asm__ __volatile__ ("swap [%2],%0" :"=r"(val) :"0"(val),"r"(addr) :"memory");
	return val;
}

extern int __hart_atomic_hash_locks[HART_CL_SIZE*HART_ATOMIC_HASHTABLE_SIZE];
static inline int*
__hart_atomic_hash_lock(int* addr)
{
	int hash = ((unsigned int)addr/sizeof(int*))/HART_ATOMIC_HASHTABLE_SIZE;
	return &__hart_atomic_hash_locks[HART_CL_SIZE/sizeof(int)*hash];
}

static inline int
__hart_fetch_and_add(int* addr, int addend)
{
	int * lock = __hart_atomic_hash_lock(addr);
	while(__hart_swap(lock,1));

	int old = *addr;
	*addr = old+addend;

	*lock = 0;
	return old;
}

static inline int
__hart_compare_and_swap(int* addr, int testval, int newval)
{
	int * lock = __hart_atomic_hash_lock(addr);
	while(__hart_swap(lock,1));

	int old = *addr;
	if(old == testval)
		*addr = newval;

	*lock = 0;
	return old;
}

#endif
