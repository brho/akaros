#include <arch/mmu.h>
#include <ros/memlayout.h>
#include <arch/trap.h>
#include <arch/x86.h>

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
	# Turn on Paging.  We're using the symbol from entry64, which we'll have no
	# problem linking against (compared to boot_cr3).  this assumes we use the
	# boot stuff at least through smp_boot.
	movl	$boot_pml4, %eax
	movl	%eax, %cr3
	# turn on paging option in cr4.  note we assume PSE support.  if we didn't
	# have it, then our jumbo page mappings are going to fail.  we also want
	# global pages (for performance).  PAE is the basics needed for long paging
	movl	%cr4, %eax
	orl		$(CR4_PSE | CR4_PGE | CR4_PAE), %eax
	movl	%eax, %cr4
	# Turn on the IA32E enabled bit.
	# rd/wrmsr use ecx for the addr, and eax as the in/out register.
	movl	$IA32_EFER_MSR, %ecx
	rdmsr
	orl		$IA32_EFER_IA32E_EN, %eax
	wrmsr
	# Setup cr0.  PE and PG are critical for now.  The others are similar to
	# what we want in general (-AM with 64 bit, it's useless).
	movl	%cr0, %eax
	orl		$(CR0_PE | CR0_PG | CR0_WP | CR0_NE | CR0_MP), %eax
	andl	$(~(CR0_AM | CR0_TS | CR0_EM | CR0_CD | CR0_NW)), %eax
	movl	%eax, %cr0

	# paging is on, but we don't have that low page mapped.  hence the crash
	# here
	# 		even if we made this work, we'd probably want pg 0 mapped until the
	# 		IDT is up
	#
	# 		1)maybe smp boot in entry.S and pause
	# 				lock after long_mode
	# 		2)faster syscalls with bools and whatnot.  see email

	#1:jmp 1b

	# load the 64bit GDT and jump to long mode (symbol from entry64)
	lgdt	gdt64desc
	# Want to jump to the label long_mode, but we need to relocate to code
	# reachable by 32 bit code: on our trampoline page.
	ljmp	$0x08, $(long_mode - smp_entry + 0x1000)
.code64
long_mode:
	# Note: we are still running code on the trampoline
	# zero the data segments.  Not sure if this is legit or not.
	xor		%rax, %rax
	mov		%ax, %ds
	mov		%ax, %es
	mov		%ax, %ss
	mov		%ax, %fs
	mov		%ax, %gs
	lldt	%ax
	incl	x86_num_cores_booted		# an int
	movq	(smp_stack_top), %rsp
	movq	$0, %rbp		# so backtrace works
	# We're on the trampoline, but want to be in the real location of the smp
	# code (somewhere above KERN_LOAD_ADDR).  This allows us to easily unmap
	# the boot up memory, which the trampoline is part of.
	movabs	$(non_trampoline), %rax
	call	*%rax
non_trampoline:
	call	smp_main
	movq	%rax, %rsp		# use our new stack, value returned from smp_main
	# note the next two lines are using the direct mapping from smp_boot().
	# Remember, the stuff at 0x1000 is a *copy* of the code and data at
	# KERN_LOAD_ADDR.
	movw	$0, smp_boot_lock - smp_entry + 0x1000 	# release lock
	lock decw	smp_semaphore - smp_entry + 0x1000  # show we are done
	sti                     # so we can get the IPI
	hlt                     # wait for the IPI to run smp_pcu_init()
	call 	smp_final_core_init
	call 	smp_idle		# idle loop, will have interrupts turned on
	# smp_idle should never return
spin:
	jmp spin

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
