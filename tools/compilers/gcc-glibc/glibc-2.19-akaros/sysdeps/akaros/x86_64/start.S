/* Copyright (c) 2015 Google Inc., All Rights Reserved.
 * Kevin Klues <klueska@google.com>
 * See LICENSE for details. */

#define _start _real_start
#include <sysdeps/x86_64/start.S>
#undef _start

	.text
	.globl _start
	.type _start,@function
_start:
	cfi_startproc
	/* Save rdx on the stack. The SYSV ABI uses rdx as the only register to
	 * pass information to a process (everything else is on the stack). Save
	 * it here since we interpose with a call to __libc_vcore_entry(). */
	pushq %rdx

	/* Pass vcoreid to __libc_vcore_entry. Akaros passes it to us in the rbx
	 * register. On all but the first time through this function we will
	 * never return. */
	movq %rbx, %rdi
	call __libc_vcore_entry

	/* Restore rdx and call out to the real start function. This function is
	 * Only called once even though we may enter on this start function
	 * multiple times. */
	popq %rdx
	jmp _real_start
	cfi_endproc

