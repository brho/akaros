#pragma once

#include <parlib/common.h>
#include <parlib/parlib.h>
#include <parlib/stdio.h>

__BEGIN_DECLS

void trace_printf(const char *fmt, ...);

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)

/* user/parlib/hexdump.c */
void hexdump(FILE *f, void *v, int length);

void fprintf_hw_tf(FILE *f, struct hw_trapframe *hw_tf);
void fprintf_sw_tf(FILE *f, struct sw_trapframe *sw_tf);
void fprintf_vm_tf(FILE *f, struct vm_trapframe *vm_tf);
void print_user_context(struct user_context *ctx);

__END_DECLS
