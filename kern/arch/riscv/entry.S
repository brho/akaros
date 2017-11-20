#include <arch/pcr.h>
#include <arch/trap.h>
#include <kstack.h>

#ifdef __riscv64
# define STORE    sd
# define LOAD     ld
# define LOG_REGBYTES 3
#else
# define STORE    sw
# define LOAD     lw
# define LOG_REGBYTES 2
#endif
#define REGBYTES (1 << LOG_REGBYTES)

  .text
  .global save_kernel_tf_asm
save_kernel_tf_asm:
  mfpcr  a1,ASM_CR(PCR_SR)

  STORE s0,  2*REGBYTES(a0)
  STORE s1,  3*REGBYTES(a0)
  STORE s2,  4*REGBYTES(a0)
  STORE s3,  5*REGBYTES(a0)
  STORE s4,  6*REGBYTES(a0)
  STORE s5,  7*REGBYTES(a0)
  STORE s6,  8*REGBYTES(a0)
  STORE s7,  9*REGBYTES(a0)
  STORE s8, 10*REGBYTES(a0)
  STORE s9, 11*REGBYTES(a0)
  STORE s10,12*REGBYTES(a0)
  STORE s11,13*REGBYTES(a0)
  STORE sp, 14*REGBYTES(a0)

  STORE  a1,32*REGBYTES(a0)

  # set EPC to this function's return address
  STORE  ra,33*REGBYTES(a0)
  ret

# Remove these (or this comment) when implementing setjmp on riscv.
  .text
  .global pop_kernel_ctx
pop_kernel_ctx:
  LOAD  a1,32*REGBYTES(a0)
  LOAD  ra,33*REGBYTES(a0)

  LOAD  s0,  2*REGBYTES(a0)
  LOAD  s1,  3*REGBYTES(a0)
  LOAD  s2,  4*REGBYTES(a0)
  LOAD  s3,  5*REGBYTES(a0)
  LOAD  s4,  6*REGBYTES(a0)
  LOAD  s5,  7*REGBYTES(a0)
  LOAD  s6,  8*REGBYTES(a0)
  LOAD  s7,  9*REGBYTES(a0)
  LOAD  s8, 10*REGBYTES(a0)
  LOAD  s9, 11*REGBYTES(a0)
  LOAD  s10,12*REGBYTES(a0)
  LOAD  s11,13*REGBYTES(a0)
  LOAD  sp, 14*REGBYTES(a0)

  mtpcr  a1,ASM_CR(PCR_SR)
  ret


save_tf:  # write the trap frame onto the stack

  ret

  .globl  pop_hw_tf
pop_hw_tf:  # write the trap frame onto the stack
  # restore SR.{PS, EF, U64} and disable interrupts
  LOAD  v0,32*REGBYTES(a0)
  mfpcr v1, ASM_CR(PCR_SR)
  andi  v0, v0,  (SR_PS | SR_EF | SR_U64)
  andi  v1, v1, ~(SR_PS | SR_EF | SR_U64 | SR_ET)
  or    v0, v0, v1
  mtpcr v0, ASM_CR(PCR_SR)

  # restore gprs
  LOAD  x1,1*REGBYTES(a0)
  mtpcr  x1,ASM_CR(PCR_K0)
  LOAD  x1,2*REGBYTES(a0)
  mtpcr x1,ASM_CR(PCR_K1)
  move  x1,a0
  LOAD  x3,3*REGBYTES(x1)
  LOAD  x4,4*REGBYTES(x1)
  LOAD  x5,5*REGBYTES(x1)
  LOAD  x6,6*REGBYTES(x1)
  LOAD  x7,7*REGBYTES(x1)
  LOAD  x8,8*REGBYTES(x1)
  LOAD  x9,9*REGBYTES(x1)
  LOAD  x10,10*REGBYTES(x1)
  LOAD  x11,11*REGBYTES(x1)
  LOAD  x12,12*REGBYTES(x1)
  LOAD  x13,13*REGBYTES(x1)
  LOAD  x14,14*REGBYTES(x1)
  LOAD  x15,15*REGBYTES(x1)
  LOAD  x16,16*REGBYTES(x1)
  LOAD  x17,17*REGBYTES(x1)
  LOAD  x18,18*REGBYTES(x1)
  LOAD  x19,19*REGBYTES(x1)
  LOAD  x20,20*REGBYTES(x1)
  LOAD  x21,21*REGBYTES(x1)
  LOAD  x22,22*REGBYTES(x1)
  LOAD  x23,23*REGBYTES(x1)
  LOAD  x24,24*REGBYTES(x1)
  LOAD  x25,25*REGBYTES(x1)
  LOAD  x26,26*REGBYTES(x1)
  LOAD  x27,27*REGBYTES(x1)
  LOAD  x28,28*REGBYTES(x1)
  LOAD  x29,29*REGBYTES(x1)
  LOAD  x30,30*REGBYTES(x1)
  LOAD  x31,31*REGBYTES(x1)

  # gtfo!
  LOAD  x2,33*REGBYTES(x1)
  mtpcr x2,ASM_CR(PCR_EPC)
  mfpcr x1,ASM_CR(PCR_K0)
  mfpcr x2,ASM_CR(PCR_K1)
  eret

  .global  trap_entry
trap_entry:
  mtpcr x1, ASM_CR(PCR_K0)  # stash x1 in k0
  mfpcr x1, ASM_CR(PCR_SR)
  mtpcr x2, ASM_CR(PCR_K1)  # stash x2 in k1
  # when coming from kernel, continue below its stack
  add   x2, sp, -SIZEOF_HW_TRAPFRAME
  and   x1, x1, SR_PS
  bnez  x1, 1f

  # otherwise, start at the top of the per-core stack
  mfpcr x1, ASM_CR(PCR_COREID)
  lui	x2, %hi(core_stacktops)
  sll   x1, x1, LOG_REGBYTES
  add   x2, x2, x1
  LOAD  x2, %lo(core_stacktops)(x2)
  add   x2, x2, -SIZEOF_HW_TRAPFRAME

1:# save gprs
  STORE  x3,3*REGBYTES(x2)
  STORE  x4,4*REGBYTES(x2)
  mfpcr  x3,ASM_CR(PCR_K0)    # retrieve x1
  mfpcr  x4,ASM_CR(PCR_K1)    # retrieve x2
  STORE  x5,5*REGBYTES(x2)
  STORE  x6,6*REGBYTES(x2)
  STORE  x7,7*REGBYTES(x2)
  STORE  x8,8*REGBYTES(x2)
  STORE  x9,9*REGBYTES(x2)
  STORE  x3,1*REGBYTES(x2)    # save x1
  STORE  x4,2*REGBYTES(x2)    # save x2
  STORE  x10,10*REGBYTES(x2)
  STORE  x11,11*REGBYTES(x2)
  STORE  x12,12*REGBYTES(x2)
  STORE  x13,13*REGBYTES(x2)
  STORE  x14,14*REGBYTES(x2)
  STORE  x15,15*REGBYTES(x2)
  STORE  x16,16*REGBYTES(x2)
  STORE  x17,17*REGBYTES(x2)
  STORE  x18,18*REGBYTES(x2)
  STORE  x19,19*REGBYTES(x2)
  STORE  x20,20*REGBYTES(x2)
  STORE  x21,21*REGBYTES(x2)
  STORE  x22,22*REGBYTES(x2)
  STORE  x23,23*REGBYTES(x2)
  STORE  x24,24*REGBYTES(x2)
  STORE  x25,25*REGBYTES(x2)
  STORE  x26,26*REGBYTES(x2)
  STORE  x27,27*REGBYTES(x2)
  STORE  x28,28*REGBYTES(x2)
  STORE  x29,29*REGBYTES(x2)
  STORE  x30,30*REGBYTES(x2)
  STORE  x31,31*REGBYTES(x2)

  # get sr, epc, badvaddr, cause
  mfpcr  x3,ASM_CR(PCR_SR)
  mfpcr  x4,ASM_CR(PCR_EPC)
  mfpcr  x5,ASM_CR(PCR_BADVADDR)
  mfpcr  x6,ASM_CR(PCR_CAUSE)
  STORE  x3,32*REGBYTES(x2)
  STORE  x4,33*REGBYTES(x2)
  STORE  x5,34*REGBYTES(x2)
  STORE  x6,35*REGBYTES(x2)

  move  sp, x2
  move  a0, x2
  j     handle_trap

  .global  cpu_halt
  .global  after_cpu_halt
cpu_halt:
  setpcr ASM_CR(PCR_SR), SR_ET
1:b     1b   # handle_ipi can advance the PC to break out of this loop.
  ret
after_cpu_halt:
  clearpcr ASM_CR(PCR_SR), SR_ET
