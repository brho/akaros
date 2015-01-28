/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * Trivial thread-safe ID pool for small sets of things (< 64K)
 * implemented as a stack.
 */

#ifndef ROS_KERN_SMALLIDPOOL_H
#define ROS_KERN_SMALLIDPOOL_H

#define MAX_U16_POOL_SZ (1 << 16)

#include <atomic.h>

/* IDS is the stack of 16 bit integers we give out.  TOS is the top of stack -
 * it is the index of the next slot that can be popped, if there are any.  It's
 * a u32 so it can be greater than a u16.
 *
 * All free slots in ids will be below the TOS, ranging from indexes [0, TOS),
 * where if TOS == 0, then there are no free slots to push.
 *
 * We can hand out u16s in the range [0, 65535].
 *
 * The check array is used instead of a bitfield because these architectures
 * suck at those. */

struct u16_pool {
	spinlock_t lock;
	uint32_t tos;
	uint16_t *ids;
	uint8_t *check;
	int size;
};

struct u16_pool *create_u16_pool(unsigned int size);
int get_u16(struct u16_pool *id);
void put_u16(struct u16_pool *id, int v);

#endif /* ROS_KERN_SMALLIDPOOL_H */
