#ifndef JOS_INC_X86_H
#define JOS_INC_X86_H

#include <inc/types.h>
#include <inc/mmu.h>

/* Model Specific Registers */
#define IA32_APIC_BASE				0x1b
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

static __inline void breakpoint(void) __attribute__((always_inline));
static __inline uint8_t inb(int port) __attribute__((always_inline));
static __inline void insb(int port, void *addr, int cnt) __attribute__((always_inline));
static __inline uint16_t inw(int port) __attribute__((always_inline));
static __inline void insw(int port, void *addr, int cnt) __attribute__((always_inline));
static __inline uint32_t inl(int port) __attribute__((always_inline));
static __inline void insl(int port, void *addr, int cnt) __attribute__((always_inline));
static __inline void outb(int port, uint8_t data) __attribute__((always_inline));
static __inline void outsb(int port, const void *addr, int cnt) __attribute__((always_inline));
static __inline void outw(int port, uint16_t data) __attribute__((always_inline));
static __inline void outsw(int port, const void *addr, int cnt) __attribute__((always_inline));
static __inline void outsl(int port, const void *addr, int cnt) __attribute__((always_inline));
static __inline void outl(int port, uint32_t data) __attribute__((always_inline));
static __inline void invlpg(void *addr) __attribute__((always_inline));
static __inline void lidt(void *p) __attribute__((always_inline));
static __inline void lldt(uint16_t sel) __attribute__((always_inline));
static __inline void ltr(uint16_t sel) __attribute__((always_inline));
static __inline void lcr0(uint32_t val) __attribute__((always_inline));
static __inline uint32_t rcr0(void) __attribute__((always_inline));
static __inline uint32_t rcr2(void) __attribute__((always_inline));
static __inline void lcr3(uint32_t val) __attribute__((always_inline));
static __inline uint32_t rcr3(void) __attribute__((always_inline));
static __inline void lcr4(uint32_t val) __attribute__((always_inline));
static __inline uint32_t rcr4(void) __attribute__((always_inline));
static __inline void tlbflush(void) __attribute__((always_inline));
static __inline uint32_t read_eflags(void) __attribute__((always_inline));
static __inline void write_eflags(uint32_t eflags) __attribute__((always_inline));
static __inline uint32_t read_ebp(void) __attribute__((always_inline));
static __inline uint32_t read_esp(void) __attribute__((always_inline));
static __inline void cpuid(uint32_t info, uint32_t *eaxp, uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp);
static __inline uint64_t read_tsc(void) __attribute__((always_inline));
static __inline uint64_t read_msr(uint32_t reg) __attribute__((always_inline));
static __inline void write_msr(uint32_t reg, uint64_t val) __attribute__((always_inline));
static __inline uint32_t read_mmreg32(uint32_t reg) __attribute__((always_inline));
static __inline void write_mmreg32(uint32_t reg, uint32_t val) __attribute__((always_inline));
static __inline void enable_irq(void) __attribute__((always_inline));
static __inline void disable_irq(void) __attribute__((always_inline));
static __inline void enable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void disable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void cpu_relax(void) __attribute__((always_inline));
static __inline void wbinvd(void) __attribute__((always_inline));
static __inline void clflush(uintptr_t* addr) __attribute__((always_inline));

static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}

static __inline uint8_t
inb(int port)
{
	uint8_t data;
	__asm __volatile("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insb(int port, void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\tinsb"			:
			 "=D" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory", "cc");
}

static __inline uint16_t
inw(int port)
{
	uint16_t data;
	__asm __volatile("inw %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insw(int port, void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\tinsw"			:
			 "=D" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory", "cc");
}

static __inline uint32_t
inl(int port)
{
	uint32_t data;
	__asm __volatile("inl %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

static __inline void
insl(int port, void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\tinsl"			:
			 "=D" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "memory", "cc");
}

static __inline void
outb(int port, uint8_t data)
{
	__asm __volatile("outb %0,%w1" : : "a" (data), "d" (port));
}

static __inline void
outsb(int port, const void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\toutsb"		:
			 "=S" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "cc");
}

static __inline void
outw(int port, uint16_t data)
{
	__asm __volatile("outw %0,%w1" : : "a" (data), "d" (port));
}

static __inline void
outsw(int port, const void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\toutsw"		:
			 "=S" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "cc");
}

static __inline void
outsl(int port, const void *addr, int cnt)
{
	__asm __volatile("cld\n\trepne\n\toutsl"		:
			 "=S" (addr), "=c" (cnt)		:
			 "d" (port), "0" (addr), "1" (cnt)	:
			 "cc");
}

static __inline void
outl(int port, uint32_t data)
{
	__asm __volatile("outl %0,%w1" : : "a" (data), "d" (port));
}

static __inline void 
invlpg(void *addr)
{ 
	__asm __volatile("invlpg (%0)" : : "r" (addr) : "memory");
}  

static __inline void
lidt(void *p)
{
	__asm __volatile("lidt (%0)" : : "r" (p));
}

static __inline void
lldt(uint16_t sel)
{
	__asm __volatile("lldt %0" : : "r" (sel));
}

static __inline void
ltr(uint16_t sel)
{
	__asm __volatile("ltr %0" : : "r" (sel));
}

static __inline void
lcr0(uint32_t val)
{
	__asm __volatile("movl %0,%%cr0" : : "r" (val));
}

static __inline uint32_t
rcr0(void)
{
	uint32_t val;
	__asm __volatile("movl %%cr0,%0" : "=r" (val));
	return val;
}

static __inline uint32_t
rcr2(void)
{
	uint32_t val;
	__asm __volatile("movl %%cr2,%0" : "=r" (val));
	return val;
}

static __inline void
lcr3(uint32_t val)
{
	__asm __volatile("movl %0,%%cr3" : : "r" (val));
}

static __inline uint32_t
rcr3(void)
{
	uint32_t val;
	__asm __volatile("movl %%cr3,%0" : "=r" (val));
	return val;
}

static __inline void
lcr4(uint32_t val)
{
	__asm __volatile("movl %0,%%cr4" : : "r" (val));
}

static __inline uint32_t
rcr4(void)
{
	uint32_t cr4;
	__asm __volatile("movl %%cr4,%0" : "=r" (cr4));
	return cr4;
}

static __inline void
tlbflush(void)
{
	uint32_t cr3;
	__asm __volatile("movl %%cr3,%0" : "=r" (cr3));
	__asm __volatile("movl %0,%%cr3" : : "r" (cr3));
}

static __inline uint32_t
read_eflags(void)
{
        uint32_t eflags;
        __asm __volatile("pushfl; popl %0" : "=r" (eflags));
        return eflags;
}

static __inline void
write_eflags(uint32_t eflags)
{
        __asm __volatile("pushl %0; popfl" : : "r" (eflags));
}

static __inline uint32_t
read_ebp(void)
{
        uint32_t ebp;
        __asm __volatile("movl %%ebp,%0" : "=r" (ebp));
        return ebp;
}

static __inline uint32_t
read_esp(void)
{
        uint32_t esp;
        __asm __volatile("movl %%esp,%0" : "=r" (esp));
        return esp;
}

static __inline void
cpuid(uint32_t info, uint32_t *eaxp, uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
{
	uint32_t eax, ebx, ecx, edx;
	asm volatile("cpuid" 
		: "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		: "a" (info));
	if (eaxp)
		*eaxp = eax;
	if (ebxp)
		*ebxp = ebx;
	if (ecxp)
		*ecxp = ecx;
	if (edxp)
		*edxp = edx;
}

static __inline uint64_t
read_tsc(void)
{
        uint64_t tsc;
        __asm __volatile("rdtsc" : "=A" (tsc));
        return tsc;
}

// Might need to mfence rdmsr.  supposedly wrmsr serializes, but not for x2APIC
static __inline uint64_t
read_msr(uint32_t reg)
{
	uint32_t edx, eax;
	asm volatile("rdmsr; mfence" : "=d"(edx), "=a"(eax) : "c"(reg));
	return (uint64_t)edx << 32 | eax;
}

static __inline void
write_msr(uint32_t reg, uint64_t val)
{
	asm volatile("wrmsr" : : "d"((uint32_t)(val >> 32)),
	                         "a"((uint32_t)(val & 0xFFFFFFFF)), 
	                         "c"(reg));
}

static __inline void
write_mmreg32(uint32_t reg, uint32_t val)
{
	{TRUSTEDBLOCK *((volatile uint32_t*)reg) = val; }
	//the C ends up producing better asm than this:
	//asm volatile("movl %0, (%1)" : : "r"(val), "r"(reg));
}

static __inline uint32_t
read_mmreg32(uint32_t reg)
{
	{TRUSTEDBLOCK return *((volatile uint32_t*)reg); }
}

static __inline void
enable_irq(void)
{
	asm volatile("sti");
}

static __inline void
disable_irq(void)
{
	asm volatile("cli");
}

static __inline void
enable_irqsave(int8_t* state)
{
	// *state tracks the number of nested enables and disables
	// initial value of state: 0 = first run / no favorite
	// > 0 means more enabled calls have been made
	// < 0 means more disabled calls have been made
	// Mostly doing this so we can call disable_irqsave first if we want

	// one side or another "gets a point" if interrupts were already the
	// way it wanted to go.  o/w, state stays at 0.  if the state was not 0
	// then, enabling/disabling isn't even an option.  just increment/decrement

	// if enabling is winning or tied, make sure it's enabled
	if ((*state == 0) && (!(read_eflags() & FL_IF)))
		enable_irq();
	else
		(*state)++;
}

static __inline void
disable_irqsave(int8_t* state)
{
	if ((*state == 0) && (read_eflags() & FL_IF))
		disable_irq();
	else 
		(*state)--;
}

static __inline void
cpu_relax(void)
{
	asm volatile("pause");
}

static __inline void
wbinvd(void) __attribute__((always_inline))
{
	asm volatile("wbinvd");
}

static __inline void
clflush(uintptr_t* addr) __attribute__((always_inline))
{
	asm volatile("clflush %0" : : "m"(*addr));
}
#endif /* !JOS_INC_X86_H */
