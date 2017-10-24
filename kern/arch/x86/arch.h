#pragma once

#include <ros/arch/arch.h>
#include <ros/common.h>
#include <arch/x86.h>

/* Arch Constants */
#define ARCH_CL_SIZE				 64

static inline void breakpoint(void) __attribute__((always_inline));
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
static inline void clflush(uintptr_t* addr) __attribute__((always_inline));
static inline int irq_is_enabled(void) __attribute__((always_inline));
static inline void cache_flush(void) __attribute__((always_inline));
static inline void reboot(void)
              __attribute__((always_inline)) __attribute__((noreturn));
static inline void prefetch(void *addr);
static inline void prefetchw(void *addr);
static inline void swap_gs(void);
static inline void __attribute__((noreturn))
__reset_stack_pointer(void *arg, uintptr_t sp, void (*f)(void *));

/* in trap.c */
void send_ipi(uint32_t os_coreid, uint8_t vector);
/* in cpuinfo.c */
int x86_family, x86_model, x86_stepping;
void print_cpuinfo(void);
void show_mapping(pgdir_t pgdir, uintptr_t start, size_t size);
int vendor_id(char *);
/* pmap.c */
void invlpg(void *addr);
void tlbflush(void);
void tlb_flush_global(void);
/* idle.c */
void cpu_halt(void);

static inline void breakpoint(void)
{
	asm volatile("int3");
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

static inline void mwait(void *eax)
{
	asm volatile("xorq %%rcx, %%rcx;"
	             "xorq %%rdx, %%rdx;"
	             "monitor;"
				 /* this is racy, generically.  we never check if the write to
				  * the monitored address happened already. */
	             "movq $0, %%rax;"	/* c-state hint.  this is C1 */
	             "mwait;"
	             : : "a"(eax));
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

static inline void prefetch(void *addr)
{
	asm volatile("prefetchnta (%0)" : : "r"(addr));
}

static inline void prefetchw(void *addr)
{
	asm volatile("prefetchw (%0)" : : "r"(addr));
}

/* Guest VMs have a maximum physical address they can use.  Guest
 * physical addresses are mapped into this MCP 1:1, but limited to
 * this max address *in hardware*.  I.e., the MCP process can address
 * more memory than the VMMCP can.  This is great; it means that
 * keeping VM management stuff separate from the VM is trivial: just
 * map it above max_vm_address. There's no need, as in other systems,
 * to tweak the page table or root pointer to protect management
 * memory from VM memory.
 *
 * TODO: read a register the first time this is called and save it
 * away.  But this is more than enough for now.
 */
static inline uint64_t max_guest_pa(void)
{
	return (1ULL<<40) - 1;
}

static inline void swap_gs(void)
{
	asm volatile ("swapgs");
}

/* Resets a stack pointer to sp, then calls f(arg) */
static inline void __attribute__((noreturn))
__reset_stack_pointer(void *arg, uintptr_t sp, void (*f)(void *))
{
	/* FP must be zeroed before SP.  Ideally, we'd do both atomically.  If we
	 * take an IRQ/NMI in between and set SP first, then a backtrace would be
	 * confused since FP points *below* the SP that the *IRQ handler* is now
	 * using.  By zeroing FP first, at least we won't BT at all (though FP is
	 * still out of sync with SP). */
	asm volatile ("mov $0x0, %%rbp;"
	              "mov %0, %%rsp;"
	              "jmp *%%rdx;"
	              : : "q"(sp), "D"(arg), "d"(f));
	while (1);
}
