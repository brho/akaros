#include <arch/mmu.h>
#include <ros/memlayout.h>

.section .data


// Define the global symbols 'procinfo', 'procdata', 'pages', 'vpt', and 'vpd'
// so that they can be used in C as if they were ordinary global arrays.
.globl procinfo
procinfo:
.long UINFO
.globl procdata
procdata:
.long UDATA
.globl vpt
vpt:
.long UVPT
.globl vpd
vpd:
.long (UVPT+(UVPT>>12)*4)


// Entrypoint - this is where the kernel (or our parent process)
// starts us running when we are initially created and run
.text
.globl _start
_start:
	// See if we are a new core
	cmpl $1, %eax
	je new_core

	// See if we were started with arguments on the stack
	cmpl $USTACKTOP, %esp
	jne args_exist

	// If not, push dummy argc/argv arguments.
	// This happens when we are loaded by the kernel,
	// because the kernel does not know about passing arguments.
	pushl $0
	pushl $0

args_exist:
	call libmain
1:      jmp 1b

new_core:
2:		jmp 2b
