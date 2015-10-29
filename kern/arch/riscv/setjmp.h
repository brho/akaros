// Arch specific struct definitions for setjmp/longjmp.
// TODO: Implement for riscv

#pragma once

#warning "No jmpbuf/setjmp/longjmp!"
struct jmpbuf {
};
static inline void __ros_clobber_callee_regs(void)
{
}
