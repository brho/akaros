#ifndef PARLIB_ARCH_H
#define PARLIB_ARCH_H

#include <ros/trapframe.h>
#include <ros/arch/mmu.h>
#include <parlib/common.h>
#include <string.h>

__BEGIN_DECLS

#define ARCH_CL_SIZE 64
#ifdef __x86_64__

#define internal_function 
#define X86_REG_BP					"rbp"
#define X86_REG_SP					"rsp"
#define X86_REG_IP					"rip"
#define X86_REG_AX					"rax"
#define X86_REG_BX					"rbx"
#define X86_REG_CX					"rcx"
#define X86_REG_DX					"rdx"

#else /* 32 bit */

#define internal_function   __attribute ((regparm (3), stdcall))
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

static inline void save_fp_state(struct ancillary_state *silly)
{
	asm volatile("fxsave %0" : : "m"(*silly));
}

static inline void restore_fp_state(struct ancillary_state *silly)
{
	asm volatile("fxrstor %0" : : "m"(*silly));
}

__END_DECLS

#endif /* PARLIB_ARCH_H */
