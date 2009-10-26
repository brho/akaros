#include <ros/memlayout.h>

.data


// Define the global symbols 'envs', 'pages', 'vpt', and 'vpd'
// so that they can be used in C as if they were ordinary global arrays.
	.globl procinfo
	.set procinfo, UINFO
	.globl procdata
	.set procdata, UDATA

// Stack pointers, to be allocated by the hart lib
	.globl stack_ptr_array
	stack_ptr_array:
		.word 0

// TODO: We're not exposing these yet.  Think about how to do so judiciously.
//	.globl vpt
//	.set vpt, UVPT
//	.globl vpd
//	.set vpd, (UVPT+(UVPT>>12)*4)


// Entrypoint - this is where the kernel (or our parent environment)
// starts us running when we are initially loaded into a new environment.
.text
.globl _start
_start:
	// Are we core 0?
	test	%eax,%eax
	jne	notcore0

	// See if we were started with arguments on the stack
	cmpl $USTACKTOP, %esp
	jne args_exist

	// If not, push dummy argc/argv arguments.
	// This happens when we are loaded by the kernel,
	// because the kernel does not know about passing arguments.
	pushl $0
	pushl $0

args_exist:
	call parlibmain
1:      jmp 1b

notcore0:
	mov	stack_ptr_array,%edx
	mov	(%edx,%eax,4),%esp

	call	hart_entry

	call	hart_yield

1:	jmp	1b
