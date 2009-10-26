#include <arch/mmu.h>
#include <arch/arch.h>
#include <ros/memlayout.h>

.data


// Define the global symbols 'envs', 'pages', 'vpt', and 'vpd'
// so that they can be used in C as if they were ordinary global arrays.
	.globl procinfo
	.set procinfo, UINFO
	.globl procdata
	.set procdata, UDATA
	.globl pages
	.set pages, UPAGES
	.globl vpt
	.set vpt, UVPT
	.globl vpd
	.set vpd, (UVPT+(UVPT>>12)*4)

.align 4
.globl stack_ptr_array
stack_ptr_array:
   .word 0


// Entrypoint - this is where the kernel (or our parent environment)
// starts us running when we are initially loaded into a new environment.
.text
.globl _start
_start:
	tst     %g6
	bne     notcore0
	 nop

	// Save top 256 bytes of stack for other threads to allocate their stacks
	//sub   %sp,256,%sp

	// See if we were started with arguments on the stack
	tst     %o0
	bne     args_exist
	 nop

	// If not, push dummy argc/argv arguments.
	// This happens when we are loaded by the kernel,
	// because the kernel does not know about passing arguments.
	mov	0,%o0
	mov	0,%o1

args_exist:
	call	parlibmain
	 nop

	unimp

notcore0:

	// Get a stack, set %sp

	set	stack_ptr_array, %l0
	ld	[%l0], %l0
	sll	%g6, 2, %l1
	ld	[%l0+%l1], %l1
	sub	%l1, 64, %sp

	call    hart_entry
	 nop

	call	hart_yield
	 nop

	unimp
