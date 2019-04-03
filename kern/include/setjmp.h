// Kernel implementations for setjmp/longjmp.

#pragma once

#include <arch/setjmp.h>

int slim_setjmp(struct jmpbuf *env) __attribute__((returns_twice));
void longjmp(struct jmpbuf *env, int val) __attribute__((noreturn));

#pragma GCC diagnostic push
/* Currently, this only throws in tcpackproc().  Not sure why, but if you take
 * out the loop++ > 1000, it won't warn. */
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

#define setjmp(jb) ({ __ros_clobber_callee_regs(); slim_setjmp(jb); })

#pragma GCC diagnostic pop
