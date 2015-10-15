/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <stdio.h>

static void mem_swap(void *a, void *b, size_t size)
{
	for (; size > 0; size--, a++, b++) {
		char tmp = *(char*) a;

		*(char *) a = *(char *) b;
		*(char *) b = tmp;
	}
}

void sort(void *base, size_t count, size_t size,
          int (*cmp)(const void *, const void *))
{
	ssize_t n = count * size, c;

	/* Standard heapsort algorithm. First loop creates a heap, and the
	 * second one progressively pop the top of the heap, and does re-heap
	 * operations so that we maintain the heap properties.
	 */
	for (ssize_t i = (count / 2 - 1) * size; i >= 0; i -= size) {
		for (ssize_t r = i; r * 2 + size < n; r = c) {
			c = r * 2 + size;
			if (c < n - size && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			mem_swap(base + r, base + c, size);
		}
	}

	for (ssize_t i = n - size; i > 0; i -= size) {
		mem_swap(base, base + i, size);
		for (ssize_t r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size && cmp(base + c, base + c + size) < 0)
				c += size;
			if (cmp(base + r, base + c) >= 0)
				break;
			mem_swap(base + r, base + c, size);
		}
	}
}
