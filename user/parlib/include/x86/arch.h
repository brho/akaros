#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/trapframe.h>
#include <ros/arch/mmu.h>
#include <ros/common.h>
#include <string.h>

#define internal_function   __attribute ((regparm (3), stdcall))

#define ARCH_CL_SIZE 64
#ifdef __x86_64__

#define X86_REG_BP					"rbp"
#define X86_REG_SP					"rsp"
#define X86_REG_IP					"rip"
#define X86_REG_AX					"rax"
#define X86_REG_BX					"rbx"
#define X86_REG_CX					"rcx"
#define X86_REG_DX					"rdx"

#else /* 32 bit */

#define X86_REG_BP					"ebp"
#define X86_REG_SP					"esp"
#define X86_REG_IP					"eip"
#define X86_REG_AX					"eax"
#define X86_REG_BX					"ebx"
#define X86_REG_CX					"ecx"
#define X86_REG_DX					"edx"

#endif /* 64bit / 32bit */

/* Make sure you subtract off/save enough space at the top of the stack for
 * whatever you compiler might want to use when calling a noreturn function or
 * to handle a HW spill or whatever. */
static inline void __attribute__((always_inline))
set_stack_pointer(void *sp)
{
	asm volatile("mov %0,%%"X86_REG_SP"" : : "r"(sp) : "memory", X86_REG_SP);
}

static inline void breakpoint(void)
{
	asm volatile("int3");
}

static inline uint64_t read_tsc(void)
{
	uint32_t edx, eax;
	asm volatile("rdtsc" : "=d"(edx), "=a"(eax));
	return (uint64_t)edx << 32 | eax;
}

/* non-core-id reporting style (it is in ecx) */
static inline uint64_t read_tscp(void)
{
	uint32_t edx, eax;
	asm volatile("rdtscp" : "=d"(edx), "=a"(eax) : : X86_REG_CX);
	return (uint64_t)edx << 32 | eax;
}

static inline void cpuid(uint32_t info1, uint32_t info2, uint32_t *eaxp,
                         uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
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
static inline uint64_t read_tsc_serialized(void)
{
	asm volatile("lfence");	/* mfence on amd */
	return read_tsc();
}

static inline void cpu_relax(void)
{
	asm volatile("pause" : : : "memory");
}

static inline uint64_t read_pmc(uint32_t index)
{
	uint32_t edx, eax;
	asm volatile("rdpmc" : "=d"(edx), "=a"(eax) : "c"(index));
	return (uint64_t)edx << 32 | eax;
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
