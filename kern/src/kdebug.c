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
	"limborexmit",
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

/* Call this via kfunc */
void reset_print_func_depth(void)
{
	tab_depth = 0;
}

static spinlock_t lock = SPINLOCK_INITIALIZER_IRQSAVE;

static void __print_hdr(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	printd("Core %2d ", core_id());	/* may help with multicore output */
	if (in_irq_ctx(pcpui)) {
		printk("IRQ       :");
	} else {
		assert(pcpui->cur_kthread);
		if (pcpui->cur_kthread->is_ktask) {
			printk("%10s:", pcpui->cur_kthread->name);
		} else {
			printk("PID %3d   :", pcpui->cur_proc ? pcpui->cur_proc->pid : 0);
		}
	}
}

void __print_func_entry(const char *func, const char *file)
{
	char tentabs[] = "\t\t\t\t\t\t\t\t\t\t"; // ten tabs and a \0
	char *ourtabs = &tentabs[10 - MIN(tab_depth, 10)];
	if (!printx_on)
		return;
	if (is_blacklisted(func))
		return;
	spin_lock_irqsave(&lock);
	__print_hdr();
	printk("%s%s() in %s\n", ourtabs, func, file);
	spin_unlock_irqsave(&lock);
	tab_depth++;
}

void __print_func_exit(const char *func, const char *file)
{
	char tentabs[] = "\t\t\t\t\t\t\t\t\t\t"; // ten tabs and a \0
	char *ourtabs;
	if (!printx_on)
		return;
	if (is_blacklisted(func))
		return;
	tab_depth--;
	ourtabs = &tentabs[10 - MIN(tab_depth, 10)];
	spin_lock_irqsave(&lock);
	__print_hdr();
	printk("%s---- %s()\n", ourtabs, func);
	spin_unlock_irqsave(&lock);
}

bool printx_on = FALSE;

void set_printx(int mode)
{
	switch (mode) {
		case 0:
			printx_on = FALSE;
			break;
		case 1:
			printx_on = TRUE;
			break;
		case 2:
			printx_on = !printx_on;
			break;
	}
}

void debug_addr_proc(struct proc *p, unsigned long addr)
{
	struct vm_region *vmr;
	spin_lock(&p->vmr_lock);
	TAILQ_FOREACH(vmr, &p->vm_regions, vm_link) {
		if ((vmr->vm_base <= addr) && (addr < vmr->vm_end))
			break;
	}
	if (!vmr) {
		spin_unlock(&p->vmr_lock);
		printk("Addr %p has no VMR\n", addr);
		return;
	}
	if (!vmr->vm_file) {
		spin_unlock(&p->vmr_lock);
		printk("Addr %p's VMR has no file\n", addr);
		return;
	}
	printk("Addr %p is in %s at offset %p\n", addr, file_name(vmr->vm_file),
	       addr - vmr->vm_base + vmr->vm_foff);
	spin_unlock(&p->vmr_lock);
}

void debug_addr_pid(int pid, unsigned long addr)
{
	struct proc *p;
	p = pid2proc(pid);
	if (!p) {
		printk("No such proc for pid %d\n", pid);
		return;
	}
	debug_addr_proc(p, addr);
	proc_decref(p);
}
