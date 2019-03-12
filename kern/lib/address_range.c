/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <ros/errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <sort.h>
#include <address_range.h>

static int address_range_cmp(const void *f1, const void *f2)
{
	return ((const struct address_range *) f1)->start <
		((const struct address_range *) f2)->start ? -1 : 1;
}

int address_range_validate(const struct address_range *ars, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (ars[i].start > ars[i].end)
			return -EINVAL;
		if ((i > 0) && (ars[i - 1].end >= ars[i].start))
			return -EINVAL;
	}

	return 0;
}

int address_range_init(struct address_range *ars, size_t count)
{
	sort(ars, count, sizeof(*ars), address_range_cmp);

	return address_range_validate(ars, count);
}

const struct address_range *address_range_find(const struct address_range *ars,
					       size_t count, uintptr_t addr)
{
	ssize_t l = 0, r = count - 1;

	while (l <= r) {
		ssize_t x = l + (r - l) / 2;
		const struct address_range *car = ars + x;

		if (car->end < addr)
			l = x + 1;
		else if (car->start > addr)
			r = x - 1;
		else
			return car;
	}

	return NULL;
}
