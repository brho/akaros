/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Vcore entry point from the kernel.  We need this in asm to fix our stack
 * pointer offset.  The kernel will start us with a 16-byte aligned stack.  C
 * code expects to be odd-8-byte aligned.  The 'call' will push a retaddr on the
 * stack, which is the 8 bytes we need. */

.globl __kernel_vcore_entry
__kernel_vcore_entry:
	call __kvc_entry_c
1:
	jb 1
