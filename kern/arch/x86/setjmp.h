// Arch specific struct definitions for setjmp/longjmp.

#ifndef ROS_ARCH_SETJMP_H
#define ROS_ARCH_SETJMP_H

#include <common.h>

#ifdef __x86_64__
struct jmpbuf {
	uintptr_t retaddr; // return address
	uintreg_t rsp;     // post-return rsp
	uintreg_t rbx;     // callee saved registers
	uintreg_t rbp;
	uintreg_t r12;
	uintreg_t r13;
	uintreg_t r14;
	uintreg_t r15;
};
#else
struct jmpbuf {
	uintptr_t retaddr; // return address
 	uintreg_t esp;     // post-return esp
 	uintreg_t ebx;     // callee saved registers
	uintreg_t ebp;
	uintreg_t esi;
	uintreg_t edi;
};
#endif

#endif /* !ROS_ARCH_SETJMP_H */
