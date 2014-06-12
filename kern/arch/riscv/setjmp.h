// Arch specific struct definitions for setjmp/longjmp.
// TODO: Implement for riscv

#ifndef ROS_ARCH_SETJMP_H
#define ROS_ARCH_SETJMP_H

#warning "No jmpbuf/setjmp/longjmp!"
struct jmpbuf {
};
static inline void __ros_clobber_callee_regs(void)
{
}

#endif /* !ROS_ARCH_SETJMP_H */
