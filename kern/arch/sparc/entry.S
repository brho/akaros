/* See COPYRIGHT for copyright information. */

#include <arch/mmu.h>
#include <arch/sparc.h>
#include <arch/arch.h>
#include <ros/memlayout.h>

///////////////////////////////////////////////////////////////////
// The kernel (this code) is linked at address (KERNBASE + 0x00000000),
// but we tell the bootloader to load it at physical address 
// 0x00000000, which is the start of extended memory.
// (See kernel.ld)
///////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////
// RELOC(x) maps a symbol x from its link address to its actual
// location in physical memory (its load address).	 
///////////////////////////////////////////////////////////////////
#define	RELOC(x) ((x) - KERNBASE)

///////////////////////////////////////////////////////////////////
// entry point
///////////////////////////////////////////////////////////////////

.text

.global		_start
_start:
	// This is the first code that ever executes.  It executes on all
	// cores (RAMP Gold-specific).  All we know is that PSR.S (supervisor)
	// and PSR.ET (enable traps) are both 0.  Before we can enable traps,
	// we must determine how many register windows we have, set up the
	// trap table, and set up a stack frame.

	// compute NWINDOWS

	mov	0,%wim				! mark all windows valid
	mov (PSR_S|PSR_PS),%psr	! CWP = 0
	nop; nop; nop			! 3 insns between write -> read state reg
	save					! CWP = (CWP-1) % NWINDOWS = NWINDOWS-1
	mov	%psr,%g2
	and	%g2,PSR_CWP,%g2		! g2 = NWINDOWS-1
	restore					! CWP = 0
	mov	1<<1,%wim			! mark window 1 invalid (trap on restore)

	// now g2 = NWINDOWS - 1.  Patch the window spill trap handler.
	set	RELOC(spill_patchme),%g1
	ld	[%g1],%g3
	or	%g2,%g3,%g3
	st	%g3,[%g1]
	flush	%g1

	// and patch the window fill trap handler.
	set	RELOC(fill_patchme),%g1
	ld	[%g1],%g3
	or	%g2,%g3,%g3
	st	%g3,[%g1]
	flush	%g1

	// store NWINDOWS away for safekeeping
	set	RELOC(NWINDOWS),%g1
	inc	%g2
	st	%g2,[%g1]

	// set up the TBR (trap base register)
	set	RELOC(trap_table),%g1
	mov	%g1,%tbr

	// clear frame pointer for backtrace termination
	mov	0,%fp

	// set stack pointer (-64 is space for window spill)
	// sp = bootstacktop - core_id*KSTKSIZE - 64
	set	RELOC(bootstacktop)-64,%sp
	mov	CORE_ID_REG,%g1
	sll	%g1,KSTKSHIFT,%g2
	sub	%sp,%g2,%sp

	// set up a virtual->physical mapping
	tst	%g1
	set	RELOC(pagetable_init_done),%l0
	bne pagetable_init_wait
	 nop

	// core 0 initializes the pagetable
	call	pagetable_init
	 nop
	mov	1,%g2
	st	%g2,[%l0]

pagetable_init_wait:
	ld	[%l0],%g1
	tst	%g1
	be	pagetable_init_wait
	 nop

	call	mmu_init
	 nop

	// relocate
	set	trap_table,%g1
	mov	%g1,%tbr
	set	reloc,%g1
	set	KERNBASE,%g2
	jmp	%g1
	 add	%g2,%sp,%sp
reloc:
	call	mmu_boot_cleanup_all
	 nop

	mov	1,%g1
	set	cores_relocated,%g2
	set	cores_relocated_lock,%g3
	swap	[%g3],%g1
	tst	%g1
	bne	reloc
	 nop
	ld	[%g2],%g1
	inc	%g1
	st	%g1,[%g2]
	st	%g0,[%g3]

wait_for_reloc:
	ld	[%g2],%g1
	mov	NUM_CORES_REG,%g3
	cmp	%g1,%g3
	bl	wait_for_reloc
	 nop

	// now it's safe to enable traps
	mov	%psr,%g1
	wr	%g1,PSR_ET,%psr
	nop; nop; nop

	// am i core 0?  (do i run BSD?!?)
	mov	CORE_ID_REG,%g1
	tst	%g1
	bne	4f
	 nop

	// only core 0 gets here
	call	mmu_boot_cleanup_core0
	 nop

	// set num_cpus
	set	num_cpus,%l0
	mov	NUM_CORES_REG,%l1
	st	%l1,[%l0]

	cmp	%l1,MAX_NUM_CPUS
	tg	0x7f

	// use a stack in the data section (as opposed to bss) here,
	// since kernel_init will zero the stack
	set	core0_bootstacktop-64,%sp

	sub	%sp,64,%sp		! 64 >= sizeof(multiboot_header_t)
	call	build_multiboot_info
	 add	%sp,64,%o0

	// kernel_init time!
	// first arg is pointer to multiboot_info_t, but kernel_init
	// expects it to be a pre-relocation address, so lop off KERNBASE
	set	KERNBASE,%l0
	add	%sp,64,%o0
	call	kernel_init
	 sub	%o0,%l0,%o0

	// shouldn't get here
3:	ba	3b
	 nop

	// i'm not core 0, so i'll call smp_init when the time is nigh
4:	set	time_for_smp_init,%l1
	ld	[%l1],%l0
	tst	%l0
	be	4b
	 nop

	call	smp_init
	 nop

	// shouldn't get here
5:	ba	5b
	 nop

///////////////////////////////////////////////////////////////////
// various data
///////////////////////////////////////////////////////////////////
.data

pagetable_init_done:
	.word 0

cores_relocated:
	.word 0
cores_relocated_lock:
	.word 0

	.global		time_for_smp_init
time_for_smp_init:
	.word		0

	.global		NWINDOWS
NWINDOWS:
	.word		0

	.global		num_cpus
num_cpus:
	.word		0

///////////////////////////////////////////////////////////////////
// boot stack
///////////////////////////////////////////////////////////////////

.section ".bss"
	.align		PGSIZE
	.space		KSTKSIZE*MAX_NUM_CPUS
	.global		bootstacktop   
bootstacktop:

.data
	.align		PGSIZE
	.space		KSTKSIZE
	.global		core0_bootstacktop
core0_bootstacktop:

///////////////////////////////////////////////////////////////////
// page tables
///////////////////////////////////////////////////////////////////
	.align		(NCONTEXTS+CONTEXT_TABLE_PAD)*4
	.global		mmu_context_tables
mmu_context_tables:
	.skip		MAX_NUM_CPUS*(NCONTEXTS+CONTEXT_TABLE_PAD)*4

	.align		1024
	.global		l1_page_table
l1_page_table:
	.skip		1024
