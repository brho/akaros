/* Copyright (c) 2015 Google Inc.
 *
 * Trivial ID pool for small sets of things (< 64K)
 * implemented as a stack.
 */

struct idpool *init(int size)
{
	int i;
	struct idpool *id;
	if (size > MAXAMT)
		return NULL;
	id = malloc(sizeof(*id) + 2*(size+1) + (size + 1));
	id->size = size;
	id->ids = (void *)&id[1];
	id->check = (void *)&id->ids[id->size + 1];
	for(i = 0; i < id->size+1; i++) {
		id->ids[i] = i;
		// fe rhymes with "free"
		id->check[i] = 0xfe;
	}
	// set tos.
	id->ids[0] = 1;
	return id;
}


int pop(struct idpool *id)
{
	int tos, v;
	if (id->ids[0] == id->size) {
		return -1;
	}
	/* NOT SMP SAFE! CAN'T USE IT YET!
	 * the intent here is that we want to be able to atomic ops on the stack pointer
	 * and avoid locking. One way might be to load it and, if it's in range, do a CAS.
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
