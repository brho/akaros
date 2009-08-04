#include <arch/mmu.h>
#include <ros/memlayout.h>

.data


// Define the global symbols 'procinfo', 'procdata', 'pages', 'vpt', and 'vpd'
// so that they can be used in C as if they were ordinary global arrays.
	.globl procinfo
	.set procinfo, UINFO
	.globl procdata
	.set procdata, USYSCALL
	.globl pages
	.set pages, UPAGES
	.globl vpt
	.set vpt, UVPT
	.globl vpd
	.set vpd, (UVPT+(UVPT>>12)*4)


// Entrypoint - this is where the kernel (or our parent process)
// starts us running when we are initially created and run
.text
.globl _start
_start:
	// See if we were started with arguments on the stack
	tst	%o0
	bne	args_exist
	 nop

	// If not, push dummy argc/argv arguments.
	// This happens when we are loaded by the kernel,
	// because the kernel does not know about passing arguments.
	mov	0,%o0
	mov	0,%o1

args_exist:
	call	libmain
	 nop

1:	ba	1b
	 nop

