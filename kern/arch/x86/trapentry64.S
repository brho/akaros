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

/* Only used in the kernel during SMP boot.  Send a LAPIC_EOI and iret. */
#define POKE_HANDLER(name, num)			\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:;								\
	movl $0, (LAPIC_BASE + 0x0b0);      \
	iretq;								\
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
/* IOAPIC/MSI vectors */
IRQ_HANDLER(IRQ16, 48)
IRQ_HANDLER(IRQ17, 49)
IRQ_HANDLER(IRQ18, 50)
IRQ_HANDLER(IRQ19, 51)
IRQ_HANDLER(IRQ20, 52)
IRQ_HANDLER(IRQ21, 53)
IRQ_HANDLER(IRQ22, 54)
IRQ_HANDLER(IRQ23, 55)
IRQ_HANDLER(IRQ24, 56)
IRQ_HANDLER(IRQ25, 57)
IRQ_HANDLER(IRQ26, 58)
IRQ_HANDLER(IRQ27, 59)
IRQ_HANDLER(IRQ28, 60)
IRQ_HANDLER(IRQ29, 61)
IRQ_HANDLER(IRQ30, 62)
IRQ_HANDLER(IRQ31, 63)
IRQ_HANDLER(IRQ32, 64)
IRQ_HANDLER(IRQ33, 65)
IRQ_HANDLER(IRQ34, 66)
IRQ_HANDLER(IRQ35, 67)
IRQ_HANDLER(IRQ36, 68)
IRQ_HANDLER(IRQ37, 69)
IRQ_HANDLER(IRQ38, 70)
IRQ_HANDLER(IRQ39, 71)
IRQ_HANDLER(IRQ40, 72)
IRQ_HANDLER(IRQ41, 73)
IRQ_HANDLER(IRQ42, 74)
IRQ_HANDLER(IRQ43, 75)
IRQ_HANDLER(IRQ44, 76)
IRQ_HANDLER(IRQ45, 77)
IRQ_HANDLER(IRQ46, 78)
IRQ_HANDLER(IRQ47, 79)
IRQ_HANDLER(IRQ48, 80)
IRQ_HANDLER(IRQ49, 81)
IRQ_HANDLER(IRQ50, 82)
IRQ_HANDLER(IRQ51, 83)
IRQ_HANDLER(IRQ52, 84)
IRQ_HANDLER(IRQ53, 85)
IRQ_HANDLER(IRQ54, 86)
IRQ_HANDLER(IRQ55, 87)
IRQ_HANDLER(IRQ56, 88)
IRQ_HANDLER(IRQ57, 89)
IRQ_HANDLER(IRQ58, 90)
IRQ_HANDLER(IRQ59, 91)
IRQ_HANDLER(IRQ60, 92)
IRQ_HANDLER(IRQ61, 93)
IRQ_HANDLER(IRQ62, 94)
IRQ_HANDLER(IRQ63, 95)
IRQ_HANDLER(IRQ64, 96)
IRQ_HANDLER(IRQ65, 97)
IRQ_HANDLER(IRQ66, 98)
IRQ_HANDLER(IRQ67, 99)
IRQ_HANDLER(IRQ68, 100)
IRQ_HANDLER(IRQ69, 101)
IRQ_HANDLER(IRQ70, 102)
IRQ_HANDLER(IRQ71, 103)
IRQ_HANDLER(IRQ72, 104)
IRQ_HANDLER(IRQ73, 105)
IRQ_HANDLER(IRQ74, 106)
IRQ_HANDLER(IRQ75, 107)
IRQ_HANDLER(IRQ76, 108)
IRQ_HANDLER(IRQ77, 109)
IRQ_HANDLER(IRQ78, 110)
IRQ_HANDLER(IRQ79, 111)
IRQ_HANDLER(IRQ80, 112)
IRQ_HANDLER(IRQ81, 113)
IRQ_HANDLER(IRQ82, 114)
IRQ_HANDLER(IRQ83, 115)
IRQ_HANDLER(IRQ84, 116)
IRQ_HANDLER(IRQ85, 117)
IRQ_HANDLER(IRQ86, 118)
IRQ_HANDLER(IRQ87, 119)
IRQ_HANDLER(IRQ88, 120)
IRQ_HANDLER(IRQ89, 121)
IRQ_HANDLER(IRQ90, 122)
IRQ_HANDLER(IRQ91, 123)
IRQ_HANDLER(IRQ92, 124)
IRQ_HANDLER(IRQ93, 125)
IRQ_HANDLER(IRQ94, 126)
IRQ_HANDLER(IRQ95, 127)
IRQ_HANDLER(IRQ96, 128)
IRQ_HANDLER(IRQ97, 129)
IRQ_HANDLER(IRQ98, 130)
IRQ_HANDLER(IRQ99, 131)
IRQ_HANDLER(IRQ100, 132)
IRQ_HANDLER(IRQ101, 133)
IRQ_HANDLER(IRQ102, 134)
IRQ_HANDLER(IRQ103, 135)
IRQ_HANDLER(IRQ104, 136)
IRQ_HANDLER(IRQ105, 137)
IRQ_HANDLER(IRQ106, 138)
IRQ_HANDLER(IRQ107, 139)
IRQ_HANDLER(IRQ108, 140)
IRQ_HANDLER(IRQ109, 141)
IRQ_HANDLER(IRQ110, 142)
IRQ_HANDLER(IRQ111, 143)
IRQ_HANDLER(IRQ112, 144)
IRQ_HANDLER(IRQ113, 145)
IRQ_HANDLER(IRQ114, 146)
IRQ_HANDLER(IRQ115, 147)
IRQ_HANDLER(IRQ116, 148)
IRQ_HANDLER(IRQ117, 149)
IRQ_HANDLER(IRQ118, 150)
IRQ_HANDLER(IRQ119, 151)
IRQ_HANDLER(IRQ120, 152)
IRQ_HANDLER(IRQ121, 153)
IRQ_HANDLER(IRQ122, 154)
IRQ_HANDLER(IRQ123, 155)
IRQ_HANDLER(IRQ124, 156)
IRQ_HANDLER(IRQ125, 157)
IRQ_HANDLER(IRQ126, 158)
IRQ_HANDLER(IRQ127, 159)
IRQ_HANDLER(IRQ128, 160)
IRQ_HANDLER(IRQ129, 161)
IRQ_HANDLER(IRQ130, 162)
IRQ_HANDLER(IRQ131, 163)
IRQ_HANDLER(IRQ132, 164)
IRQ_HANDLER(IRQ133, 165)
IRQ_HANDLER(IRQ134, 166)
IRQ_HANDLER(IRQ135, 167)
IRQ_HANDLER(IRQ136, 168)
IRQ_HANDLER(IRQ137, 169)
IRQ_HANDLER(IRQ138, 170)
IRQ_HANDLER(IRQ139, 171)
IRQ_HANDLER(IRQ140, 172)
IRQ_HANDLER(IRQ141, 173)
IRQ_HANDLER(IRQ142, 174)
IRQ_HANDLER(IRQ143, 175)
IRQ_HANDLER(IRQ144, 176)
IRQ_HANDLER(IRQ145, 177)
IRQ_HANDLER(IRQ146, 178)
IRQ_HANDLER(IRQ147, 179)
IRQ_HANDLER(IRQ148, 180)
IRQ_HANDLER(IRQ149, 181)
IRQ_HANDLER(IRQ150, 182)
IRQ_HANDLER(IRQ151, 183)
IRQ_HANDLER(IRQ152, 184)
IRQ_HANDLER(IRQ153, 185)
IRQ_HANDLER(IRQ154, 186)
IRQ_HANDLER(IRQ155, 187)
IRQ_HANDLER(IRQ156, 188)
IRQ_HANDLER(IRQ157, 189)
IRQ_HANDLER(IRQ158, 190)
IRQ_HANDLER(IRQ159, 191)
IRQ_HANDLER(IRQ160, 192)
IRQ_HANDLER(IRQ161, 193)
IRQ_HANDLER(IRQ162, 194)
IRQ_HANDLER(IRQ163, 195)
IRQ_HANDLER(IRQ164, 196)
IRQ_HANDLER(IRQ165, 197)
IRQ_HANDLER(IRQ166, 198)
IRQ_HANDLER(IRQ167, 199)
IRQ_HANDLER(IRQ168, 200)
IRQ_HANDLER(IRQ169, 201)
IRQ_HANDLER(IRQ170, 202)
IRQ_HANDLER(IRQ171, 203)
IRQ_HANDLER(IRQ172, 204)
IRQ_HANDLER(IRQ173, 205)
IRQ_HANDLER(IRQ174, 206)
IRQ_HANDLER(IRQ175, 207)
IRQ_HANDLER(IRQ176, 208)
IRQ_HANDLER(IRQ177, 209)
IRQ_HANDLER(IRQ178, 210)
IRQ_HANDLER(IRQ179, 211)
IRQ_HANDLER(IRQ180, 212)
IRQ_HANDLER(IRQ181, 213)
IRQ_HANDLER(IRQ182, 214)
IRQ_HANDLER(IRQ183, 215)
IRQ_HANDLER(IRQ184, 216)
IRQ_HANDLER(IRQ185, 217)
IRQ_HANDLER(IRQ186, 218)
IRQ_HANDLER(IRQ187, 219)
IRQ_HANDLER(IRQ188, 220)
IRQ_HANDLER(IRQ189, 221)
IRQ_HANDLER(IRQ190, 222)
IRQ_HANDLER(IRQ191, 223)
IRQ_HANDLER(IRQ192, 224)
IRQ_HANDLER(IRQ193, 225)
IRQ_HANDLER(IRQ194, 226)
IRQ_HANDLER(IRQ195, 227)
IRQ_HANDLER(IRQ196, 228)
IRQ_HANDLER(IRQ197, 229)

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
POKE_HANDLER(IRQ222, 254)
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
	call handle_irq
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
