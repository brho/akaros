/* See COPYRIGHT for copyright information. */

#include <arch/mmu.h>
#include <arch/sparc.h>
#include <arch/arch.h>
#include <ros/memlayout.h>

###################################################################
# The kernel (this code) is linked at address (KERNBASE + 0x00000000),
# but we tell the bootloader to load it at physical address 
# 0x00000000, which is the start of extended memory.
# (See kernel.ld)
###################################################################


###################################################################
# RELOC(x) maps a symbol x from its link address to its actual
# location in physical memory (its load address).	 
###################################################################
#define	RELOC(x) ((x) - KERNBASE)

###################################################################
# entry point
###################################################################

.text

.global		_start
_start:
	# This is the first code that ever executes.  It executes on all
	# cores (RAMP Gold-specific).  All we know is that PSR.S (supervisor)
	# and PSR.ET (enable traps) are both 0.  Before we can enable traps,
	# we must determine how many register windows we have, set up the
	# trap table, and set up a stack frame.

	# compute NWINDOWS

	mov	-1,%wim			! mark all windows invalid.
	mov	(PSR_S|PSR_PS),%psr
	nop				! 3 insns between wrwim/rdwim
	mov	0,%g2		! g2 will contain NWINDOWS-1
	mov	%wim,%g1	! get wim. nonexistent windows set to 0

1:	srl	%g1,1,%g1
	tst	%g1
	bne,a	1b
	 inc	%g2

	# now g2 = NWINDOWS - 1.  Patch the window spill trap handler.
	set	RELOC(spill_patchme),%g1
	ld	[%g1],%g3
	or	%g2,%g3,%g3
	st	%g3,[%g1]
	flush	%g1

	# and patch the window fill trap handler.
	set	RELOC(fill_patchme),%g1
	ld	[%g1],%g3
	or	%g2,%g3,%g3
	st	%g3,[%g1]
	flush	%g1

	# store NWINDOWS away for safekeeping
	set	RELOC(NWINDOWS),%g1
	inc	%g2
	st	%g2,[%g1]

	# PSR.CWP (current window pointer) == 0.
	# Set WIM so we'll trap on the next save instruction.
	mov	1 << 1,%wim

	# set up the TBR (trap base register)
	set	RELOC(trap_table),%g1
	mov	%g1,%tbr

	# clear frame pointer for backtrace termination
	mov	0,%fp

	# set stack pointer (-64 is space for window spill)
	# sp = bootstacktop - core_id*KSTKSIZE - 64
	set	RELOC(bootstacktop)-64,%sp
	mov	CORE_ID_REG,%g1
	sll	%g1,KSTKSHIFT,%g1
	sub	%sp,%g1,%sp

	# set up a virtual->physical mapping and relocate
	call	mmu_boot
	 nop

	# now we're relocated, so set %sp and TBR again
	set	KERNBASE,%g1
	add	%sp,%g1,%sp
	set	trap_table,%g1
	mov	%g1,%tbr

	# now it's safe to enable traps
	mov	%psr,%g1
	wr	%g1,PSR_ET,%psr
	nop; nop; nop

	# am i core 0?  (do i run BSD?!?)
	mov	CORE_ID_REG,%g1
	tst	%g1
	bne	4f
	 nop

	# only core 0 gets here
	# set num_cpus
	set	num_cpus,%l0
	mov	NUM_CORES_REG,%l1
	st	%l1,[%l0]

	cmp	%l1,MAX_NUM_CPUS
	tg	0x7f

	sub	%sp,64,%sp		! 64 >= sizeof(multiboot_header_t)
	call	build_multiboot_info
	 add	%sp,64,%o0

	# kernel_init time!
	# first arg is pointer to multiboot_info_t, but kernel_init
	# expects it to be a pre-relocation address, so lop off KERNBASE
	set	KERNBASE,%l0
	add	%sp,64,%o0
	call	kernel_init
	 sub	%o0,%l0,%o0

	# shouldn't get here
3:	ba	3b
	 nop

	# i'm not core 0, so i'll call smp_init when the time is nigh
4:	set	time_for_smp_init,%l1
	ld	[%l1],%l0
	tst	%l0
	be	4b
	 nop

	call	smp_init
	 nop

	# shouldn't get here
5:	ba	5b
	 nop



# this function (against the ABI!) relocates its caller's stack pointer
# and return address, then returns to the caller, relocated

.global		relocate
relocate:
	set	KERNBASE,%o0
	inc	8,%o7
	add	%i7,%o0,%i7
	jmp	%o7+%o0
	add	%sp,%o0,%sp

.data

###################################################################
# various data
###################################################################

	.global		time_for_smp_init
time_for_smp_init:
	.word		0

	.global		NWINDOWS
NWINDOWS:
	.word		0

	.global		num_cpus
num_cpus:
	.word		0

###################################################################
# boot stack
###################################################################

	.align		PGSIZE		! force page alignment
	.global		bootstack
bootstack:
	.space		KSTKSIZE*MAX_NUM_CPUS
	.global		bootstacktop   
bootstacktop:

###################################################################
# page tables
###################################################################
	.align		(NCONTEXTS+CONTEXT_TABLE_PAD)*4
	.global		mmu_context_tables
mmu_context_tables:
	.skip		MAX_NUM_CPUS*(NCONTEXTS+CONTEXT_TABLE_PAD)*4

	.align		1024
	.global		l1_page_table
l1_page_table:
	.skip		1024
