/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent kernel debugging */

#include <kdebug.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>

/* Returns a null-terminated string with the function name for a given PC /
 * instruction pointer.  kfree() the result. */
char *get_fn_name(uintptr_t pc)
{
	struct eipdebuginfo debuginfo;
	char *buf;
	if (debuginfo_eip(pc, &debuginfo))
		return 0;
	buf = kmalloc(debuginfo.eip_fn_namelen + 1, 0);
	if (!buf)
		return 0;
	assert(debuginfo.eip_fn_name);
	strncpy(buf, debuginfo.eip_fn_name, debuginfo.eip_fn_namelen);
	buf[debuginfo.eip_fn_namelen] = 0;
	return buf;
}
