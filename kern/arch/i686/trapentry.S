/* See COPYRIGHT for copyright information.
 * The two TRAP* macros (minus the .data parts) are from the JOS project.
 * Everything else:
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */
#include <arch/mmu.h>
#include <arch/trap.h>
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
	pushl $(num);												\
	jmp _alltraps;												\
	.data;														\
	.long name;													\
	.long num

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)		\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);						\
	jmp _alltraps;						\
	.data;								\
	.long name;							\
	.long num

/* Same as NOEC, but for IRQs instead.  num is the ISR number it is mapped to */
#define IRQ_HANDLER(name, num)			\
	.text;								\
	.globl name;						\
	.type name, @function;				\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);						\
	jmp _allirqs;						\
	.data;								\
	.long name;							\
	.long num

/* Same as above, but takes a specific function to jump to.  See comments
 * below from _allirqs for details.
 */
#define IRQ_HANDLER_SPEC(name, num, func)                                      \
	.text;								                                       \
	.globl name;						                                       \
	.type name, @function;				                                       \
	.align 2;							                                       \
	name:								                                       \
	pushl $0;                                                                  \
	pushl $(num);                                                              \
	cld;                                                                       \
	pushl %ds;                                                                 \
	pushl %es;                                                                 \
	pushl %fs;                                                                 \
	pushl %gs;                                                                 \
	pushal;                                                                    \
	movw $0, %ax;                                                              \
	movw %ax, %gs;                                                             \
	movw %ax, %fs;                                                             \
	movw $GD_KD, %ax;                                                          \
	movw %ax, %ds;                                                             \
	movw %ax, %es;                                                             \
	pushl %esp;                                                                \
	movl $0, %ebp;                                                             \
	call (func);                                                               \
	popl %esp;                                                                 \
	popal;                                                                     \
	popl %gs;                                                                  \
	popl %fs;                                                                  \
	popl %es;                                                                  \
	popl %ds;                                                                  \
	addl $0x8, %esp;                                                           \
	iret;                                                                      \
	.data;                                                                     \
	.long name;                                                                \
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
IRQ_HANDLER_SPEC(IRQ223, I_KERNEL_MSG, __kernel_message)

/* Technically, these HANDLER entries do not need to be in numeric order */
TRAPHANDLER_NOEC(ISR_syscall, T_SYSCALL)
/* But make sure default is last!! */
TRAPHANDLER_NOEC(ISR_default, T_DEFAULT)

.data
.globl trap_tbl_end
trap_tbl_end:

/* Keep the exit paths of _alltraps, _allirqs, and sysenter_handler in sync
 * with the corresponding pop_tf's.
 */
.text
_alltraps:
	cld
	pushl %ds
	pushl %es
	pushl %fs
	pushl %gs
	pushal
	movw $0, %ax;
	movw %ax, %gs;
	movw %ax, %fs;
	movw $GD_KD, %ax		# data segments aren't accessible by default
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	movl $0, %ebp			# so we can backtrace to this point
	call trap
	popl %esp
	popal
	popl %gs
	popl %fs
	popl %es
	popl %ds
	addl $0x8, %esp			# skip trapno and err
	iret

/* will need to think about when we reenable interrupts.  right now, iret does it,
 * if the previous EFLAGS had interrupts enabled
 */
_allirqs:
	cld
	pushl %ds
	pushl %es
	pushl %fs
	pushl %gs
	pushal
	movw $0, %ax;
	movw %ax, %gs;
	movw %ax, %fs;
	movw $GD_KD, %ax		# data segments aren't accessible by default
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	movl $0, %ebp			# so we can backtrace to this point
	call irq_handler
	popl %esp
	popal
	popl %gs
	popl %fs
	popl %es
	popl %ds
	addl $0x8, %esp			# skip IRQ number and err (which is 0)
	iret

.globl sysenter_handler;
.type sysenter_handler, @function;
# All of the pushl zeros are to keep the trap frame looking the same as when we
# receive a trap or an interrupt
sysenter_handler:
	sti						# enable interrupts (things are sane here)
	cld
	pushl $0				# ss
	pushl $0				# esp
	pushfl					# eflags
	pushl $0				# CS == 0 lets the kernel know it was a sysenter	
	pushl $0				# eip
	pushl $0				# err 
	pushl $T_SYSCALL		# helps with print_trapframe
	pushl %ds
	pushl %es
	pushl %fs
	pushl %gs
	pushal
	movw $0, %ax;
	movw %ax, %gs;
	movw %ax, %fs;
	movw $GD_KD, %ax
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	movl $0, %ebp			# so we can backtrace to this point
	call sysenter_callwrapper
	popl %esp
	popal
	popl %gs
	popl %fs
	popl %es
	popl %ds
	addl $0x10, %esp		# pop T_SYSCALL and the three zeros
	popfl					# restore EFLAGS (and usually enables interrupts!)
	movl %ebp, %ecx
	sti						# interrupts are turned off when starting a core
	sysexit
