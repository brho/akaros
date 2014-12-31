// Kernel implementations for setjmp/longjmp.

#ifndef ROS_SETJMP_H
#define ROS_SETJMP_H

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

#endif /* !ROS_SETJMP_H */
