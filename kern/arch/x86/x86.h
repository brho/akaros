#pragma once

#include <ros/common.h>
#include <ros/arch/msr-index.h>
#include <arch/mmu.h>
#include <ros/errno.h>
#include <arch/fixup.h>

/* Model Specific Registers */
// TODO: figure out which are intel specific, and name them accordingly
#define IA32_APIC_BASE				0x1b
/* These two are intel-only */
#define IA32_FEATURE_CONTROL 		0x3a
#define IA32_MISC_ENABLE			0x1a0

#define IA32_MTRR_DEF_TYPE			0x2ff
#define IA32_MTRR_PHYSBASE0			0x200
#define IA32_MTRR_PHYSMASK0			0x201
#define IA32_MTRR_PHYSBASE1			0x202
#define IA32_MTRR_PHYSMASK1			0x203
#define IA32_MTRR_PHYSBASE2			0x204
#define IA32_MTRR_PHYSMASK2			0x205
#define IA32_MTRR_PHYSBASE3			0x206
#define IA32_MTRR_PHYSMASK3			0x207
#define IA32_MTRR_PHYSBASE4			0x208
#define IA32_MTRR_PHYSMASK4			0x209
#define IA32_MTRR_PHYSBASE5			0x20a
#define IA32_MTRR_PHYSMASK5			0x20b
#define IA32_MTRR_PHYSBASE6			0x20c
#define IA32_MTRR_PHYSMASK6			0x20d
#define IA32_MTRR_PHYSBASE7			0x20e
#define IA32_MTRR_PHYSMASK7			0x20f

#define MSR_APIC_ENABLE				0x00000800
#define MSR_APIC_BASE_ADDRESS		0x0000000FFFFFF000

#define IA32_EFER_MSR				0xc0000080
# define IA32_EFER_SYSCALL			(1 << 0)
# define IA32_EFER_IA32E_EN			(1 << 8)
# define IA32_EFER_IA32E_ACT		(1 << 10)
# define IA32_EFER_EXE_DIS_BIT		(1 << 11)

#define MSR_TSC_AUX					0xc0000103

#define MSR_FS_BASE					0xc0000100
#define MSR_GS_BASE					0xc0000101
#define MSR_KERN_GS_BASE			0xc0000102

#define MSR_STAR					0xc0000081
#define MSR_LSTAR					0xc0000082
#define MSR_CSTAR					0xc0000083
#define MSR_SFMASK					0xc0000084

/* CPUID */
#define CPUID_PSE_SUPPORT			0x00000008

/* Arch Constants */
#define MAX_NUM_CORES				255

#define X86_REG_BP					"rbp"
#define X86_REG_SP					"rsp"
#define X86_REG_IP					"rip"
#define X86_REG_AX					"rax"
#define X86_REG_BX					"rbx"
#define X86_REG_CX					"rcx"
#define X86_REG_DX					"rdx"


/* Various flags defined: can be included from assembler. */

/*
 * EFLAGS bits
 */
#define X86_EFLAGS_CF	0x00000001 /* Carry Flag */
#define X86_EFLAGS_BIT1	0x00000002 /* Bit 1 - always on */
#define X86_EFLAGS_PF	0x00000004 /* Parity Flag */
#define X86_EFLAGS_AF	0x00000010 /* Auxiliary carry Flag */
#define X86_EFLAGS_ZF	0x00000040 /* Zero Flag */
#define X86_EFLAGS_SF	0x00000080 /* Sign Flag */
#define X86_EFLAGS_TF	0x00000100 /* Trap Flag */
#define X86_EFLAGS_IF	0x00000200 /* Interrupt Flag */
#define X86_EFLAGS_DF	0x00000400 /* Direction Flag */
#define X86_EFLAGS_OF	0x00000800 /* Overflow Flag */
#define X86_EFLAGS_IOPL	0x00003000 /* IOPL mask */
#define X86_EFLAGS_NT	0x00004000 /* Nested Task */
#define X86_EFLAGS_RF	0x00010000 /* Resume Flag */
#define X86_EFLAGS_VM	0x00020000 /* Virtual Mode */
#define X86_EFLAGS_AC	0x00040000 /* Alignment Check */
#define X86_EFLAGS_VIF	0x00080000 /* Virtual Interrupt Flag */
#define X86_EFLAGS_VIP	0x00100000 /* Virtual Interrupt Pending */
#define X86_EFLAGS_ID	0x00200000 /* CPUID detection flag */

/*
 * Basic CPU control in CR0
 */
#define X86_CR0_PE	0x00000001 /* Protection Enable */
#define X86_CR0_MP	0x00000002 /* Monitor Coprocessor */
#define X86_CR0_EM	0x00000004 /* Emulation */
#define X86_CR0_TS	0x00000008 /* Task Switched */
#define X86_CR0_ET	0x00000010 /* Extension Type */
#define X86_CR0_NE	0x00000020 /* Numeric Error */
#define X86_CR0_WP	0x00010000 /* Write Protect */
#define X86_CR0_AM	0x00040000 /* Alignment Mask */
#define X86_CR0_NW	0x20000000 /* Not Write-through */
#define X86_CR0_CD	0x40000000 /* Cache Disable */
#define X86_CR0_PG	0x80000000 /* Paging */

/*
 * Paging options in CR3
 */
#define X86_CR3_PWT	0x00000008 /* Page Write Through */
#define X86_CR3_PCD	0x00000010 /* Page Cache Disable */
#define X86_CR3_PCID_MASK 0x00000fff /* PCID Mask */

/*
 * Intel CPU features in CR4
 */
#define X86_CR4_VME	0x00000001 /* enable vm86 extensions */
#define X86_CR4_PVI	0x00000002 /* virtual interrupts flag enable */
#define X86_CR4_TSD	0x00000004 /* disable time stamp at ipl 3 */
#define X86_CR4_DE	0x00000008 /* enable debugging extensions */
#define X86_CR4_PSE	0x00000010 /* enable page size extensions */
#define X86_CR4_PAE	0x00000020 /* enable physical address extensions */
#define X86_CR4_MCE	0x00000040 /* Machine check enable */
#define X86_CR4_PGE	0x00000080 /* enable global pages */
#define X86_CR4_PCE	0x00000100 /* enable performance counters at ipl 3 */
#define X86_CR4_OSFXSR	0x00000200 /* enable fast FPU save and restore */
#define X86_CR4_OSXMMEXCPT 0x00000400 /* enable unmasked SSE exceptions */
#define X86_CR4_VMXE	0x00002000 /* enable VMX virtualization */
#define X86_CR4_RDWRGSFS 0x00010000 /* enable RDWRGSFS support */
#define X86_CR4_PCIDE	0x00020000 /* enable PCID support */
#define X86_CR4_OSXSAVE 0x00040000 /* enable xsave and xrestore */
#define X86_CR4_SMEP	0x00100000 /* enable SMEP support */
#define X86_CR4_SMAP	0x00200000 /* enable SMAP support */

/* MWAIT C-state hints.  The names might not be right for different processors.
 * For instance, the Linux idle driver for a Haswell calls the mwait for 0x10
 * "C3-HSW". */
#define X86_MWAIT_C1			0x00
#define X86_MWAIT_C2			0x10
#define X86_MWAIT_C3			0x20
#define X86_MWAIT_C4			0x30
#define X86_MWAIT_C5			0x40
#define X86_MWAIT_C6			0x50

/*
 * x86-64 Task Priority Register, CR8
 */
#define X86_CR8_TPR	0x0000000F /* task priority register */

#ifndef __ASSEMBLER__

static inline uint8_t inb(int port) __attribute__((always_inline));
static inline void insb(int port, void *addr, int cnt)
              __attribute__((always_inline));
static inline uint16_t inw(int port) __attribute__((always_inline));
static inline void insw(int port, void *addr, int cnt)
              __attribute__((always_inline));
static inline uint32_t inl(int port) __attribute__((always_inline));
static inline void insl(int port, void *addr, int cnt)
              __attribute__((always_inline));
static inline void outb(int port, uint8_t data) __attribute__((always_inline));
static inline void outsb(int port, const void *addr, int cnt)
              __attribute__((always_inline));
static inline void outw(int port, uint16_t data) __attribute__((always_inline));
static inline void outsw(int port, const void *addr, int cnt)
              __attribute__((always_inline));
static inline void outsl(int port, const void *addr, int cnt)
              __attribute__((always_inline));
static inline void outl(int port, uint32_t data) __attribute__((always_inline));
static inline void lidt(void *p) __attribute__((always_inline));
static inline void lldt(uint16_t sel) __attribute__((always_inline));
static inline void ltr(uint16_t sel) __attribute__((always_inline));
static inline void lcr0(unsigned long val) __attribute__((always_inline));
static inline unsigned long rcr0(void) __attribute__((always_inline));
static inline unsigned long rcr2(void) __attribute__((always_inline));
static inline void lcr3(unsigned long val) __attribute__((always_inline));
static inline unsigned long rcr3(void) __attribute__((always_inline));
static inline void lcr4(unsigned long val) __attribute__((always_inline));
static inline unsigned long rcr4(void) __attribute__((always_inline));

static inline void lxcr0(uint64_t xcr0) __attribute__((always_inline));
static inline int safe_lxcr0(uint64_t xcr0) __attribute__((always_inline));
static inline uint64_t rxcr0(void) __attribute__((always_inline));

static inline unsigned long read_flags(void) __attribute__((always_inline));
static inline void write_eflags(unsigned long eflags)
              __attribute__((always_inline));
static inline unsigned long read_bp(void) __attribute__((always_inline));
static inline unsigned long read_pc(void) __attribute__((always_inline));
static inline unsigned long read_sp(void) __attribute__((always_inline));
static inline void cpuid(uint32_t info1, uint32_t info2, uint32_t *eaxp,
                         uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
						               __attribute__((always_inline));
static inline uint32_t cpuid_ecx(uint32_t op) __attribute__((always_inline));
static inline uint64_t read_msr(uint32_t reg) __attribute__((always_inline));
static inline void write_msr(uint32_t reg, uint64_t val)
              __attribute__((always_inline));
/* if we have mm64s, change the hpet helpers */
static inline void write_mmreg8(uintptr_t reg, uint8_t val)
							__attribute__((always_inline));
static inline uint8_t read_mmreg8(uintptr_t reg)
							__attribute__((always_inline));
static inline void write_mmreg32(uintptr_t reg, uint32_t val)
              __attribute__((always_inline));
static inline uint32_t read_mmreg32(uintptr_t reg)
              __attribute__((always_inline));
static inline void wbinvd(void) __attribute__((always_inline));
static inline void __cpu_relax(void) __attribute__((always_inline));

void set_pstate(unsigned int pstate);
void set_fastest_pstate(void);
unsigned int get_pstate(void);
void set_cstate(unsigned int cstate);
unsigned int get_cstate(void);

static inline uint8_t inb(int port)
{
	uint8_t data;
	asm volatile("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static inline void insb(int port, void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\tinsb"
	             : "=D" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "memory", "cc");
}

static inline uint16_t inw(int port)
{
	uint16_t data;
	asm volatile("inw %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static inline void insw(int port, void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\tinsw"
	             : "=D" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "memory", "cc");
}

static inline uint32_t inl(int port)
{
	uint32_t data;
	asm volatile("inl %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static inline void insl(int port, void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\tinsl"
	             : "=D" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "memory", "cc");
}

static inline void outb(int port, uint8_t data)
{
	asm volatile("outb %0,%w1" : : "a" (data), "d" (port));
}

static inline void outsb(int port, const void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\toutsb"
	             : "=S" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "cc");
}

static inline void outw(int port, uint16_t data)
{
	asm volatile("outw %0,%w1" : : "a" (data), "d" (port));
}

static inline void outsw(int port, const void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\toutsw"
	             : "=S" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "cc");
}

static inline void outsl(int port, const void *addr, int cnt)
{
	asm volatile("cld\n\trepne\n\toutsl"
	             : "=S" (addr), "=c" (cnt)
	             : "d" (port), "0" (addr), "1" (cnt)
	             : "cc");
}

static inline void outl(int port, uint32_t data)
{
	asm volatile("outl %0,%w1" : : "a" (data), "d" (port));
}

static inline void lidt(void *p)
{
	asm volatile("lidt (%0)" : : "r" (p));
}

static inline void lldt(uint16_t sel)
{
	asm volatile("lldt %0" : : "r" (sel));
}

static inline void ltr(uint16_t sel)
{
	asm volatile("ltr %0" : : "r" (sel));
}

static inline void lcr0(unsigned long val)
{
	asm volatile("mov %0,%%cr0" : : "r" (val));
}

static inline unsigned long rcr0(void)
{
	unsigned long val;
	asm volatile("mov %%cr0,%0" : "=r" (val));
	return val;
}

static inline void lcr2(unsigned long val)
{
	asm volatile("mov %0,%%cr2" : : "r" (val));
}

static inline unsigned long rcr2(void)
{
	unsigned long val;
	asm volatile("mov %%cr2,%0" : "=r" (val));
	return val;
}

static inline void lcr3(unsigned long val)
{
	asm volatile("mov %0,%%cr3" : : "r" (val));
}

static inline unsigned long rcr3(void)
{
	unsigned long val;
	asm volatile("mov %%cr3,%0" : "=r" (val));
	return val;
}

static inline void lcr4(unsigned long val)
{
	asm volatile("mov %0,%%cr4" : : "r" (val));
}

static inline unsigned long rcr4(void)
{
	unsigned long cr4;
	asm volatile("mov %%cr4,%0" : "=r" (cr4));
	return cr4;
}

static inline void lxcr0(uint64_t xcr0)
{
	uint32_t eax, edx;

	edx = xcr0 >> 32;
	eax = xcr0;
	asm volatile("xsetbv"
	             : /* No outputs */
	             : "a"(eax), "c" (0), "d"(edx));
}

static inline int safe_lxcr0(uint64_t xcr0)
{
	int err = 0;
	uint32_t eax, edx;

	edx = xcr0 >> 32;
	eax = xcr0;
	asm volatile(ASM_STAC               ";"
		         "1: xsetbv              ;"
	             "2: " ASM_CLAC "        ;"
	             ".section .fixup, \"ax\";"
	             "3: mov %4, %0          ;"
	             "   jmp 2b              ;"
	             ".previous              ;"
	             _ASM_EXTABLE(1b, 3b)
	             : "=r" (err)
	             : "a"(eax), "c" (0), "d"(edx),
	               "i" (-EINVAL), "0" (err));

	return err;
}

static inline uint64_t rxcr0(void)
{
	uint32_t eax, edx;

	asm volatile("xgetbv"
	             : "=a"(eax), "=d"(edx)
	             : "c" (0));
	return ((uint64_t)edx << 32) | eax;
}

static inline unsigned long read_flags(void)
{
	unsigned long eflags;
	asm volatile("pushf; pop %0" : "=r" (eflags));
	return eflags;
}

static inline void write_eflags(unsigned long eflags)
{
	asm volatile("push %0; popf" : : "r" (eflags));
}

static inline unsigned long read_bp(void)
{
	unsigned long bp;
	asm volatile("mov %%"X86_REG_BP",%0" : "=r" (bp));
	return bp;
}

static inline unsigned long read_pc(void)
{
	unsigned long ip;
	asm volatile("call 1f; 1: pop %0" : "=r"(ip));
	return ip;
}

static inline unsigned long read_sp(void)
{
	unsigned long sp;
	asm volatile("mov %%"X86_REG_SP",%0" : "=r" (sp));
	return sp;
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

static inline uint32_t cpuid_ecx(uint32_t op)
{
	uint32_t ecx;
	cpuid(op, 0, NULL, NULL, &ecx, NULL);
	return ecx;
}

// Might need to mfence rdmsr.  supposedly wrmsr serializes, but not for x2APIC
static inline uint64_t read_msr(uint32_t reg)
{
	uint32_t edx, eax;
	asm volatile("rdmsr; mfence" : "=d"(edx), "=a"(eax) : "c"(reg));
	return (uint64_t)edx << 32 | eax;
}

static inline void write_msr(uint32_t reg, uint64_t val)
{
	asm volatile("wrmsr" : : "d"(val >> 32), "a"(val & 0xFFFFFFFF), "c"(reg));
}

static inline void write_mmreg8(uintptr_t reg, uint8_t val)
{
	*((volatile uint8_t*)reg) = val;
}

static inline uint8_t read_mmreg8(uintptr_t reg)
{
	return *((volatile uint8_t*)reg);
}

static inline void write_mmreg32(uintptr_t reg, uint32_t val)
{
	*((volatile uint32_t*)reg) = val;
}

static inline uint32_t read_mmreg32(uintptr_t reg)
{
	return *((volatile uint32_t*)reg);
}

static inline void wbinvd(void)
{
	asm volatile("wbinvd");
}

/* this version of cpu_relax is needed to resolve some circular dependencies
 * with arch/arch.h and arch/apic.h */
static inline void __cpu_relax(void)
{
	// in case the compiler doesn't serialize for pause, the "m" will make sure
	// no memory is reordered around this instruction.
	asm volatile("pause" : : : "memory");
}

#endif /* !__ASSEMBLER__ */
