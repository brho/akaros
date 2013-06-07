#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/trapframe.h>
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

/* non-core-id reporting style (it is in ecx) */
static __inline uint64_t
read_tscp(void)
{
	uint64_t tsc;
	__asm __volatile("rdtscp" : "=A" (tsc) : : "ecx");
	return tsc;
}

static __inline void
cpuid(uint32_t info1, uint32_t info2, uint32_t *eaxp, uint32_t *ebxp,
      uint32_t *ecxp, uint32_t *edxp)
{
	uint32_t eax, ebx, ecx, edx;
	/* Can select with both eax (info1) and ecx (info2) */
	asm volatile("cpuid" 
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (info1), "c" (info2));
	if (eaxp)
		*eaxp = eax;
	if (ebxp)
		*ebxp = ebx;
	if (ecxp)
		*ecxp = ecx;
	if (edxp)
		*edxp = edx;
}

/* Check out k/a/x86/rdtsc_test.c for more info */
static __inline uint64_t 
read_tsc_serialized(void)
{
	asm volatile("lfence");	/* mfence on amd */
	return read_tsc();
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

static inline void save_fp_state(struct ancillary_state *silly)
{
	asm volatile("fxsave %0" : : "m"(*silly));
}

static inline void restore_fp_state(struct ancillary_state *silly)
{
	asm volatile("fxrstor %0" : : "m"(*silly));
}

#endif /* PARLIB_ARCH_H */
