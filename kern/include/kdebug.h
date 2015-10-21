#pragma once

#include <ros/common.h>
#include <ros/trapframe.h>
#include <arch/kdebug.h>
#include <profiler.h>

struct symtab_entry {
	char *name;
	uintptr_t addr;
};

#define TRACEME() trace_printk(TRUE, "%s(%d)", __FILE__, __LINE__)

void backtrace(void);
void gen_backtrace_frame(uintptr_t eip, uintptr_t ebp,
						 void (*pfunc)(void *, const char *), void *opaque);
void gen_backtrace(void (*pfunc)(void *, const char *), void *opaque);
void backtrace_frame(uintptr_t pc, uintptr_t fp);
size_t backtrace_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
                      size_t nr_slots);
size_t user_backtrace_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
						   size_t nr_slots);
void backtrace_kframe(struct hw_trapframe *hw_tf);
/* for includes */ struct proc;
void backtrace_user_ctx(struct proc *p, struct user_context *ctx);

/* Arch dependent, listed here for ease-of-use */
static inline uintptr_t get_caller_pc(void);

/* Returns a null-terminated string with the function name for a given PC /
 * instruction pointer.  kfree() the result. */
char *get_fn_name(uintptr_t pc);

/* Returns the address of sym, or 0 if it does not exist */
uintptr_t get_symbol_addr(char *sym);

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)
void hexdump(void *v, int length);
void pahexdump(uintptr_t pa, int length);
int printdump(char *buf, int buflen, uint8_t *data);

extern bool printx_on;
void set_printx(int mode);
#define printx(args...)							\
	do {											\
		if (printx_on)								\
			printk(args);							\
	} while (0)
#define trace_printx(args...)						\
	do {											\
		if (printx_on)								\
			trace_printk(TRUE, args);				\
	} while (0)

void debug_addr_proc(struct proc *p, unsigned long addr);
void debug_addr_pid(int pid, unsigned long addr);
