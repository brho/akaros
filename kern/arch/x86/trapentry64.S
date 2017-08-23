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

#define NMI_HANDLER(name, num)			\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushq $0;							\
	pushq $(num);						\
	jmp _nmi_entry;						\
	.data;								\
	.quad name;							\
	.long num

#define DOUBLEFAULT_HANDLER(name, num)	\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushq $(num);						\
	jmp _dblf_entry;					\
	.data;								\
	.quad name;							\
	.long num

/* Bare minimum IRQ handler: send a LAPIC_EOI and immediately iret. */
#define POKE_HANDLER(name, num)			\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:;								\
	pushq %rax;			\
	pushq %rcx;			\
	pushq %rdx;			\
	movq $0, %rax;			\
	movq $0, %rdx;			\
	movq $(MSR_LAPIC_EOI), %rcx; 	\
	wrmsr;				\
	popq %rdx;			\
	popq %rcx;			\
	popq %rax;			\
	iretq;								\
	.data;								\
	.quad name;							\
	.long num

# Set data for the trap_tbl symbol.  Set text for the entry_points.  The various
# trap/irq handler macros will toggle data on and off as needed.
.data
.globl trap_tbl
trap_tbl:
.text
.globl __asm_entry_points_start
__asm_entry_points_start:

/* Generate entry points for the different traps.  Note that all of these bounce
 * off the corresponding trap.c function, such as handle_irqs, and that the name
 * e.g. ISR_divide_error is soley for the little stup that jumps to something
 * like _alltraps.
 *
 * Technically, these HANDLER entries do not need to be in numeric order.
 * trap.c will do a 'foreach (up to last-1), set the IDT for the number to point
 * to the func' in the order in which they appear in the trap tbl, so the 'last
 * one wins'. */
TRAPHANDLER_NOEC(ISR_divide_error, T_DIVIDE)
TRAPHANDLER_NOEC(ISR_debug_exceptions, T_DEBUG)
NMI_HANDLER(ISR_NMI, T_NMI)
TRAPHANDLER_NOEC(ISR_breakpoint, T_BRKPT)
TRAPHANDLER_NOEC(ISR_overflow, T_OFLOW)
TRAPHANDLER_NOEC(ISR_bounds_check, T_BOUND)
TRAPHANDLER_NOEC(ISR_invalid_opcode, T_ILLOP)
TRAPHANDLER_NOEC(ISR_device_not_available, T_DEVICE)
DOUBLEFAULT_HANDLER(ISR_double_fault, T_DBLFLT)
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
/* 32-47 are PIC/8259 IRQ vectors */
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
TRAPHANDLER_NOEC(ISR_syscall, T_SYSCALL)
/* 49-223 are IOAPIC routing vectors (from IOAPIC to LAPIC) */
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
/* 224-239 are OS IPI vectors (0xe0-0xef) */
IRQ_HANDLER(IRQ192, I_SMP_CALL0)
IRQ_HANDLER(IRQ193, I_SMP_CALL1)
IRQ_HANDLER(IRQ194, I_SMP_CALL2)
IRQ_HANDLER(IRQ195, I_SMP_CALL3)
IRQ_HANDLER(IRQ196, I_SMP_CALL4)
IRQ_HANDLER(IRQ197, 229)
IRQ_HANDLER(IRQ198, 230)
IRQ_HANDLER(IRQ199, 231)
IRQ_HANDLER(IRQ200, 232)
IRQ_HANDLER(IRQ201, 233)
IRQ_HANDLER(IRQ202, 234)
IRQ_HANDLER(IRQ203, 235)
IRQ_HANDLER(IRQ204, I_TESTING)
POKE_HANDLER(IRQ205, I_POKE_GUEST)
POKE_HANDLER(IRQ206, I_POKE_CORE)
IRQ_HANDLER(IRQ207, I_KERNEL_MSG)
/* 240-255 are LAPIC vectors (0xf0-0xff), hightest priority class */
IRQ_HANDLER(IRQ208, 240)
IRQ_HANDLER(IRQ209, 241)
IRQ_HANDLER(IRQ210, 242)
IRQ_HANDLER(IRQ211, 243)
IRQ_HANDLER(IRQ212, 244)
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
IRQ_HANDLER(IRQ223, 255)
/* But make sure default is last!! */
TRAPHANDLER_NOEC(ISR_default, T_DEFAULT)

.data
.globl trap_tbl_end
trap_tbl_end:

.text
_alltraps:
	cld
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
	je trap_all_tf
	# this is a user TF.  we need to swapgs to get the kernel's gs and mark the
	# context as partial
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	movl $0x1, 0xac(%rsp)	# 0xac - diff btw tf_padding0 and tf_rax
trap_all_tf:
	pushq $0				# fsbase space
	pushq $0				# gsbase space
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
	je irq_all_tf
	# this is a user TF.  we need to swapgs to get the kernel's gs and mark the
	# context as partial
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	movl $0x1, 0xac(%rsp)	# 0xac - diff btw tf_padding0 and tf_rax
irq_all_tf:
	pushq $0				# fsbase space
	pushq $0				# gsbase space
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

# Similar to the NMI handler, we come in on a special stack, but we can trust
# the contents of GS for kernel contexts.  This is mostly true.  If we double
# faulted in this file, for instance, then GS could be garbage.  But that should
# never happen.  Most double faults will be for running into a guard page on a
# stack.
#
# Since the double fault is so bad, we just want to get into the kernel's C code
# where we can run some diagnostics.  We (currently) won't try to recover, since
# it's probably a kernel bug.
_dblf_entry:
	cld
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
	je dblf_all_tf
	# this is a user TF.  we need to swapgs to get the kernel's gs and mark the
	# context as partial
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	movl $0x1, 0xac(%rsp)	# 0xac - diff btw tf_padding0 and tf_rax
dblf_all_tf:
	pushq $0				# fsbase space
	pushq $0				# gsbase space
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdi
	call handle_double_fault
dblf_spin:
	jmp dblf_spin

# Unlike normal trap and IRQ handlers, both user and kernel TFs are handled the
# similarly.  Both come in here and return from here.  We cannot do anything
# fancy like proc_restartcore() from NMI context.
#
# All NMIs will come in fresh on the same stack (IST1, per-core).  We don't need
# to find a stackpointer, but we do need to find GS.
#
# Regardless of whether or not the interrupted context was in the kernel, we
# can't trust GS.  Basically we can never tell from looking at GS and KERN_GS
# whether swapgs occurred, since the user could have put kernel addrs in their
# GS.  But we can use the stack for storage to bootstrap GS.  %rsp at entry
# points to pcpui *.
#
# We can also tell if GS was set correctly or not and avoid the wrmsr calls.
# This isn't a huge deal.  But note that we don't know whether or not the kernel
# actually set the gsbase.  We just know if it was correct or not.  The user
# could have set it and the kernel hadn't had a chance to swapgs yet.  The NMI
# handler doesn't care, since all TFs come in and go out via this asm.
#
# If we want to be paranoid, we can completely ignore user TFs and just save and
# restore GS with the same mechanism we use for the kernel.  But since we came
# in on a user TF, we can use swapgs.  Note that even for nested NMI handlers,
# we have a user TF for only the first time, and only the last exit will swapgs
# back to the way it was initially.
_nmi_entry:
	cld
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
	je nmi_kern_tf
	# this is a user TF.  we need to swapgs to get the kernel's gs and mark the
	# context as partial
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	movl $0x1, 0xac(%rsp)	# 0xac - diff btw tf_padding0 and tf_rax
	pushq $0				# fsbase space
	pushq $0				# gsbase space
	jmp nmi_all_tf
nmi_kern_tf:
	# this is a kernel TF.  but we don't know if they set up gs yet, so we'll
	# save and restore whatever they had loaded and use our own
	pushq $0				# fsbase space
	# Get the current GS base into rax
	movl $MSR_GS_BASE, %ecx
	rdmsr
	shlq $32, %rdx
	orq %rdx, %rax
	# Get the real GS base from the top of the stack.  This was set in smp_boot,
	# and our rsp pointed to it when we entered the kernel.
	movq 0xb8(%rsp), %rdx	# 0xb8 from fs_base to the top
	# Compare them.  If they are the same, we can just push 0 for gsbase (which
	# later will mean "no need to restore GS".
	cmpq %rdx, %rax
	je nmi_gs_ok
	# They weren't the same.  Save the old one and set the new one.
	pushq %rax				# gsbase space
	movq %rdx, %rax
	shrq $32, %rdx
	andl $0xffffffff, %eax
	wrmsr
	jmp nmi_all_tf
nmi_gs_ok:
	pushq $0				# gsbase space
nmi_all_tf:
	# At this point, GS is set correctly, either due to swapgs (user TF), wrmsr
	# (kern TF with bad GS), or it was already fine (and gsbase in the TF = 0).
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdi
	call handle_nmi
	# Unlike in normal IRQs/Traps, both user and kernel contexts return via this
	# path.
	cmpw $GD_KT, 0xa0(%rsp)	# 0xa0 - diff btw tf_cs and tf_gsbase
	je nmi_kern_restore_gs
	# User TF.  Restore whatever was there with swapgs.  We don't care what it
	# was, nor do we care what was in the TF.
	swapgs					# user's GS is now in MSR_GS_BASE
	addq $0x10, %rsp		# skip gs/fs base
	jmp nmi_popal
nmi_kern_restore_gs:
	popq %rax				# fetch saved gsbase
	addq $0x08, %rsp		# skip fs base
	cmpq $0, %rax
	je nmi_popal
	# gsbase in the TF != 0, which means we need to restore that gsbase
	movl $MSR_GS_BASE, %ecx
	movq %rax, %rdx
	shrq $32, %rdx
	andl $0xffffffff, %eax
	wrmsr
nmi_popal:
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

.globl __nmi_pop_ok_start;
.globl __nmi_pop_ok_end;
.globl __nmi_pop_fail_start;
.globl __nmi_pop_fail_end;

# extern void nmi_try_to_pop(struct hw_trapframe *tf, int *status,
#                            int old_val, int new_val);
#
# __nmi_bottom_half calls this to atomically pop a hw_tf (%rdi) and set
# &pcpui->nmi_status (%rsi) with compare and swap to NMI_NORMAL_OPN (%ecx) given
# that it was NMI_IN_PROGRESS (%edx)
#
# (Careful, nmi_status is an int, not a long.)
#
# If the real NMI handler interrupts us, it'll move us to the fail section of
# the code.  That code is identical to 'ok', up until ok's final statement.
#
# In that event, we'll need a little help returning: specifically to bootstrap
# pcpui and our current stackpointer.  pcpui is already saved near the top of
# stack.  We'll save rsp ourselves.
.globl nmi_try_to_pop;
.type nmi_try_to_pop, @function;
nmi_try_to_pop:
__nmi_pop_ok_start:
	# careful only to use caller-saved or argument registers before saving
	movl %edx, %eax			# load old_val into eax for the CAS
	cmpxchgl %ecx, (%rsi)	# no need for LOCK, since an NMI would serialize
	jz nmi_ok_cas_worked	# ZF = 1 on successful CAS
	ret
nmi_ok_cas_worked:
	# save callee-saved regs (the pops below clobber them, and we might return)
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	# We need to save the current rsp into the scratch space at the top of the
	# stack.  This assumes we're within the top page of our stack, which should
	# always be true.  Careful not to use rdi, which still has an argument.
	movq %rsp, %rbx
	# Want to round rbx up to PGSIZE, then subtract 8, to get our slot.
	movq $0xfff, %rax
	notq %rax				# rax = 0xfffffffffffff000
	andq %rax, %rbx			# round down rbx
	addq $0x1000, %rbx		# add PGSIZE, assuming rsp was not page aligned
	subq $0x8, %rbx			# point to the scratch space
	movq %rsp, (%rbx)		# save rsp in the scratch space
	# We jump our rsp to the base of the HW_TF.  This is still on the same
	# stack, just farther back than where our caller is.  We need to be careful
	# to not clobber the stack.  Otherwise we'll have chaos.
	movq %rdi, %rsp
	# From here down is the same as the normal NMI exit path, but with 'ok' in
	# the symbol names.
	cmpw $GD_KT, 0xa0(%rsp)	# 0xa0 - diff btw tf_cs and tf_gsbase
	je nmi_ok_kern_restore_gs
	# User TF.  Restore whatever was there with swapgs.  We don't care what it
	# was, nor do we care what was in the TF.
	swapgs					# user's GS is now in MSR_GS_BASE
	addq $0x10, %rsp		# skip gs/fs base
	jmp nmi_ok_popal
nmi_ok_kern_restore_gs:
	popq %rax				# fetch saved gsbase
	addq $0x08, %rsp		# skip fs base
	cmpq $0, %rax
	je nmi_ok_popal
	# gsbase in the TF != 0, which means we need to restore that gsbase
	movl $MSR_GS_BASE, %ecx
	movq %rax, %rdx
	shrq $32, %rdx
	andl $0xffffffff, %eax
	wrmsr
nmi_ok_popal:
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
__nmi_pop_ok_end:

# This is the 'fail' case.  It is identical to the 'ok' case, up until the
# iretq, other than 'ok' replaced with 'fail'.  In place of iretq, we undo the
# entire operation.
__nmi_pop_fail_start:
	# careful only to use caller-saved or argument registers before saving
	movl %edx, %eax			# load old_val into eax for the CAS
	cmpxchgl %ecx, (%rsi)	# no need for LOCK, since an NMI would serialize
	jz nmi_fail_cas_worked	# ZF = 1 on successful CAS
	ret
nmi_fail_cas_worked:
	# save callee-saved regs (the pops below clobber them, and we might return)
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	# We need to save the current rsp into the scratch space at the top of the
	# stack.  This assumes we're within the top page of our stack, which should
	# always be true.  Careful not to use rdi, which still has an argument.
	movq %rsp, %rbx
	# Want to round rbx up to PGSIZE, then subtract 8, to get our slot.
	movq $0xfff, %rax
	notq %rax				# rax = 0xfffffffffffff000
	andq %rax, %rbx			# round down rbx
	addq $0x1000, %rbx		# add PGSIZE, assuming rsp was not page aligned
	subq $0x8, %rbx			# point to the scratch space
	movq %rsp, (%rbx)		# save rsp in the scratch space
	# We jump our rsp to the base of the HW_TF.  This is still on the same
	# stack, just farther back than where our caller is.  We need to be careful
	# to not clobber the stack.  Otherwise we'll have chaos.
	movq %rdi, %rsp
	# From here down is the same as the normal NMI exit path and the ok path,
	# but with 'fail' in the symbol names.
	cmpw $GD_KT, 0xa0(%rsp)	# 0xa0 - diff btw tf_cs and tf_gsbase
	je nmi_fail_kern_restore_gs
	# User TF.  Restore whatever was there with swapgs.  We don't care what it
	# was, nor do we care what was in the TF.
	swapgs					# user's GS is now in MSR_GS_BASE
	addq $0x10, %rsp		# skip gs/fs base
	jmp nmi_fail_popal
nmi_fail_kern_restore_gs:
	popq %rax				# fetch saved gsbase
	addq $0x08, %rsp		# skip fs base
	cmpq $0, %rax
	je nmi_fail_popal
	# gsbase in the TF != 0, which means we need to restore that gsbase
	movl $MSR_GS_BASE, %ecx
	movq %rax, %rdx
	shrq $32, %rdx
	andl $0xffffffff, %eax
	wrmsr
nmi_fail_popal:
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
	addq $0x10, %rsp		# skip trapno and err
	# Here's is where we differ from OK.  Time to undo everything and return
	# rsp currently is pointing at tf->tf_rip.  Remember that we don't want to
	# write anything to the stack - everything in the TF is still the way it was
	# when we started to pop.
	#
	# First off, let's get the stack addr of the pcpui pointer loaded
	movq %rsp, %rbx
	movq $0xfff, %rax
	notq %rax				# rax = 0xfffffffffffff000
	andq %rax, %rbx			# round down rbx
	addq $0x1000, %rbx		# add PGSIZE, assuming rsp was not page aligned
	subq $0x10, %rbx		# point to the pcpui pointer
	# Now let's start to unwind
	subq $0x98, %rsp		# jump from rip to tf_gsbase (top of hw_tf)
	# Need to restore gs, just like on an NMI entry
	cmpw $GD_KT, 0xa0(%rsp)	# 0xa0 - diff btw tf_cs and tf_gsbase
	je nmi_pop_fail_kern_tf
	# This is a user TF.  We need to swapgs to get the kernel's gs
	# We don't need to mark the context as partial (we never do for NMIs,
	# actually), and in general, we don't want to write anything on the stack.
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	jmp nmi_pop_fail_all_tf
nmi_pop_fail_kern_tf:
	# Kernel TF.  We basically need to do the same thing on entry, since we
	# might have restored some weird GS base.  We can tell based on tf_gsbase
	# 0 for gsbase means we didn't need to change GS
	cmpq $0, (%rsp)
	je nmi_pop_fail_gs_fine
	# rbx points to where pcpui* is stored
	mov (%rbx), %rdx
	movl $MSR_GS_BASE, %ecx
	movq %rdx, %rax
	shrq $32, %rdx
	andl $0xffffffff, %eax
	wrmsr
nmi_pop_fail_gs_fine:
nmi_pop_fail_all_tf:
	addq $0x8, %rbx			# move to the scratch slot, holding rsp
	mov (%rbx), %rsp
	# restore callee-saved regs
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp
	ret
	# sweet jeebus.
__nmi_pop_fail_end:


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
	swapgs					# user's GS is now in MSR_KERNEL_GS_BASE
	movq %gs:0, %rsp
	# Saving the FPU callee-saved state for now.  Might be able to have the
	# preempt handler deal with it.
	pushq $0				# space for mxcsr, fpucw, and padding0
	movw $0x1, 0x6(%rsp)	# tf_padding0 = 1, partial context
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
	pushq $0			# fsbase space
	pushq $0			# gsbase space
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdx
	# arg0, rdi: struct sysc*.  arg1, rsi: count.  arg2, rdx: sw_tf
	call sysenter_callwrapper
	# return via pop_tf, never this path
sysenter_spin:
	jmp sysenter_spin

.globl vmexit_handler;
.type vmexit_handler, @function;
vmexit_handler:
	# rflags has all flags = 0, so cli and cld already.
	# HOST_GS_BASE and RSP is set by the hardware
	# Set default values.  Most of these will be set in C later.
	pushq $0			# guest_pa
	pushq $0			# guest_va
	pushq $0			# intrinfo2 and 1
	pushq $0			# exit_qual + exit_reason
	pushq $0			# pad + trap_inject
	pushq $0			# flags + guest_pcorid
	pushq $0			# cr3
	pushq $0			# cr2
	pushq $0			# rsp
	pushq $0			# rflags
	pushq $0			# rip
	# Save register state
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
	movq $0, %rbp			# so we can backtrace to this point
	movq %rsp, %rdi
	call handle_vmexit
vmexit_spin:
	jmp vmexit_spin

.globl __asm_entry_points_end
__asm_entry_points_end:
