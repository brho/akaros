#ifndef ROS_INC_ARCH_H
#define ROS_INC_ARCH_H

#include <ros/arch/arch.h>

/* Arch Constants */
#define HW_CACHE_ALIGN		64
#define IOAPIC_BASE		0xFEC00000 // max virtual address

#include <arch/mmu.h>
#include <arch/sparc.h>

#ifndef __ASSEMBLER__

#include <ros/common.h>
#include <arch/timer.h>

static __inline void breakpoint(void) __attribute__((always_inline));
static __inline void invlpg(void *addr) __attribute__((always_inline));
static __inline uint64_t read_tsc(void) __attribute__((always_inline));
static __inline uint64_t read_tsc_serialized(void) __attribute__((always_inline));
static __inline void enable_irq(void) __attribute__((always_inline));
static __inline void disable_irq(void) __attribute__((always_inline));
static __inline void enable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void disable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void cpu_relax(void) __attribute__((always_inline));
static __inline void tlbflush(void) __attribute__((always_inline));
static __inline void icache_flush_page(void* va, void* pa)__attribute__((always_inline));
static __inline void clflush(uintptr_t* addr) __attribute__((always_inline));
static __inline int irq_is_enabled(void) __attribute__((always_inline));
static __inline uint32_t core_id(void) __attribute__((always_inline));
static __inline void cache_flush(void) __attribute__((always_inline));
static __inline void reboot(void) __attribute__((always_inline)) __attribute__((noreturn));
static __inline void lcr3(uint32_t val) __attribute__((always_inline));
static __inline uint32_t rcr3(void) __attribute__((always_inline));

void print_cpuinfo(void);
void show_mapping(uintptr_t start, size_t size);
void backtrace(void);
void cpu_halt(void);

extern uintptr_t mmu_context_tables[MAX_NUM_CPUS][NCONTEXTS+CONTEXT_TABLE_PAD];

static __inline void
breakpoint(void)
{
	asm volatile ("ta 0x7f");
}

static __inline void 
invlpg(void *addr)
{ 
	store_alternate(((uintptr_t)addr) & ~0xFFF,3,0);
}  

static __inline void
tlbflush(void)
{
	store_alternate(0x400,3,0);
}

static __inline void
icache_flush_page(void* va, void* kva)
{
	for(int i = 0; i < PGSIZE; i+=32) // functional pipeline line size
		clflush((uintptr_t*)((char*)kva+i));
}

static __inline uint64_t
read_tsc(void)
{
	return read_perfctr(0,0);
}

static __inline uint64_t 
read_tsc_serialized(void)
{
	return read_tsc();
}

static __inline void
enable_irq(void)
{
	write_psr(read_psr() & ~PSR_PIL);
}

static __inline void
disable_irq(void)
{
	write_psr(read_psr() | PSR_PIL);
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
	if ((*state == 0) && !irq_is_enabled())
		enable_irq();
	else
		(*state)++;
}

static __inline void
disable_irqsave(int8_t* state)
{
	if ((*state == 0) && irq_is_enabled())
		disable_irq();
	else 
		(*state)--;
}

static __inline uint64_t
read_perfctr(uint32_t cpu, uint32_t which)
{
	register uint32_t hi asm("o0"), lo asm("o1");
	uintptr_t addr = cpu<<10 | which<<3;

	#ifdef ROS_KERNEL
		int8_t state = 0;
		disable_irqsave(&state);
		hi = load_iobus(0,addr);
		lo = load_iobus(0,addr+4);
		enable_irqsave(&state);
	#else
		// can't use two load_iobuses in userspace because of atomicity
		asm volatile("mov %2,%%o0; ta 9"
		             : "=r"(hi),"=r"(lo) : "r"(addr));
	#endif
	return (((uint64_t)hi) << 32) | lo;
}

static __inline void
cpu_relax(void)
{
	int ctr = 8;
	asm volatile("1: deccc %0; bne 1b; nop" :
	             "=r"(ctr) : "0"(ctr) : "cc","memory");
}

static __inline void
clflush(uintptr_t* addr)
{
	asm volatile("flush %0" : : "r"(addr));
}

static __inline int
irq_is_enabled(void)
{
	return (read_psr() & PSR_PIL) == 0;
}

static __inline uint32_t
core_id(void)
{
	uint32_t reg;
	asm ("mov %" XSTR(CORE_ID_REG) ",%0" : "=r"(reg));
	return reg;
}

static __inline void
cache_flush(void)
{
}

static __inline void
reboot(void)
{
	extern void appserver_die(int code);
	appserver_die(0);
	while(1);
}

static __inline void
lcr3(uint32_t val)
{
	mmu_context_tables[core_id()][0] = val >> 4 | PTE_PTD;
	tlbflush();
}

static __inline uint32_t
rcr3(void)
{
	return (mmu_context_tables[core_id()][0] & ~0x3) << 4;
}

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_X86_H */
