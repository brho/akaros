/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <vmm/util.h>

/* initrd loads the initrd. It is currently just a wrapper for cat,
 * but we leave it here in case we need to do more for initrd at
 * some point. */
ssize_t setup_initrd(char *filename, void *membase, size_t memsize)
{
	if (!filename)
		return 0;

	return cat(filename, membase, memsize);
}
