#include <arch/mmu.h>
#include <ros/memlayout.h>
#include <arch/trap.h>

#define	RELOC(x) ((x) - KERNBASE)
#define	CPUID_PSE_SUPPORT	0x00000008

.globl			smp_entry
smp_entry:		.code16
	cli
	cld
	lock incw	smp_semaphore - smp_entry + 0x1000  # announce our presence
spin_start:						# grab lock in real mode
	movw	$1, %ax
	xchgw	%ax, smp_boot_lock - smp_entry + 0x1000
	test	%ax, %ax
	jne		spin_start

	# Set up rudimentary segmentation
	xorw	%ax, %ax			# Segment number zero
	movw	%ax, %ds			# -> Data Segment
	movw	%ax, %es			# -> Extra Segment
	movw	%ax, %ss			# -> Stack Segment
	# Would like to patch all of these 0x1000's at trampoline relocation time
	# There's three of them, so we could patch the trampoline code when we load,
	# once we're sure the entry code will not change anymore
	# Note that this GDT is straight through, with no KERNBASE translation
	lgdt	gdtdesc - smp_entry + 0x1000

	# Turn on protected mode
	movl	%cr0, %eax
	orl		$CR0_PE, %eax
	movl	%eax, %cr0
	ljmp	$GD_KT, $(protcseg - smp_entry + 0x1000)
	
.code32
protcseg:
	# Set up the protected-mode data segment registers
	movw	$GD_KD, %ax		# Kernel segment selector
	movw	%ax, %ds		# -> DS: Data Segment
	movw	%ax, %es		# -> ES: Extra Segment
	movw	%ax, %ss		# -> SS: Stack Segment
	movw	%ax, %fs		# -> FS
	movw	%ax, %gs		# -> GS

	# Turn on Paging
	# crap, how do we sort out the relocs such that this is reachable from 32
	# bit code?  need to put these in a magic location?
	# XXX movl	RELOC(boot_cr3), %eax
	movl	%eax, %cr3
	# Enable PSE, if available
	movl	$1, %eax
	cpuid
	test	$CPUID_PSE_SUPPORT, %edx
	jz		past_pse
	movl	%cr4, %eax
	orl		$CR4_PSE, %eax
	movl	%eax, %cr4
past_pse:
	# Turn on PGE, no matter what.  Ghetto, but we panic if it's not supported.
	movl	%cr4, %eax
	orl		$CR4_PGE, %eax
	movl	%eax, %cr4
	movl	%cr0, %eax	
	# These cr0 flags are the same as in pmap.c.  Keep them in sync
	orl		$(CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_MP), %eax  
	andl	$(~(CR0_TS|CR0_EM|CR0_CD|CR0_NW)), %eax  
	movl	%eax, %cr0

	# Reload Segments, using the same gdt_pd as Core 0
	# XXX lgdt	gdt_pd
	movw	$GD_KD, %ax		# Kernel segment selector
	movw	%ax, %ds		# -> DS: Data Segment
	movw	%ax, %es		# -> ES: Extra Segment
	movw	%ax, %ss		# -> SS: Stack Segment
	movw	$GD_UD|3, %ax	# User segment selector, with RPL=3
	movw	%ax, %fs		# -> FS
	movw	%ax, %gs		# -> GS
	# TODO: this reloc is mucking up.  it assembles for 32 bit okay, but if you asm
	# for 64 bit, or if you objcopy ther 32 to 64, it can't find 'here'.  tried
	# other names too.  is it because the linker doesn't know where smp entry
	# is going to be, or that it later becomes a 64 bit reloc (unlike in
	# _start, where we know it is linked for low mem).  Subtracting them gives
	# the linker the ability to do the math now. (don't forget you can't trust
	# the disassembly when the format doesn't match the code type)
	# 
	# we're going to have issues with all of the relocs like that - trying to
	# find 64 bit addresses in 32 bit code.  for all of the external ones,
	# we're going to need to either put the memory in here, so we don't need to
	# reloc, or do it after we turn on 64b mode


	# ljmp	$GD_KT, $(here)	# jumping to original location of trampoline!
	ljmp	$GD_KT, $(here - smp_entry + 0x1000)
here:
	xorl	%eax, %eax
	lldt	%ax
	# XXX incl	num_cpus
	# XXX movl	(smp_stack_top), %esp
	movl	$0, %ebp		# so backtrace works
	call	smp_main
	movl	%eax, %esp		# use our new stack, value returned from smp_main
	# note the next two lines are using the direct mapping from smp_boot()
	movw	$0, smp_boot_lock - smp_entry + 0x1000 	# release lock
	lock decw	smp_semaphore - smp_entry + 0x1000  # show we are done
	sti                     # so we can get the IPI
	hlt                     # wait for the IPI to run smp_pcu_init()
	call 	smp_idle 		# idle loop, will have interrupts turned on

	# Below here is just data, stored with the code text
	.p2align	2						# force 4 byte alignment
gdt:
	SEG_NULL							# null seg
	SEG(STA_X|STA_R, 0, 0xffffffff)		# code seg
	SEG(STA_W, 0, 0xffffffff)			# data seg
gdtdesc:
	.word	gdtdesc - gdt - 1			# sizeof(gdt) - 1
	.long	gdt - smp_entry + 0x1000	# address gdt
	.p2align	2						# force 4 byte alignment
.globl			smp_boot_lock
smp_boot_lock:							# this lock word will be only used from
	.word	0							# its spot in the trampoline (0x1000)
.globl			smp_semaphore
smp_semaphore:							# poor man's polling semaphore
	.word	0							
.globl			smp_entry_end
smp_entry_end:
