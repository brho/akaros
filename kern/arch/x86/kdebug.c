#include <string.h>
#include <assert.h>
#include <kdebug.h>
#include <pmap.h>
#include <process.h>
#include <kmalloc.h>
#include <arch/uaccess.h>

void gen_backtrace(void (*pfunc)(void *, const char *), void *opaque)
{
	uintptr_t pcs[MAX_BT_DEPTH];
	size_t nr_pcs;

	nr_pcs = backtrace_list(get_caller_pc(), get_caller_fp(), pcs,
	                        MAX_BT_DEPTH);
	print_backtrace_list(pcs, nr_pcs, pfunc, opaque);
}

static bool pc_is_asm_trampoline(uintptr_t pc)
{
	extern char __asm_entry_points_start[], __asm_entry_points_end[];
	extern char __asm_pop_hwtf_start[], __asm_pop_hwtf_end[];
	extern char __asm_pop_swtf_start[], __asm_pop_swtf_end[];
	extern char __asm_pop_vmtf_start[], __asm_pop_vmtf_end[];

	if (((uintptr_t)__asm_entry_points_start <= pc) &&
	    (pc < (uintptr_t)__asm_entry_points_end))
		return TRUE;
	if (((uintptr_t)__asm_pop_hwtf_start <= pc) &&
	    (pc < (uintptr_t)__asm_pop_hwtf_end))
		return TRUE;
	if (((uintptr_t)__asm_pop_swtf_start <= pc) &&
	    (pc < (uintptr_t)__asm_pop_swtf_end))
		return TRUE;
	if (((uintptr_t)__asm_pop_vmtf_start <= pc) &&
	    (pc < (uintptr_t)__asm_pop_vmtf_end))
		return TRUE;
	return FALSE;
}

size_t backtrace_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
                      size_t nr_slots)
{
	size_t nr_pcs = 0;

	while (nr_pcs < nr_slots) {
		pcs[nr_pcs++] = pc;
		printd("PC %p FP %p\n", pc, fp);
		if (pc_is_asm_trampoline(pc))
			break;
		if (!fp)
			break;
		assert(KERNBASE <= fp);
		/* We need to check the next FP before reading PC from beyond it.  FP
		 * could be 0 and be at the top of the stack, and reading PC in that
		 * case will be a wild read. */
		if (!*(uintptr_t*)fp)
			break;
		/* We used to set PC = retaddr - 1, where the -1 would put our PC back
		 * inside the function that called us.  This was for obscure cases where
		 * a no-return function calls another function and has no other code
		 * after the function call.  Or something. */
		pc = *(uintptr_t*)(fp + sizeof(uintptr_t));
		fp = *(uintptr_t*)fp;
	}
	return nr_pcs;
}

size_t backtrace_user_list(uintptr_t pc, uintptr_t fp, uintptr_t *pcs,
						   size_t nr_slots)
{
	int error;
	size_t nr_pcs = 0;
	uintptr_t frame[2];

	while (nr_pcs < nr_slots) {
		pcs[nr_pcs++] = pc;
		if (!fp)
			break;
		error = copy_from_user(frame, (const void *) fp, 2 * sizeof(uintptr_t));
		if (unlikely(error))
			break;
		pc = frame[1];
		fp = frame[0];
	}
	return nr_pcs;
}

/* Assumes 32-bit header */
void print_fpu_state(struct ancillary_state *fpu)
{
	print_lock();
	printk("fcw:        0x%04x\n", fpu->fp_head_n64.fcw);
	printk("fsw:        0x%04x\n", fpu->fp_head_n64.fsw);
	printk("ftw:          0x%02x\n", fpu->fp_head_n64.ftw);
	printk("fop:        0x%04x\n", fpu->fp_head_n64.fop);
	printk("fpu_ip: 0x%08x\n", fpu->fp_head_n64.fpu_ip);
	printk("cs:         0x%04x\n", fpu->fp_head_n64.cs);
	printk("fpu_dp: 0x%08x\n", fpu->fp_head_n64.fpu_dp);
	printk("ds:         0x%04x\n", fpu->fp_head_n64.ds);
	printk("mxcsr:  0x%08x\n", fpu->fp_head_n64.mxcsr);
	printk("mxcsrm: 0x%08x\n", fpu->fp_head_n64.mxcsr_mask);

	for (int i = 0; i < sizeof(struct ancillary_state); i++) {
		if (i % 20 == 0)
			printk("\n");
		printk("%02x ", *((uint8_t*)fpu + i));
	}
	printk("\n");
	print_unlock();
}
