// Kernel implementations for setjmp/longjmp.

#pragma once

#include <arch/setjmp.h>

int slim_setjmp(struct jmpbuf *env) __attribute__((returns_twice));
void longjmp(struct jmpbuf *env, int val) __attribute__((noreturn));

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

#define setjmp(jb) ({bool err;                                                 \
                    __ros_clobber_callee_regs();                               \
                    err = slim_setjmp(jb);                                     \
                    err;})

#pragma GCC diagnostic pop
