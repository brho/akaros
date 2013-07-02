/* See COPYRIGHT for copyright information.
 * The two TRAP* macros (minus the .data parts) are from the JOS project.
 * Everything else:
 * Copyright (c) 2009, 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */
#include <arch/mmu.h>
#include <arch/trap.h>
#include <arch/x86.h>
#include <ros/memlayout.h>

###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * It also builds this traps portion of the trap_tbl.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */
#define TRAPHANDLER(name, num)									\
	.text;														\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */				\
	name:			/* function starts here */					\
	pushq $(num);												\
	jmp _alltraps;												\
	.data;														\
	.quad name;													\
	.long num

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.  */
#define TRAPHANDLER_NOEC(name, num)		\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);						\
	jmp _alltraps;						\
	.data;								\
	.quad name;							\
	.long num

/* Same as NOEC, but for IRQs instead.  num is the ISR number it is mapped to */
#define IRQ_HANDLER(name, num)			\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);						\
	jmp _allirqs;						\
	.data;								\
	.quad name;							\
	.long num

.data
.globl trap_tbl
trap_tbl:

/*
 * Generate entry points for the different traps.
 */
TRAPHANDLER_NOEC(ISR_divide_error, T_DIVIDE)
TRAPHANDLER_NOEC(ISR_debug_exceptions, T_DEBUG)
TRAPHANDLER_NOEC(ISR_NMI, T_NMI)
TRAPHANDLER_NOEC(ISR_breakpoint, T_BRKPT)
TRAPHANDLER_NOEC(ISR_overflow, T_OFLOW)
TRAPHANDLER_NOEC(ISR_bounds_check, T_BOUND)
TRAPHANDLER_NOEC(ISR_invalid_opcode, T_ILLOP)
TRAPHANDLER_NOEC(ISR_device_not_available, T_DEVICE)
/* supposedly, DF generates an error code, but the one time we've had a DF so
 * far, it didn't.  eventually, this should probably be handled with a task gate
 * it might have pushed a 0, but just the rest of the stack was corrupt
 */
TRAPHANDLER_NOEC(ISR_double_fault, T_DBLFLT)
/* 9 reserved */
TRAPHANDLER(ISR_invalid_TSS, T_TSS)
TRAPHANDLER(ISR_segment_not_present, T_SEGNP)
TRAPHANDLER(ISR_stack_exception, T_STACK)
TRAPHANDLER(ISR_general_protection_fault, T_GPFLT)
TRAPHANDLER(ISR_page_fault, T_PGFLT)
/* 15 reserved */
TRAPHANDLER_NOEC(ISR_floating_point_error, T_FPERR)
TRAPHANDLER(ISR_alignment_check, T_ALIGN)
TRAPHANDLER_NOEC(ISR_machine_check, T_MCHK)
TRAPHANDLER_NOEC(ISR_simd_error, T_SIMDERR)
/* 20 - 31 reserved */
IRQ_HANDLER(IRQ0, 32)
IRQ_HANDLER(IRQ1, 33)
IRQ_HANDLER(IRQ2, 34)
IRQ_HANDLER(IRQ3, 35)
IRQ_HANDLER(IRQ4, 36)
IRQ_HANDLER(IRQ5, 37)
IRQ_HANDLER(IRQ6, 38)
IRQ_HANDLER(IRQ7, 39)
IRQ_HANDLER(IRQ8, 40)
IRQ_HANDLER(IRQ9, 41)
IRQ_HANDLER(IRQ10, 42)
IRQ_HANDLER(IRQ11, 43)
IRQ_HANDLER(IRQ12, 44)
IRQ_HANDLER(IRQ13, 45)
IRQ_HANDLER(IRQ14, 46)
IRQ_HANDLER(IRQ15, 47)
/* 25 general purpose vectors, for use by the LAPIC.  Can expand later. */
IRQ_HANDLER(IRQ198, I_TESTING) # used in testing.c
IRQ_HANDLER(IRQ199, 231)
IRQ_HANDLER(IRQ200, 232)
IRQ_HANDLER(IRQ201, 233)
IRQ_HANDLER(IRQ202, 234)
IRQ_HANDLER(IRQ203, 235)
IRQ_HANDLER(IRQ204, 236)
IRQ_HANDLER(IRQ205, 237)
IRQ_HANDLER(IRQ206, 238)
IRQ_HANDLER(IRQ207, 239)
/* 0xf0 - start of the SMP_CALL IPIS */
IRQ_HANDLER(IRQ208, I_SMP_CALL0)
IRQ_HANDLER(IRQ209, I_SMP_CALL1)
IRQ_HANDLER(IRQ210, I_SMP_CALL2)
IRQ_HANDLER(IRQ211, I_SMP_CALL3)
IRQ_HANDLER(IRQ212, I_SMP_CALL4)
IRQ_HANDLER(IRQ213, 245)
IRQ_HANDLER(IRQ214, 246)
IRQ_HANDLER(IRQ215, 247)
IRQ_HANDLER(IRQ216, 248)
IRQ_HANDLER(IRQ217, 249)
IRQ_HANDLER(IRQ218, 250)
IRQ_HANDLER(IRQ219, 251)
IRQ_HANDLER(IRQ220, 252)
IRQ_HANDLER(IRQ221, 253)
IRQ_HANDLER(IRQ222, 254)
IRQ_HANDLER(IRQ223, I_KERNEL_MSG)

/* Technically, these HANDLER entries do not need to be in numeric order */
TRAPHANDLER_NOEC(ISR_syscall, T_SYSCALL)
/* But make sure default is last!! */
TRAPHANDLER_NOEC(ISR_default, T_DEFAULT)

.data
.globl trap_tbl_end
trap_tbl_end:

.text
_alltraps:
	cld
	swapgs			# harmless if we were already in the kernel
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rdi
	pushq %rsi
	pushq %rbp
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax
	cmpw $GD_KT, 0x90(%rsp)	# 0x90 - diff btw tf_cs and tf_rax
	je trap_kernel_tf
	# this is a user TF, so we need to save their fs/gsbase and load gs base for
	# the kernel.
	movl $MSR_FS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# because we swapped gs earlier, the user GS is now in KERN_GS_BASE
	movl $MSR_KERN_GS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# make sure the kernel's gs base is loaded into the KERN slot at all times
	movl $MSR_GS_BASE, %ecx
	rdmsr
	movl $MSR_KERN_GS_BASE, %ecx
	wrmsr
	jmp trap_all_tf
trap_kernel_tf:
	# we don't muck with fs/gsbase, push placeholders
	movq $0xdeadbeef, %rax
	pushq %rax
	pushq %rax
trap_all_tf:
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdi
	call trap
	# the return paths are only used by the kernel
	addq $0x10, %rsp			# skip fs/gs base
	popq %rax
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rbp
	popq %rsi
	popq %rdi
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15
	addq $0x10, %rsp			# skip trapno and err
	iretq

# might merge this with _alltraps
_allirqs:
	cld
	swapgs			# harmless if we were already in the kernel
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %r11
	pushq %r10
	pushq %r9
	pushq %r8
	pushq %rdi
	pushq %rsi
	pushq %rbp
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %rax
	cmpw $GD_KT, 0x90(%rsp)	# 0x90 - diff btw tf_cs and tf_rax
	je irq_kernel_tf
	# this is a user TF, so we need to save their fs/gsbase and load gs base for
	# the kernel.
	movl $MSR_FS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# because we swapped gs earlier, the user GS is now in KERN_GS_BASE
	movl $MSR_KERN_GS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# make sure the kernel's gs base is loaded into the KERN slot at all times
	movl $MSR_GS_BASE, %ecx
	rdmsr
	movl $MSR_KERN_GS_BASE, %ecx
	wrmsr
	jmp irq_all_tf
irq_kernel_tf:
	# we don't muck with fs/gsbase, push placeholders
	movq $0xdeadbeef, %rax
	pushq %rax
	pushq %rax
irq_all_tf:
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdi
	call irq_handler
	# the return paths are only used by the kernel
	addq $0x10, %rsp			# skip fs/gs base
	popq %rax
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rbp
	popq %rsi
	popq %rdi
	popq %r8
	popq %r9
	popq %r10
	popq %r11
	popq %r12
	popq %r13
	popq %r14
	popq %r15
	addq $0x10, %rsp			# skip trapno and err
	iretq

.globl sysenter_handler;
.type sysenter_handler, @function;

sysenter_handler:
#ifndef CONFIG_NOFASTCALL_FSBASE
	# Do a quick TLS / FS base change, never changing stacks.
	# When rdi has the magic number, rsi has the new base
	movabs $FASTCALL_SETFSBASE, %rax
	cmp %rax, %rdi
	jne normal_syscall	# could profile this and handle the jump differently
	# need to check rsi, make sure it is canonical (will enfore below ULIM).
	# need to either do this check, or handle the kernel GP fault on wrmsr.
	movq %rsi, %rdi
	shrq $47, %rdi
	cmp $0, %rdi
	jne fastcall_pop
	# need to use cx, dx, and ax for the wrmsr.  dx and ax are free.
	movq %rcx, %rdi		# save rcx, the retaddr
	movq %rsi, %rdx
	movq %rsi, %rax
	shrq $32, %rdx
	andl $0xffffffff, %eax
	movl $MSR_FS_BASE, %ecx
	wrmsr
	movq %rdi, %rcx 	# restore retaddr
fastcall_pop:
	rex.w sysret
normal_syscall:
#endif
	# cld is handled by the SFMASK
	swapgs
	movq %gs:0, %rsp
	# Saving the FPU callee-saved state for now.  Might be able to have the
	# preempt handler deal with it.
	pushq $0			# space for mxcsr and fpucw
	fnstcw 0x4(%rsp)
	stmxcsr (%rsp)
	pushq %rdx			# rsp, saved by userspace
	pushq %rcx			# rip, saved by hardware
	pushq %r15
	pushq %r14
	pushq %r13
	pushq %r12
	pushq %rbp
	pushq %rbx
	# save fs and gs base
	movl $MSR_FS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# because we swapped gs earlier, the user GS is now in KERN_GS_BASE
	movl $MSR_KERN_GS_BASE, %ecx
	rdmsr
	shl $32, %rdx
	orq %rax, %rdx
	pushq %rdx
	# make sure the kernel's gs base is loaded into the KERN slot at all times
	movl $MSR_GS_BASE, %ecx
	rdmsr
	movl $MSR_KERN_GS_BASE, %ecx
	wrmsr
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdx
	# arg0, rdi: struct sysc*.  arg1, rsi: count.  arg2, rdx: sw_tf
	call sysenter_callwrapper
	# return via pop_tf, never this path
sysenter_spin:
	jmp sysenter_spin
