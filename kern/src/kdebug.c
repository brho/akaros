/* Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-independent kernel debugging */

#include <kdebug.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>
#include <smp.h>

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

static const char *blacklist[] = {
	"addnode",
	"addqueue",
	"allocroute",
	"balancetree",
	"calcd",
	"freeroute",
	"genrandom",	/* not noisy, just never returns */
	"rangecompare",
	"walkadd",
};

static bool is_blacklisted(const char *s)
{
	for (int i = 0; i < ARRAY_SIZE(blacklist); i++) {
		if (!strcmp(blacklist[i], s))
			return TRUE;
	}
	return FALSE;
}

static int tab_depth = 0;
static bool print = TRUE;

/* Call these via kfunc */
void reset_print_func_depth(void)
{
	tab_depth = 0;
}

void toggle_print_func(void)
{
	print = !print;
	printk("Func entry/exit printing is now %sabled\n", print ? "en" : "dis");
}

static spinlock_t lock = SPINLOCK_INITIALIZER_IRQSAVE;

void __print_func_entry(const char *func, const char *file)
{
	if (!print)
		return;
	if (is_blacklisted(func))
		return;
	spin_lock_irqsave(&lock);
	printd("Core %2d", core_id());	/* helps with multicore output */
	for (int i = 0; i < tab_depth; i++)
		printk("\t");
	printk("%s() in %s\n", func, file);
	spin_unlock_irqsave(&lock);
	tab_depth++;
}

void __print_func_exit(const char *func, const char *file)
{
	if (!print)
		return;
	if (is_blacklisted(func))
		return;
	tab_depth--;
	spin_lock_irqsave(&lock);
	printd("Core %2d", core_id());
	for (int i = 0; i < tab_depth; i++)
		printk("\t");
	printk("---- %s()\n", func);
	spin_unlock_irqsave(&lock);
}
