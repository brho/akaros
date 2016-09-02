// Arch specific struct definitions for setjmp/longjmp.

#pragma once

#include <common.h>

#ifdef __x86_64__
struct jmpbuf {
	uintptr_t retaddr; // return address
	uintreg_t rsp;     // post-return rsp
	uintreg_t rbp;
};

static inline void __ros_clobber_callee_regs(void)
{
	asm volatile ("" : : : "rbx", "r12", "r13", "r14", "r15");
}

static inline uintptr_t jmpbuf_get_pc(struct jmpbuf *jb)
{
	return jb->retaddr;
}

static inline uintptr_t jmpbuf_get_fp(struct jmpbuf *jb)
{
	return jb->rbp;
}

#else

struct jmpbuf {
	uintptr_t retaddr; // return address
 	uintreg_t esp;     // post-return esp
	uintreg_t ebp;
};

static inline __ros_clobber_callee_regs(void)
{
	asm volatile ("" : : : "ebx", "esi", "edi");
}

static inline uintptr_t jmpbuf_get_pc(struct jmpbuf *jb)
{
	return jb->retaddr;
}

static inline uintptr_t jmpbuf_get_fp(struct jmpbuf *jb)
{
	return jb->ebp;
}

#endif
