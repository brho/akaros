// Arch specific struct definitions for setjmp/longjmp.
// TODO: Implement for riscv

#pragma once

#warning "No jmpbuf/setjmp/longjmp!"
struct jmpbuf {
};
static inline void __ros_clobber_callee_regs(void)
{
}

static inline uintptr_t jmpbuf_get_pc(struct jmpbuf *jb)
{
	return jb->retaddr;
}

static inline uintptr_t jmpbuf_get_fp(struct jmpbuf *jb)
{
	return jb->fp;
}
