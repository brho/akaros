/* See COPYRIGHT for copyright information. */

#include <arch/pcr.h>
#include <ros/arch/arch.h>
#include <ros/memlayout.h>

///////////////////////////////////////////////////////////////////
// The kernel (this code) is linked at address (KERNBASE + 0x00000000),
// but we tell the bootloader to load it at physical address 
// 0x00000000, which is the start of extended memory.
// (See kernel.ld)
///////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////
// entry point
///////////////////////////////////////////////////////////////////

.text

.global _start
.ent    _start
_start:
  // This is the first kernel code that executes; it is run only by core 0.

  // set up stack and terminate frame pointer for backtracing
  li     $fp, 0
  la     $sp, bootstacktop
	li     $t1, KERNBASE
	sub    $sp, $sp, $t1

  // set up trap entry point
  la     $t0, trap_entry
	sub    $t0, $t0, $t1
  mtpcr  $t0, ASM_CR(PCR_EVEC)

  // enable traps
  li     $t0, SR_S | SR_ET | SR_SX
  mtpcr  $t0, ASM_CR(PCR_SR)

	jal    mmu_init

  // relocate stack and call into C code using absolute jump, not pc-relative
  la     $sp, bootstacktop
	lui    $t0, %hi(cmain)
	jalr.j $t0, %lo(cmain)

.end    _start

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
