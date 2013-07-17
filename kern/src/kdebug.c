/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent kernel debugging */

#include <kdebug.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>

struct symtab_entry gbl_symtab[1] __attribute__((weak)) = {{0, 0}};

/* Returns a null-terminated string with the function name for a given PC /
 * instruction pointer.  kfree() the result. */
char *get_fn_name(uintptr_t pc)
{
	struct symtab_entry *i, *prev = 0, *found = 0;
	char *buf;
	size_t name_len;
	/* Table is in ascending order.  As soon as we get to an entry greater than
	 * us, we were in the previous one.  This is only true if we were given a
	 * good PC.  Random addresses will just find the previous symbol. */
	for (i = &gbl_symtab[0]; i->name; i++) {
		if (i->addr > pc) {
			found = prev;
			break;
		}
		prev = i;
	}
	if (!found)
		return 0;
	assert(found->name);
	name_len = strlen(found->name) + 1;
	buf = kmalloc(name_len, 0);
	if (!buf)
		return 0;
	strncpy(buf, found->name, name_len);
	buf[name_len] = 0;
	return buf;
}

uintptr_t get_symbol_addr(char *sym)
{
	struct symtab_entry *i;
	for (i = &gbl_symtab[0]; i->name; i++) {
		if (strcmp(i->name, sym) == 0)
			return i->addr;
	}
	return 0;
}
