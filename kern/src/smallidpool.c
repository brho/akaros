/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * Trivial thread-safe ID pool for small sets of things (< 64K)
 * implemented as a stack.
 */

#include <smallidpool.h>
#include <kmalloc.h>
#include <atomic.h>
#include <stdio.h>
#include <assert.h>

struct u16_pool *create_u16_pool(unsigned int size)
{
	struct u16_pool *id;

	/* We could have size be a u16, but this might catch bugs where users
	 * tried to ask for more than 2^16 and had it succeed. */
	if (size > MAX_U16_POOL_SZ)
		return NULL;
	/* ids and check are alloced and aligned right after the id struct */
	id = kmalloc(sizeof(*id) + sizeof(uint16_t) * size + size, MEM_WAIT);
	spinlock_init_irqsave(&id->lock);
	id->size = size;
	id->ids = (void *)&id[1];
	id->check = (void *)&id->ids[id->size];
	for (int i = 0; i < id->size; i++) {
		id->ids[i] = i;
		// fe rhymes with "free"
		id->check[i] = 0xfe;
	}
	id->tos = 0;
	return id;
}

/* Returns an unused u16, or -1 on failure (pool full or corruption).
 *
 * The invariant is that the stackpointer (TOS) will always point to the next
 * slot that can be popped, if there are any.  All free slots will be below the
 * TOS, ranging from indexes [0, TOS), where if TOS == 0, then there are no free
 * slots to push.  The last valid slot is when TOS == size - 1. */
int get_u16(struct u16_pool *id)
{
	uint16_t v;

	spin_lock_irqsave(&id->lock);
	if (id->tos == id->size) {
		spin_unlock_irqsave(&id->lock);
		return -1;
	}
	v = id->ids[id->tos++];
	spin_unlock_irqsave(&id->lock);
	/* v is ours, we can freely read and write its check field */
	if (id->check[v] != 0xfe) {
		printk("BAD! %d is already allocated (0x%x)\n", v,
		       id->check[v]);
		return -1;
	}
	id->check[v] = 0x5a;
	return v;
}

void put_u16(struct u16_pool *id, int v)
{
	/* we could check for if v is in range before dereferencing. */
	if (id->check[v] != 0x5a) {
		printk("BAD! freeing non-allocated: %d(0x%x)\n", v,
		       id->check[v]);
		return;
	}
	id->check[v] = 0xfe;
	spin_lock_irqsave(&id->lock);
	id->ids[--id->tos] = v;
	spin_unlock_irqsave(&id->lock);
}
