#ifndef ROS_INC_ARCH_H
#define ROS_INC_ARCH_H

#include <arch/x86.h>
#include <arch/types.h>
#include <arch/apic.h>
#include <arch/trap.h>

/* Arch Constants */
#define MAX_NUM_CPUS				255

static __inline void breakpoint(void) __attribute__((always_inline));
static __inline void invlpg(void *SNT addr) __attribute__((always_inline));
static __inline void tlbflush(void) __attribute__((always_inline));
static __inline uint64_t read_tsc(void) __attribute__((always_inline));
static __inline uint64_t read_tsc_serialized(void) __attribute__((always_inline));
static __inline void enable_irq(void) __attribute__((always_inline));
static __inline void disable_irq(void) __attribute__((always_inline));
static __inline void enable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void disable_irqsave(int8_t* state) __attribute__((always_inline));
static __inline void cpu_relax(void) __attribute__((always_inline));
static __inline void cpu_halt(void) __attribute__((always_inline));
static __inline void clflush(uintptr_t* addr) __attribute__((always_inline));
static __inline int irq_is_enabled(void) __attribute__((always_inline));
static __inline uint32_t core_id(void) __attribute__((always_inline));
static __inline void cache_flush(void) __attribute__((always_inline));
static __inline void reboot(void) __attribute__((always_inline)) __attribute__((noreturn));

void print_cpuinfo(void);
void show_mapping(uintptr_t start, size_t size);
void backtrace(void);

static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}

static __inline void 
invlpg(void *addr)
{ 
	__asm __volatile("invlpg (%0)" : : "r" (addr) : "memory");
}  

static __inline void
tlbflush(void)
{
	uint32_t cr3;
	__asm __volatile("movl %%cr3,%0" : "=r" (cr3));
	__asm __volatile("movl %0,%%cr3" : : "r" (cr3));
}

static __inline uint64_t
read_tsc(void)
{
	uint64_t tsc;
	__asm __volatile("rdtsc" : "=A" (tsc));
	return tsc;
}

static __inline uint64_t 
read_tsc_serialized(void)
{
    uint64_t tsc;
	cpuid(0, 0, 0, 0, 0);
	tsc = read_tsc();
	return tsc;
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

static __inline void
cpu_relax(void)
{
	// in case the compiler doesn't serialize for pause, the "m" will make sure
	// no memory is reordered around this instruction.
	asm volatile("pause" : : : "memory");
}

static __inline void
cpu_halt(void)
{
	asm volatile("hlt" : : : "memory");
}

static __inline void
clflush(uintptr_t* addr) __attribute__((always_inline))
{
	asm volatile("clflush %0" : : "m"(*addr));
}

static __inline int
irq_is_enabled(void)
{
	return read_eflags() & FL_IF;
}

static __inline uint32_t
( core_id)(void)
{
	return lapic_get_id();
}

static __inline void
cache_flush(void)
{
        wbinvd();
}

static __inline void
reboot(void)
{
	outb(0x92, 0x3);
	asm volatile ("movl $0, %esp; int $0");
	while(1);
}

#endif /* !ROS_INC_X86_H */
