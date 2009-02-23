/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



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

.data
.globl trap_tbl;
trap_tbl:

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(ISR_divide_error, T_DIVIDE);
TRAPHANDLER_NOEC(ISR_debug_exceptions, T_DEBUG);
/* NMI is int 2 */
TRAPHANDLER_NOEC(ISR_breakpoint, T_BRKPT);
TRAPHANDLER_NOEC(ISR_overflow, T_OFLOW);
TRAPHANDLER_NOEC(ISR_bounds_check, T_BOUND);
TRAPHANDLER_NOEC(ISR_invalid_opcode, T_ILLOP);
TRAPHANDLER_NOEC(ISR_device_not_available, T_DEVICE);
TRAPHANDLER(ISR_double_fault, T_DBLFLT);
/* 9 reserved */
TRAPHANDLER(ISR_invalid_TSS, T_TSS);
TRAPHANDLER(ISR_segment_not_present, T_SEGNP);
TRAPHANDLER(ISR_stack_exception, T_STACK);
TRAPHANDLER(ISR_general_protection_fault, T_GPFLT);
TRAPHANDLER(ISR_page_fault, T_PGFLT);
/* 15 reserved */
TRAPHANDLER_NOEC(ISR_floating_point_error, T_FPERR);
TRAPHANDLER(ISR_alignment_check, T_ALIGN);
TRAPHANDLER_NOEC(ISR_machine_check, T_MCHK);
TRAPHANDLER_NOEC(ISR_simd_error, T_SIMDERR);

TRAPHANDLER_NOEC(ISR_syscall, T_SYSCALL);
/* Make sure default is last!! */
TRAPHANDLER_NOEC(ISR_default, T_DEFAULT);

.data
.globl trap_tbl_end;
trap_tbl_end:

.text
/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	pushl %ds;
	pushl %es;
	pushal;
	movw $GD_KD, %ax; /* data segments aren't accessible by default */
	movw %ax, %ds;
	movw %ax, %es;
	pushl %esp;
	call trap;
	popl %esp;
	popal;
	popl %es;
	popl %ds;
	addl $0x8, %esp; /* skip trapno and err */
	iret;
