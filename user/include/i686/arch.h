#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/arch/trapframe.h>
#include <ros/arch/mmu.h>
#include <ros/common.h>
#include <string.h>

#define internal_function   __attribute ((regparm (3), stdcall))

#define ARCH_CL_SIZE 64

/* Make sure you subtract off/save enough space at the top of the stack for
 * whatever you compiler might want to use when calling a noreturn function or
 * to handle a HW spill or whatever. */
static __inline void __attribute__((always_inline))
set_stack_pointer(void* sp)
{
	asm volatile ("mov %0,%%esp" : : "r"(sp) : "memory","esp");
}

static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}

static __inline uint64_t
read_tsc(void)
{
	uint64_t tsc;
	__asm __volatile("rdtsc" : "=A" (tsc));
	return tsc;
}

static __inline void
cpu_relax(void)
{
	asm volatile("pause" : : : "memory");
}

static __inline uint64_t                                                                             
read_pmc(uint32_t index)
{                                                                                                    
    uint64_t pmc;

    __asm __volatile("rdpmc" : "=A" (pmc) : "c" (index)); 
    return pmc;                                                                                      
}


#endif /* PARLIB_ARCH_H */
