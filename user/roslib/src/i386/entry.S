#include <arch/mmu.h>
#include <arch/trap.h>
#include <ros/memlayout.h>
#include <ros/mman.h>
#include <ros/syscall.h>

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
	cmpl $0, %eax
	jne new_core

	// See if we should start up normally, or act like a new core
	cmpl $0, in_multi_mode
	jne new_core

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
	movl %eax, %esi // save the vcoreid in esi

	// compute USTACKTOP - PGSIZE*(%eax+1) (which is vcore_id+1), put in %edx
	incl %eax;
	movl $PGSIZE, %ebx
	mull %ebx
	movl $USTACKTOP, %edx
	subl %eax, %edx
	
	// syscall, a1: edx, a2: ecx, a3: ebx, a4: edi
	movl $PGSIZE, %ecx
	movl $(PROT_READ|PROT_WRITE), %ebx
	// the kernel uses arg4 of mmap to find the real arg4, 5 and 6
	// WARNING!  keep this in sync!!
	movl $mmap_args, %edi
	mov $SYS_mmap, %eax
	int $T_SYSCALL
	// by using edx instead of eax, we're assuming our request was satisfied.
	// eax has the actual address we can use.  since we would have used eax
	// without checking, it would fail anyways, and now we can handle mmap
	// aborting in the event a stack is already allocated here.  this is all
	// extremely dangerous to do, so be careful...
	addl $PGSIZE, %edx
	movl %edx, %esp
	movl %esi, %eax // restore the vcoreid
	call hart_entry
	// spin when we return.  ought to yield before this ever happens
2:		jmp 2b

.data
mmap_args:
	.long MAP_ANONYMOUS|MAP_FIXED|MAP_STACK|MAP_POPULATE|MAP_GROWSDOWN // arg4
	.long -1             // arg5
	.long 0              // arg6
.globl in_multi_mode
in_multi_mode:
	.long 0              // flag for vcore0 to know to act like other cores
