#pragma once

#include <ros/trapframe.h>
#include <ros/arch/mmu.h>
#include <ros/procinfo.h>
#include <parlib/cpu_feat.h>

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

static inline unsigned long get_stack_pointer(void)
{
	unsigned long sp;

	asm volatile("mov %%"X86_REG_SP",%0" : "=r"(sp));
	return sp;
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
	uint64_t x86_default_xcr0 = __proc_global_info.x86_default_xcr0;
	uint32_t eax, edx;

	/* PLEASE NOTE:
	 * AMD CPUs ignore the FOP/FIP/FDP fields when there is
	 * no pending exception. When you are on AMD, we zero these fields in the
	 * ancillary_state argument before saving. This way, if you are on AMD and
	 * re-using an ancillary_state memory region, an old save's information
	 * won't leak into your new data. The side-effect of this is that you can't
	 * trust these fields to report accurate information on AMD unless an
	 * exception was pending. Granted, AMD says that only exception handlers
	 * should care about FOP/FIP/FDP, so that's probably okay.
	 *
	 * You should also note that on newer Intel 64 processors, while the value
	 * of the FOP is always saved and restored, it contains the opcode of the
	 * most recent x87 FPU instruction that triggered an unmasked exception,
	 * rather than simply the most recent opcode. Some older Xeons and P4s had
	 * the fopcode compatibility mode feature, which you could use to make the
	 * FOP update on every x87 non-control instruction, but that has been
	 * eliminated in newer hardware.
	 *
	 */
	if (cpu_has_feat(CPU_FEAT_X86_VENDOR_AMD)) {
		silly->fp_head_64d.fop      = 0x0;
		silly->fp_head_64d.fpu_ip   = 0x0;
		silly->fp_head_64d.cs       = 0x0;
		silly->fp_head_64d.padding1 = 0x0; // padding1 is FIP or rsvd, proc dep.
		silly->fp_head_64d.fpu_dp   = 0x0;
		silly->fp_head_64d.ds       = 0x0;
		silly->fp_head_64d.padding2 = 0x0; // padding2 is FDP or rsvd, proc dep.
	}


	if (cpu_has_feat(CPU_FEAT_X86_XSAVEOPT)) {
		edx = x86_default_xcr0 >> 32;
		eax = x86_default_xcr0;
		asm volatile("xsaveopt64 %0" : : "m"(*silly), "a"(eax), "d"(edx));
	} else if (cpu_has_feat(CPU_FEAT_X86_XSAVE)) {
		edx = x86_default_xcr0 >> 32;
		eax = x86_default_xcr0;
		asm volatile("xsave64 %0" : : "m"(*silly), "a"(eax), "d"(edx));
	} else {
		asm volatile("fxsave64 %0" : : "m"(*silly));
	}
}

// NOTE: If you try to restore from a garbage ancillary_state,
//       you might trigger a fault and crash your program.
static inline void restore_fp_state(struct ancillary_state *silly)
{
	uint64_t x86_default_xcr0 = __proc_global_info.x86_default_xcr0;
	uint32_t eax, edx;

	/*
	 * Since AMD CPUs ignore the FOP/FIP/FDP fields when there is
	 * no pending exception, we clear those fields before restoring
	 * when we are both on AMD and there is no pending exception in
	 * the ancillary_state argument to restore_fp_state.
	 * If there is a pending exception in the ancillary_state,
	 * these fields will be written to the FPU upon executing
	 * a restore instruction, and there is nothing to worry about.
	 *
	 * See CVE-2006-1056 and CVE-2013-2076 on cve.mitre.org.
	 *
	 * We check for a pending exception by checking FSW.ES (bit 7)
	 *
	 * FNINIT clears FIP and FDP and, even though it is technically a
	 * control instruction, it clears FOP because it is initializing the FPU.
	 *
	 * NOTE: This might not be the most efficient way to do things, and
	 *       could be an optimization target for context switch performance
	 *       on AMD processors in the future.
	 */
	if (!(silly->fp_head_64d.fsw & 0x80)
		&& cpu_has_feat(CPU_FEAT_X86_VENDOR_AMD))
		asm volatile ("fninit;");

	if (cpu_has_feat(CPU_FEAT_X86_XSAVE)) {
		edx = x86_default_xcr0 >> 32;
		eax = x86_default_xcr0;
		asm volatile("xrstor64 %0" : : "m"(*silly), "a"(eax), "d"(edx));
	} else {
		asm volatile("fxrstor64 %0" : : "m"(*silly));
	}
}

static inline bool arch_has_mwait(void)
{
	return cpu_has_feat(CPU_FEAT_X86_MWAIT);
}

/* Cpuid helper function originally from Barret's fputest. */
static inline void parlib_cpuid(uint32_t level1, uint32_t level2,
                                uint32_t *eaxp, uint32_t *ebxp,
                                uint32_t *ecxp, uint32_t *edxp)
{
	uint32_t eax, ebx, ecx, edx;

	asm volatile("cpuid"
	             : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	             : "a"(level1), "c"(level2));
	if (eaxp)
		*eaxp = eax;
	if (ebxp)
		*ebxp = ebx;
	if (ecxp)
		*ecxp = ecx;
	if (edxp)
		*edxp = edx;
}

__END_DECLS
