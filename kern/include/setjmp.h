// Kernel implementations for setjmp/longjmp.

#ifndef ROS_SETJMP_H
#define ROS_SETJMP_H

#include <arch/setjmp.h>

int setjmp(struct jmpbuf *env) __attribute__((returns_twice));
void longjmp(struct jmpbuf *env, void * val) __attribute__((noreturn));

#endif /* !ROS_SETJMP_H */
