#pragma once

#include <ros/common.h>
#include <ros/trapframe.h>
#include <arch/kdebug.h>
#include <profiler.h>
/* for includes */ struct proc;

struct symtab_entry {
	const char *name;
	uintptr_t addr;
};

/* An alternative here is to have backtrace_list kmalloc an array.  The downside
 * is that we're calling into the allocator in potentially-delicate situations,
 * such as the NMI handler. */
#define MAX_BT_DEPTH 20

/*** Printk Backtraces, usually used for debugging or from the monitor */
/* Backtraces the calling kernel context */
void backtrace(void);
/* Backtraces a PC/FP, with no protections */
void backtrace_frame(uintptr_t pc, uintptr_t fp);
/* Backtraces a user PC/FP */
void backtrace_user_frame(uintptr_t pc, uintptr_t fp);
/* Backtraces a hardware TF.  Can handle user or kernel TFs */
void backtrace_hwtf(struct hw_trapframe *hw_tf);
/* Backtraces a user context */
void backtrace_user_ctx(struct proc *p, struct user_context *ctx);
/* Backtraces the current user context, if there is one */
void backtrace_current_ctx(void);

/*** Programmatic Backtraces */
/* Backtraces a PC/FP, stores results in *pcs, with no protections */
size_t backtrace_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
                      size_t nr_slots);
/* Backtraces a user PC/FP, stores results in *pcs */
size_t backtrace_user_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
						   size_t nr_slots);
/* Prints out a backtrace list, using pfunc(opaque, "line") for the printk.
 * This does a symbol lookup on the kernel binary, so it is less useful for a
 * user backtrace. */
void print_backtrace_list(uintptr_t *pcs, size_t nr_pcs,
						  void (*pfunc)(void *, const char *), void *opaque);
/* Backtraces the calling kernel context, using pfunc for printing */
void gen_backtrace(void (*pfunc)(void *, const char *), void *opaque);

/* Arch dependent, listed here for ease-of-use */
static inline uintptr_t get_caller_pc(void);

/* Returns a null-terminated string with the function name for a given PC /
 * instruction pointer.  Returns NULL on failure. */
const char *get_fn_name(uintptr_t pc);

/* Returns the address of sym, or 0 if it does not exist */
uintptr_t get_symbol_addr(char *sym);

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)
void hexdump(void *v, int length);
void pahexdump(uintptr_t pa, int length);
int printdump(char *buf, int numprint, int buflen, uint8_t *data);

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
			trace_printk(args);				\
	} while (0)

void debug_addr_proc(struct proc *p, unsigned long addr);
void debug_addr_pid(int pid, unsigned long addr);

void px_lock(void);
void px_unlock(void);
