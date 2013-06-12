#ifndef ROS_INC_X86_H
#define ROS_INC_X86_H

#include <ros/common.h>
#include <arch/mmu.h>

/* Model Specific Registers */
#define IA32_APIC_BASE				0x1b
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

/* CPUID */
#define CPUID_PSE_SUPPORT			0x00000008

/* Arch Constants */
#define MAX_NUM_CPUS				255

#ifdef CONFIG_X86_64

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
static inline unsigned long read_flags(void) __attribute__((always_inline));
static inline void write_eflags(unsigned long eflags)
              __attribute__((always_inline));
static inline unsigned long read_bp(void) __attribute__((always_inline));
static inline unsigned long read_ip(void) __attribute__((always_inline));
static inline unsigned long read_sp(void) __attribute__((always_inline));
static inline void cpuid(uint32_t info1, uint32_t info2, uint32_t *eaxp,
                         uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
						               __attribute__((always_inline));
static inline uint64_t read_msr(uint32_t reg) __attribute__((always_inline));
static inline void write_msr(uint32_t reg, uint64_t val)
              __attribute__((always_inline));
static inline void write_mmreg32(uintptr_t reg, uint32_t val)
              __attribute__((always_inline));
static inline uint32_t read_mmreg32(uintptr_t reg)
              __attribute__((always_inline));
static inline void wbinvd(void) __attribute__((always_inline));
static inline void __cpu_relax(void) __attribute__((always_inline));

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

static inline unsigned long read_ip(void)
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

// Might need to mfence rdmsr.  supposedly wrmsr serializes, but not for x2APIC
static inline uint64_t read_msr(uint32_t reg)
{
	uint32_t edx, eax;
	asm volatile("rdmsr; mfence" : "=d"(edx), "=a"(eax) : "c"(reg));
	return (uint64_t)edx << 32 | eax;
}

static inline void write_msr(uint32_t reg, uint64_t val)
{
	asm volatile("wrmsr" : : "d"((uint32_t)(val >> 32)),
	                         "a"((uint32_t)(val & 0xFFFFFFFF)), 
	                         "c"(reg));
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

#ifndef UNUSED_ARG
#define UNUSED_ARG(x) (void)x
#endif /* This prevents compiler warnings for UNUSED_ARG */ 

#endif /* !ROS_INC_X86_H */
