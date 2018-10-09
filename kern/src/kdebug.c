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

/* Returns a null-terminated string from the reflected symbol table with the
 * function name for a given PC / instruction pointer.  Returns NULL on
 * failure. */
const char *get_fn_name(uintptr_t pc)
{
	struct symtab_entry *i, *prev = 0, *found = 0;

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
		return NULL;
	assert(found->name);
	return found->name;
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
	"bnx2x_alloc_rx_data",
	"bnx2x_frag_alloc",
	"__dma_map_single",
	"__dma_mapping_error",
	"__dma_zalloc_coherent",
	"__dma_alloc_coherent",
	"bnx2x_ilt_line_mem_op",
	"bnx2x_ilt_line_init_op",
	"bnx2x_ilt_line_wr",
	"bnx2x_wr_64",
	"pci_write_config_dword",
	"bnx2x_init_str_wr",
	"bnx2x_init_fill",
	"bnx2x_init_block",
	"bnx2x_write_big_buf",
	"bnx2x_init_wr_wb",
	"bnx2x_write_big_buf_wb",
	"bnx2x_cl45_read",
	"bnx2x_cl45_write",
	"bnx2x_set_mdio_clk",
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

static void __print_hdr(void)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	printd("Core %2d ", core_id());	/* may help with multicore output */
	if (in_irq_ctx(pcpui)) {
		printk("IRQ       :");
	} else {
		assert(pcpui->cur_kthread);
		if (is_ktask(pcpui->cur_kthread)) {
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
	print_lock();
	__print_hdr();
	printk("%s%s() in %s\n", ourtabs, func, file);
	print_unlock();
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
	print_lock();
	__print_hdr();
	printk("%s---- %s()\n", ourtabs, func);
	print_unlock();
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
	if (!vmr_has_file(vmr)) {
		spin_unlock(&p->vmr_lock);
		printk("Addr %p's VMR has no file\n", addr);
		return;
	}
	printk("Addr %p is in %s at offset %p\n", addr, vmr_to_filename(vmr),
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

#define BT_FMT "#%02d [<%p>] in %s\n"

void print_backtrace_list(uintptr_t *pcs, size_t nr_pcs,
						  void (*pfunc)(void *, const char *), void *opaque)
{
	char bt_line[128];

	for (size_t i = 0; i < nr_pcs; i++) {
		snprintf(bt_line, sizeof(bt_line), BT_FMT, i + 1, pcs[i],
		         get_fn_name(pcs[i]));
		pfunc(opaque, bt_line);
	}
}

void sza_print_backtrace_list(struct sized_alloc *sza, uintptr_t *pcs,
                              size_t nr_pcs)
{
	for (size_t i = 0; i < nr_pcs; i++)
		sza_printf(sza, BT_FMT, i + 1, pcs[i], get_fn_name(pcs[i]));
}

static void printk_func(void *opaque, const char *str)
{
	printk("%s", str);
}

void backtrace(void)
{
	print_lock();
	printk("Stack Backtrace on Core %d:\n", core_id());
	gen_backtrace(&printk_func, NULL);
	print_unlock();
}

static void trace_printk_func(void *opaque, const char *str)
{
	trace_printk("%s", str);
}

void backtrace_trace(void)
{
	/* Don't need this strictly, but it helps serialize to the trace buf */
	print_lock();
	trace_printk("Stack Backtrace on Core %d:\n", core_id());
	gen_backtrace(&trace_printk_func, NULL);
	print_unlock();
}

static void trace_printx_func(void *opaque, const char *str)
{
	trace_printx("%s", str);
}

void backtrace_trace_printx(void)
{
	/* Don't need this strictly, but it helps serialize to the trace buf */
	print_lock();
	trace_printx("Stack Backtrace on Core %d:\n", core_id());
	gen_backtrace(&trace_printk_func, NULL);
	print_unlock();
}

void backtrace_frame(uintptr_t eip, uintptr_t ebp)
{
	uintptr_t pcs[MAX_BT_DEPTH];
	size_t nr_pcs = backtrace_list(eip, ebp, pcs, MAX_BT_DEPTH);

	print_lock();
	printk("\nBacktrace of kernel context on Core %d:\n", core_id());
	print_backtrace_list(pcs, nr_pcs, &printk_func, NULL);
	print_unlock();
}

/* TODO: change debug_addr_proc() to allow print redirection like
 * print_backtrace_list(). */
void backtrace_user_frame(uintptr_t eip, uintptr_t ebp)
{
	uintptr_t pcs[MAX_BT_DEPTH];
	/* TODO: this assumes we have the user's address space loaded (current). */
	size_t nr_pcs = backtrace_user_list(eip, ebp, pcs, MAX_BT_DEPTH);

	print_lock();
	printk("\nBacktrace of user context on Core %d:\n", core_id());
	printk("\tOffsets only matter for shared libraries\n");
	/* This formatting is consumed by scripts/bt-akaros.sh. */
	for (int i = 0; i < nr_pcs; i++) {
		printk("#%02d ", i + 1);
		/* TODO: user backtraces all assume we're working on 'current' */
		debug_addr_proc(current, pcs[i]);
	}
	print_unlock();
}

void backtrace_hwtf(struct hw_trapframe *hw_tf)
{
	if (in_kernel(hw_tf))
		backtrace_frame(get_hwtf_pc(hw_tf), get_hwtf_fp(hw_tf));
	else
		backtrace_user_frame(get_hwtf_pc(hw_tf), get_hwtf_fp(hw_tf));
}

void backtrace_user_ctx(struct proc *p, struct user_context *ctx)
{
	uintptr_t st_save;

	if (!ctx) {
		printk("Null user context!\n");
		return;
	}
	st_save = switch_to(p);
	backtrace_user_frame(get_user_ctx_pc(ctx), get_user_ctx_fp(ctx));
	switch_back(p, st_save);
}

void backtrace_current_ctx(void)
{
	if (current)
		backtrace_user_ctx(current, current_ctx);
}

void backtrace_kthread(struct kthread *kth)
{
	backtrace_frame(jmpbuf_get_pc(&kth->context),
	                jmpbuf_get_fp(&kth->context));
}
