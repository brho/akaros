/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Console (Keyboard/serial/whatever) related functions. */

#ifndef ROS_KERN_CONSOLE_H
#define ROS_KERN_CONSOLE_H

#include <atomic.h>
#include <kthread.h>
#include <trap.h>

#define KB_BUF_SIZE 256 	/* Make sure this is a power of 2 */

/* Ring buffer for keyboard/character devices.  Might make a more generic
 * version in the future (allowing both sides to block, etc).  Adding will drop
 * any overflow, and getting will block til the full amount is read. */
struct kb_buffer {
	unsigned int prod_idx;
	unsigned int cons_idx;
	spinlock_t							buf_lock;
	struct semaphore					buf_sem;
	char								buf[KB_BUF_SIZE];
};
extern struct kb_buffer cons_buf;	/* kernel's console buffer */

void kb_buf_init(struct kb_buffer *kb);
/* These are not irq-safe.  and get will block. */
void kb_add_to_buf(struct kb_buffer *kb, char c);
void kb_get_from_buf(struct kb_buffer *kb, char *dst, size_t cnt);

/* Kernel messages associated with the console.  Arch-specific interrupt
 * handlers need to call these.  For add char, a0 = &cons_buf and a1 = the char
 * you read.  Call __run_mon on your 'magic' input.  */
void __cons_add_char(struct trapframe *tf, uint32_t srcid, long a0, long a1,
                     long a2);
void __run_mon(struct trapframe *tf, uint32_t srcid, long a0, long a1, long a2);

#endif /* ROS_KERN_CONSOLE_H */
