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
.globl tls_array
tls_array:
	.word 0

.align 8
core0_tls:
	.skip 1024*1024


// Entrypoint - this is where the kernel (or our parent environment)
// starts us running when we are initially loaded into a new environment.
.text
.globl _start
_start:
	tst     %g6
	bne     notcore0
	 nop

	// Set up TLS
	set	core0_tls,%g7

	call	parlibmain
	 nop

	unimp

notcore0:

	// Get a stack, set %sp

	set	stack_ptr_array, %l0
	ld	[%l0], %l0
	sll	%g6, 2, %l1
	ld	[%l0+%l1], %l0
	sub	%l0, 64, %sp

	// Set up TLS
	set	tls_array, %l0
	ld	[%l0], %l0
	ld	[%l0+%l1],%g7

	call    hart_entry
	 nop

	call	hart_yield
	 nop

	unimp
