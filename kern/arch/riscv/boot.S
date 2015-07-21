/* See COPYRIGHT for copyright information. */

#include <arch/pcr.h>
#include <ros/arch/arch.h>
#include <ros/memlayout.h>
#incldue <arch/mmu.h>

///////////////////////////////////////////////////////////////////
// The kernel (this code) is linked at address 0xFFFFFFFF80000000,
// but we tell the bootloader to load it at physical address 
// 0x00000000, which is the start of extended memory.
// (See kernel.ld)
///////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////
// entry point
///////////////////////////////////////////////////////////////////

#define PCR0 (SR_S | SR_S64 | (1 << (IRQ_IPI + SR_IM_SHIFT)))
.text

.global _start
_start:
  // This is the first kernel code that executes; it is run only by core 0.

  // set up trap entry point.  this is not a relocated address, as we
  // do not support trapping before the MMU is set up.
  la     a6, trap_entry
  mtpcr  a6, ASM_CR(PCR_EVEC)

  // clear IPIs and enable traps
  mtpcr  zero, ASM_CR(PCR_CLR_IPI)
  li     a6, PCR0
  mtpcr  a6, ASM_CR(PCR_SR)

  // core 0?
  mfpcr  a6, ASM_CR(PCR_COREID)
  bnez   a6, notcore0

  // terminate frame pointer for backtracing and set up stack
  li     s0, 0
  la     sp, percore_stacks + KSTKSIZE
  li     a7, KERN_LOAD_ADDR
  sub    sp, sp, a7

  // get memory size and core count from first two words of memory
  lw     s0, 0(zero)
  lw     s1, 4(zero)

  // set up initial page mappings
  move   a0, s0
  la     a1, l1pt
  sub    a1, a1, a7
  la     a2, l1pt_boot
  sub    a2, a2, a7
#ifdef __riscv64
  la     a3, l2pt
  sub    a3, a3, a7
#endif
  jal    pagetable_init
  jal    enable_mmu

  // relocate stack and call into C code using absolute jump, not pc-relative
  move   a0, s0
  move   a1, s1
  la     sp, percore_stacks + KSTKSIZE
  la     a6, cmain
  jr     a6

notcore0:
  // wait for core 0 to boot
  la     a8, num_cores_booted - KERN_LOAD_ADDR
1:lw     a9, 0(a8)
  beqz   a9, 1b

  // if for some reason coreid >= num_cores, don't boot this core
  la     a8, num_cores - KERN_LOAD_ADDR
  lw     a8, 0(a8)
1:bgeu   a6, a8, 1b

  // set up stack: sp = percoore_stacks+(core_id()+1)*KSTKSIZE
  la     sp, percore_stacks
  add    a6, a6, 1
  sll    a6, a6, KSTKSHIFT
  add    sp, sp, a6
  li     a7, KERN_LOAD_ADDR
  sub    sp, sp, a7
  
  jal    enable_mmu

  // relocate stack and call into C code
  li     a7, KERN_LOAD_ADDR
  add    sp, sp, a7
  la     a6, smp_init
  jr     a6

enable_mmu:
  la     a6, l1pt_boot
  li     a7, KERN_LOAD_ADDR
  sub    a6, a6, a7
  mtpcr  a6, ASM_CR(PCR_PTBR)
  li     a6, PCR0 | SR_VM
  mtpcr  a6, ASM_CR(PCR_SR)
  ret

///////////////////////////////////////////////////////////////////
// boot stack and regular stacks.
// (boot stack cannot be in .bss, as .bss is later zereoed by the kernel.)
///////////////////////////////////////////////////////////////////

.data
  .align  PGSHIFT
l1pt_boot:
  .space  PGSIZE
.global l1pt
l1pt:
  .space  PGSIZE
l2pt:
  .space  PGSIZE

  .global percore_stacks
percore_stacks:
  .space  KSTKSIZE*MAX_NUM_CORES
