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

.text

.global _start
.ent _start
_start:
  // This is the first kernel code that executes; it is run only by core 0.

  // set up trap entry point.  this is not a relocated address, as we
  // do not support trapping before the MMU is set up.
  la     $t0, trap_entry
  mtpcr  $t0, ASM_CR(PCR_EVEC)

  // enable traps
  li     $t0, SR_S | SR_ET | SR_SX
  mtpcr  $t0, ASM_CR(PCR_SR)

  // core 0?
  mfpcr  $t0, ASM_CR(PCR_COREID)
  bnez   $t0, notcore0

  // set up stack and terminate frame pointer for backtracing
  li     $fp, 0
  la     $sp, bootstacktop
  li     $t1, KERN_LOAD_ADDR
  sub    $sp, $sp, $t1

  // set up initial page mappings
  la     $a0, l1pt
  sub    $a0, $a0, $t1
  la     $a1, l1pt_boot
  sub    $a1, $a1, $t1
#ifdef __riscv64
  la     $a2, l2pt_kernbase
  sub    $a2, $a2, $t1
  la     $a3, l2pt_load
  sub    $a3, $a3, $t1
#endif
  jal    pagetable_init

  // turn on MMU
  la     $a0, l1pt_boot
  sub    $a0, $a0, $t1
  jal    mmu_init

  // relocate stack and call into C code using absolute jump, not pc-relative
  la     $sp, bootstacktop
  lui    $t0, %hi(cmain)
  jalr.j $t0, %lo(cmain)

notcore0:
  // set up stack and terminate frame pointer for backtracing
  // sp = percoore_stacks+(core_id()+1)*KSTKSIZE
  li     $fp, 0
  la     $sp, percore_stacks
  add    $t0, $t0, 1
  sll    $t0, $t0, KSTKSHIFT
  add    $sp, $sp, $t0
  li     $t1, KERN_LOAD_ADDR
  sub    $sp, $sp, $t1
  
  // turn on MMU
  la     $a0, l1pt_boot
  sub    $a0, $a0, $t1
  jal    mmu_init

  // relocate stack and call into C code
  li     $t1, KERN_LOAD_ADDR
  add    $sp, $sp, $t1
  lui    $t0, %hi(smp_init)
  jalr.j $t0, %lo(smp_init)

.end _start
///////////////////////////////////////////////////////////////////
// boot stack and regular stacks.
// (boot stack cannot be in .bss, as .bss is later zereoed by the kernel.)
///////////////////////////////////////////////////////////////////

.data
  .align  PGSHIFT
  .space  KSTKSIZE
  .global bootstacktop
bootstacktop:

.bss
  .align  PGSHIFT
  .global percore_stacks
percore_stacks:
  .space  KSTKSIZE*MAX_NUM_CPUS
