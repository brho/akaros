#include <arch/pcr.h>
#include <arch/trap.h>
#include <ros/memlayout.h>

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
  .ent    save_kernel_tf_asm
  .global save_kernel_tf_asm
save_kernel_tf_asm:
  STORE  s0,20*REGBYTES(a0)
  STORE  s1,21*REGBYTES(a0)
  STORE  s2,22*REGBYTES(a0)
  STORE  s3,23*REGBYTES(a0)
  STORE  s4,24*REGBYTES(a0)
  STORE  s5,25*REGBYTES(a0)
  STORE  s6,26*REGBYTES(a0)
  STORE  s7,27*REGBYTES(a0)
  STORE  s8,28*REGBYTES(a0)
  STORE  s9,29*REGBYTES(a0)
  STORE  sp,30*REGBYTES(a0)

  mfpcr  t0,ASM_CR(PCR_SR)
  STORE  t0,32*REGBYTES(a0)

  # set EPC to this function's return address
  STORE  ra,33*REGBYTES(a0)
  ret
  .end  save_kernel_tf_asm

  .text
  .ent    pop_kernel_tf
  .global pop_kernel_tf
pop_kernel_tf:
  LOAD  t0,32*REGBYTES(a0)
  LOAD  ra,33*REGBYTES(a0)

  LOAD  s0,20*REGBYTES(a0)
  LOAD  s1,21*REGBYTES(a0)
  LOAD  s2,22*REGBYTES(a0)
  LOAD  s3,23*REGBYTES(a0)
  LOAD  s4,24*REGBYTES(a0)
  LOAD  s5,25*REGBYTES(a0)
  LOAD  s6,26*REGBYTES(a0)
  LOAD  s7,27*REGBYTES(a0)
  LOAD  s8,28*REGBYTES(a0)
  LOAD  s9,29*REGBYTES(a0)
  LOAD  sp,30*REGBYTES(a0)

  mtpcr  t0,ASM_CR(PCR_SR)
  ret

  .end  pop_kernel_tf

  .ent  save_tf
save_tf:  # write the trap frame onto the stack

  ret
  .end  save_tf

  .globl  env_pop_tf
  .ent  env_pop_tf
env_pop_tf:  # write the trap frame onto the stack
  # restore gprs
  LOAD  t0,32*REGBYTES(a0)  # restore sr (should disable interrupts)
  mfpcr t1, ASM_CR(PCR_SR)
  andi  t1, t1, ~(SR_PS | SR_EF | SR_U64)
  andi  t0, t0, SR_PS | SR_EF | SR_U64
  or    t0, t0, t1
  mtpcr t0, ASM_CR(PCR_SR)

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
  .end  env_pop_tf

  .global  trap_entry
  .ent  trap_entry
trap_entry:
  # save x1 and x2 so we can use them as temporaries
  mtpcr x1, ASM_CR(PCR_K0)
  mtpcr x2, ASM_CR(PCR_K1)

  # when coming from kernel, continue below its stack
  mfpcr x1, ASM_CR(PCR_SR)
  and   x1, x1, SR_PS
  add   x2, sp, -SIZEOF_TRAPFRAME_T
  bnez  x1, 1f

  # otherwise, start at the top of the per-core stack
  la    x2, core_stacktops
  mfpcr x1, ASM_CR(PCR_COREID)
  sll   x1, x1, LOG_REGBYTES
  add   x2, x2, x1
  LOAD  x2, 0(x2)
  add   x2, x2, -SIZEOF_TRAPFRAME_T

1:# save gprs
  STORE  x3,3*REGBYTES(x2)
  STORE  x4,4*REGBYTES(x2)
  STORE  x5,5*REGBYTES(x2)
  STORE  x6,6*REGBYTES(x2)
  STORE  x7,7*REGBYTES(x2)
  STORE  x8,8*REGBYTES(x2)
  STORE  x9,9*REGBYTES(x2)
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

  mfpcr  x3,ASM_CR(PCR_K0)
  STORE  x3,1*REGBYTES(x2)          # x1 is in PCR_K0
  mfpcr  x3,ASM_CR(PCR_K1)
  STORE  x3,2*REGBYTES(x2)          # x2 is in PCR_K1

  # get sr, epc, badvaddr, cause
  mfpcr  x3,ASM_CR(PCR_SR)          # sr
  STORE  x3,32*REGBYTES(x2)
  mfpcr  x4,ASM_CR(PCR_EPC)          # epc
  STORE  x4,33*REGBYTES(x2)
  mfpcr  x3,ASM_CR(PCR_BADVADDR)      # badvaddr
  STORE  x3,34*REGBYTES(x2)
  mfpcr  x3,ASM_CR(PCR_CAUSE)        # cause
  STORE  x3,35*REGBYTES(x2)

  li    s9, 0
  move  sp, x2
  move  a0, x2
  j     handle_trap
  .end  trap_entry

  .global  cpu_halt
  .global  after_cpu_halt
  .ent  cpu_halt
cpu_halt:
  setpcr ASM_CR(PCR_SR), SR_ET
1:b     1b   # handle_ipi can advance the PC to break out of this loop.
  ret
after_cpu_halt:
  .end  cpu_halt
