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

#define PCR0 (SR_S | SR_ET | SR_SX)
.text

.global _start
.ent _start
_start:
  // This is the first kernel code that executes; it is run only by core 0.

  // set up trap entry point.  this is not a relocated address, as we
  // do not support trapping before the MMU is set up.
  la     t0, trap_entry
  mtpcr  t0, ASM_CR(PCR_EVEC)

  // enable traps
  li     t0, PCR0
  mtpcr  t0, ASM_CR(PCR_SR)

  // core 0?
  mfpcr  t0, ASM_CR(PCR_COREID)
  bnez   t0, notcore0

  // set up stack
  la     sp, bootstacktop
  li     t1, KERN_LOAD_ADDR
  sub    sp, sp, t1

  // set up initial page mappings
  la     a0, l1pt
  sub    a0, a0, t1
  la     a1, l1pt_boot
  sub    a1, a1, t1
#ifdef __riscv64
  la     a2, l2pt
  sub    a2, a1, t1
#endif
  jal    pagetable_init
  jal    enable_mmu

  // relocate stack and call into C code using absolute jump, not pc-relative
  la     sp, bootstacktop
  la     t0, cmain
  jr     t0

notcore0:
  // set up stack: sp = percoore_stacks+(core_id()+1)*KSTKSIZE
  la     sp, percore_stacks
  add    t0, t0, 1
  sll    t0, t0, KSTKSHIFT
  add    sp, sp, t0
  li     t1, KERN_LOAD_ADDR
  sub    sp, sp, t1
  
  jal    enable_mmu

  // relocate stack and call into C code
  li     t1, KERN_LOAD_ADDR
  add    sp, sp, t1
  la     t0, smp_init
  jr     t0

.end _start

.ent enable_mmu
enable_mmu:
  la     t0, l1pt_boot
  li     t1, KERN_LOAD_ADDR
  sub    t0, t0, t1
  mtpcr  t0, ASM_CR(PCR_PTBR)
  li     t0, PCR0 | SR_VM
  mtpcr  t0, ASM_CR(PCR_SR)
  ret
.end enable_mmu

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

  .space  KSTKSIZE
  .global bootstacktop
bootstacktop:

.bss
  .align  PGSHIFT
  .global percore_stacks
percore_stacks:
  .space  KSTKSIZE*MAX_NUM_CPUS
