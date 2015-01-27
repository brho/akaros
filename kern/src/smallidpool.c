/* Copyright (c) 2015 Google Inc.
 *
 * Trivial ID pool for small sets of things (< 64K)
 * implemented as a stack.
 *
 * TODO:
 * - Kbuild, change function names, kmalloc, ifdefs, size should be unsigned
 * - walk through what happens if size == 0.  See TODO below in pop.
 * - look into CAS options.  The problem is that although we can CAS on TOS, we
 *   cannot yank out v without some sort of ABA issue.  maybe if we have a
 *   separate index variable and maintain a history counter along with the idx.
 * - maybe build in locking, if we think we'll have an SMP-safe option?
 */

struct idpool *init(int size)
{
	int i;
	struct idpool *id;
	/* they give us e.g. size = 65535, we make an array of 65536 elements.
	 * array[0] is a tracking slot, not an item slot.  so there are still only
	 * 65535 elements available, numbered 1..65535 (which is the max u16). */
	if (size > MAXAMT)
		return NULL;
	id = malloc(sizeof(*id) + 2 * (size + 1) + (size + 1));
	id->size = size;
	id->ids = (void *)&id[1];
	id->check = (void *)&id->ids[id->size + 1];
	for (i = 0; i < id->size + 1; i++) {
		id->ids[i] = i;
		// fe rhymes with "free"
		id->check[i] = 0xfe;
	}
	// set tos.
	id->ids[0] = 1;
	id->check[0] = 0x5a;	/* never give out id 0 */
	return id;
}

/* The invariant is that the stackpointer (TOS) will always point to the next
 * slot that can be popped, if there are any.  All free slots will be below the
 * TOS, ranging from indexes [1, TOS), where if TOS == 1, then there are no free
 * slots to push. */
int pop(struct idpool *id)
{
	int tos, v;
	/* TODO: is this off by one?  this means we won't give out 65535.  right
	 * now, we are pointing at the last valid slot, right?  Based on how push
	 * subtracts right away, it should be okay. */
	if (id->ids[0] == id->size) {
		return -1;
	}
	/* NOT SMP SAFE! CAN'T USE IT YET!  the intent here is that we want to be
	 * able to atomic ops on the stack pointer and avoid locking. One way might
	 * be to load it and, if it's in range, do a CAS.
	 */
	tos = id->ids[0];
	id->ids[0]++;
	v = id->ids[tos];
	if (id->check[v] != 0xfe) {
		printk("BAD! %d is already allocated (0x%x)\n", v, id->check[v]);
		return -1;
	}
	id->check[v] = 0x5a;
	return v;
}

void push(struct idpool *id, int v)
{
	int tos;
	if (id->check[v] != 0x5a) {
		panic("BAD! freeing non-allocated: %d(0x%x)\n", v, id->check[v]);
		return;
	}
	id->ids[0]--;
	tos = id->ids[0];
	id->ids[tos] = v;
	id->check[v] = 0xfe;
}
