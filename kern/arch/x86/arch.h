#ifndef ROS_INC_ARCH_H
#define ROS_INC_ARCH_H

#include <ros/arch/arch.h>
#include <ros/common.h>
#include <arch/x86.h>

/* Arch Constants */
#define ARCH_CL_SIZE				 64

#define __always_inline __attribute__((always_inline))
static inline void breakpoint(void) __attribute__((always_inline));
static inline void invlpg(void *addr) __attribute__((always_inline));  
static inline void tlbflush(void) __attribute__((always_inline));
static inline void icache_flush_page(void *va, void *kva)
              __attribute__((always_inline));
static inline uint64_t read_tsc(void) __attribute__((always_inline));
static inline uint64_t read_tscp(void) __attribute__((always_inline));
static inline uint64_t read_tsc_serialized(void) __attribute__((always_inline));
static inline void enable_irq(void) __attribute__((always_inline));
static inline void disable_irq(void) __attribute__((always_inline));
static inline void enable_irqsave(int8_t *state) __attribute__((always_inline));
static inline void disable_irqsave(int8_t *state)
              __attribute__((always_inline));
static inline void cpu_relax(void) __attribute__((always_inline));
static inline void cpu_halt(void) __attribute__((always_inline));
static inline void clflush(uintptr_t* addr) __attribute__((always_inline));
static inline int irq_is_enabled(void) __attribute__((always_inline));
static inline void cache_flush(void) __attribute__((always_inline));
static inline void reboot(void)
              __attribute__((always_inline)) __attribute__((noreturn));

/* in trap.c */
void send_ipi(uint32_t os_coreid, uint8_t vector);
/* in cpuinfo.c */
void print_cpuinfo(void);
void show_mapping(uintptr_t start, size_t size);
int vendor_id(char *);

static inline void breakpoint(void)
{
	asm volatile("int3");
}

static inline void invlpg(void *addr)
{ 
	asm volatile("invlpg (%0)" : : "r" (addr) : "memory");
}  

static inline void tlbflush(void)
{
	unsigned long cr3;
	asm volatile("mov %%cr3,%0" : "=r" (cr3));
	asm volatile("mov %0,%%cr3" : : "r" (cr3));
}

static inline void icache_flush_page(void *va, void *kva)
{
	// x86 handles self-modifying code (mostly) without SW support
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

/* Check out k/a/x86/rdtsc_test.c for more info */
static inline uint64_t read_tsc_serialized(void)
{
	asm volatile("lfence" ::: "memory");	/* mfence on amd? */
	return read_tsc();
}

static inline void enable_irq(void)
{
	asm volatile("sti");
}

static inline void disable_irq(void)
{
	asm volatile("cli");
}

static inline void enable_irqsave(int8_t *state)
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

static inline void disable_irqsave(int8_t *state)
{
	if ((*state == 0) && irq_is_enabled())
		disable_irq();
	else 
		(*state)--;
}

static inline void cpu_relax(void)
{
	__cpu_relax();
}

/* This doesn't atomically enable interrupts and then halt, like we want, so
 * x86 needs to use a custom helper in the irq handler in trap.c. */
static inline void cpu_halt(void)
{
	asm volatile("sti; hlt" : : : "memory");
}

static inline void clflush(uintptr_t* addr)
{
	asm volatile("clflush %0" : : "m"(*addr));
}

static inline int irq_is_enabled(void)
{
	return read_flags() & FL_IF;
}

static inline void cache_flush(void)
{
	wbinvd();
}

static inline void reboot(void)
{
	uint8_t cf9 = inb(0xcf9) & ~6;
	outb(0x92, 0x3);
	outb(0xcf9, cf9 | 2);
	outb(0xcf9, cf9 | 6);
	asm volatile ("mov $0, %"X86_REG_SP"; int $0");
	while (1);
}

#endif /* !ROS_INC_ARCH_H */
