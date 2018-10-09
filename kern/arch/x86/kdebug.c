#include <stab.h>
#include <string.h>
#include <assert.h>
#include <kdebug.h>
#include <pmap.h>
#include <process.h>
#include <kmalloc.h>
#include <arch/uaccess.h>

#include <ros/memlayout.h>

// Beginning of stabs table
extern const stab_t __STAB_BEGIN__[];

// End of stabs table
extern const stab_t __STAB_END__[];

// Beginning of string table
extern const char __STABSTR_BEGIN__[];

 // End of string table
extern const char __STABSTR_END__[];

typedef struct UserStabData {
	const stab_t *stabs;
	const stab_t *stab_end;
	const char *stabstr;
	const char *stabstr_end;
} user_stab_data_t;

/* We used to check for a null terminating byte for the entire strings section
 * (due to JOS, I think), but that's not what the spec says: only that all
 * strings are null terminated.  There might be random stuff tacked on at the
 * end.  I had some stabs that seemed valid (lookups worked), that did not have
 * the entire table be null terminated.  Still, something else might be jacked
 * up.  If it turns out that's the case, put the checks in here. */
static bool stab_table_valid(const char *stabstr, const char *stabstr_end)
{
	if (stabstr_end <= stabstr)
		return FALSE;
	return TRUE;
}

// stab_binsearch(stabs, region_left, region_right, type, addr)
//
//	Some stab types are arranged in increasing order by instruction
//	address.  For example, N_FUN stabs (stab entries with n_type ==
//	N_FUN), which mark functions, and N_SO stabs, which mark source files.
//
//	Given an instruction address, this function finds the single stab
//	entry of type 'type' that contains that address.
//
//	The search takes place within the range [*region_left, *region_right].
//	Thus, to search an entire set of N stabs, you might do:
//
//		left = 0;
//		right = N - 1;     /* rightmost stab */
//		stab_binsearch(stabs, &left, &right, type, addr);
//
//	The search modifies *region_left and *region_right to bracket the
//	'addr'.  *region_left points to the matching stab that contains
//	'addr', and *region_right points just before the next stab.  If
//	*region_left > *region_right, then 'addr' is not contained in any
//	matching stab.
//
//	For example, given these N_SO stabs:
//		Index  Type   Address
//		0      SO     f0100000
//		13     SO     f0100040
//		117    SO     f0100176
//		118    SO     f0100178
//		555    SO     f0100652
//		556    SO     f0100654
//		657    SO     f0100849
//	this code:
//		left = 0, right = 657;
//		stab_binsearch(stabs, &left, &right, N_SO, 0xf0100184);
//	will exit setting left = 118, right = 554.
//
static void
stab_binsearch(const stab_t *stabs,
               const stab_t *stab_end,
               int *region_left, int *region_right,
	       int type, uintptr_t addr)
{
	int l = *region_left, r = *region_right, any_matches = 0;

	while (l <= r) {
		int true_m = (l + r) / 2, m = true_m;

		// search for earliest stab with right type
		while (m >= l && stabs[m].n_type != type)
			m--;
		if (m < l) {	// no match in [l, m]
			l = true_m + 1;
			continue;
		}

		// actual binary search
		any_matches = 1;
		if (stabs[m].n_value < addr) {
			*region_left = m;
			l = true_m + 1;
		} else if (stabs[m].n_value > addr) {
			*region_right = m - 1;
			r = m - 1;
		} else {
			// exact match for 'addr', but continue loop to find
			// *region_right
			*region_left = m;
			l = m;
			addr++;
		}
	}

	if (!any_matches)
		*region_right = *region_left - 1;
	else {
		// find rightmost region containing 'addr'
		for (l = *region_right;
		     l > *region_left && stabs[l].n_type != type;
		     l--)
			/* do nothing */;
		*region_left = l;
	}
}


// debuginfo_eip(addr, info)
//
//	Fill in the 'info' structure with information about the specified
//	instruction address, 'addr'.  Returns 0 if information was found, and
//	negative if not.  But even if it returns negative it has stored some
//	information into '*info'.
//
int
debuginfo_eip(uintptr_t addr, eipdebuginfo_t *info)
{
	const stab_t *stab_end;
	const stab_t *stabs;
	const char *stabstr_end;
	const char *stabstr;
	int lfile, rfile, lfun, rfun, lline, rline;

	// Initialize *info
	info->eip_file = "<unknown>";
	info->eip_line = 0;
	info->eip_fn_name = "<unknown>";
	info->eip_fn_namelen = 9;
	info->eip_fn_addr = addr;
	info->eip_fn_narg = 0;

	// Find the relevant set of stabs
	if (addr >= ULIM) {
		stab_end = __STAB_END__;
		stabs = __STAB_BEGIN__;
		stabstr_end = __STABSTR_END__;
		stabstr = __STABSTR_BEGIN__;
	} else {
		/* TODO: short circuiting this, til our user space apps pack stab data
		 * the kernel knows about */
		return -1;
		#if 0
		// The user-application linker script, user/user.ld,
		// puts information about the application's stabs (equivalent
		// to __STAB_BEGIN__, __STAB_END__, __STABSTR_BEGIN__, and
		// __STABSTR_END__) in a structure located at virtual address
		// USTABDATA.
		const user_stab_data_t *usd = (const user_stab_data_t *)USTABDATA;

		// Make sure this memory is valid.
		// Return -1 if it is not.  Hint: Call user_mem_check.
		// LAB 3: Your code here.

		stab_end = usd->stab_end;
		stabs = usd->stabs;
		stabstr_end = usd->stabstr_end;
		stabstr = usd->stabstr;

		// Make sure the STABS and string table memory is valid.
		// LAB 3: Your code here.
		#endif
	}

	if (!stab_table_valid(stabstr, stabstr_end))
		return -1;

	// Now we find the right stabs that define the function containing
	// 'eip'.  First, we find the basic source file containing 'eip'.
	// Then, we look in that source file for the function.  Then we look
	// for the line number.

	// Search the entire set of stabs for the source file (type N_SO).
	lfile = 0;
	rfile = (stab_end - stabs) - 1;
	stab_binsearch(stabs, stab_end, &lfile, &rfile, N_SO, addr);
	if (lfile == 0)
		return -1;

	// Search within that file's stabs for the function definition
	// (N_FUN).
	lfun = lfile;
	rfun = rfile;
	stab_binsearch(stabs, stab_end, &lfun, &rfun, N_FUN, addr);

	if (lfun <= rfun) {
		// stabs[lfun] points to the function name
		// in the string table, but check bounds just in case.
		if (stabs[lfun].n_strx < stabstr_end - stabstr)
			info->eip_fn_name = stabstr + stabs[lfun].n_strx;
		info->eip_fn_addr = stabs[lfun].n_value;
		addr -= info->eip_fn_addr;
		// Search within the function definition for the line number.
		lline = lfun;
		rline = rfun;
	} else {
		// Couldn't find function stab!  Maybe we're in an assembly
		// file.  Search the whole file for the line number.
		info->eip_fn_addr = addr;
		lline = lfile;
		rline = rfile;
	}
	// Ignore stuff after the colon.
	info->eip_fn_namelen = strfind(info->eip_fn_name, ':') - info->eip_fn_name;

	// Search within [lline, rline] for the line number stab.
	// If found, set info->eip_line to the right line number.
	// If not found, return -1.
	//
	// Hint:
	//	There's a particular stabs type used for line numbers.
	//	Look at the STABS documentation and <inc/stab.h> to find
	//	which one.
	// Your code here.

	stab_binsearch(stabs, stab_end, &lline, &rline, N_SLINE, addr);
	if (lline <= rline)
		// stabs[lline] points to the line number
		info->eip_line = stabs[lline].n_value;
	else
		return -1;

	// Search backwards from the line number for the relevant filename
	// stab.
	// We can't just use the "lfile" stab because inlined functions
	// can interpolate code from a different file!
	// Such included source files use the N_SOL stab type.
	while (lline >= lfile
	       && stabs[lline].n_type != N_SOL
	       && (stabs[lline].n_type != N_SO || !stabs[lline].n_value))
		lline--;
	if (lline >= lfile && stabs[lline].n_strx < stabstr_end - stabstr)
		info->eip_file = stabstr + stabs[lline].n_strx;

	// Set eip_fn_narg to the number of arguments taken by the function,
	// or 0 if there was no containing function.
	// Your code here.
	info->eip_fn_narg = 0;
	if (lfun <= rfun) {
		lfun++;
		while (stabs[lfun++].n_type == N_PSYM)
			info->eip_fn_narg++;
	}

	return 0;
}

/* Returns a function pointer for a function name matching the given string. */
void *debug_get_fn_addr(char *fn_name)
{
	const struct stab *stab_end = __STAB_END__;
	const struct stab *stabs = __STAB_BEGIN__;
	const char *stabstr_end = __STABSTR_END__;
	const char *stabstr = __STABSTR_BEGIN__;

	static int first_fn_idx = 0;
	int i = first_fn_idx;
	int len;
	const char *stab_fn_name = 0;
	void *retval = 0;

	if (!stab_table_valid(stabstr, stabstr_end))
		return 0;

	for (/* i set */; &stabs[i] < stab_end; i++) {
		if (stabs[i].n_type != N_FUN)
			continue;
		first_fn_idx = first_fn_idx ? first_fn_idx : i;
		/* broken stab, just keep going */
		if (!(stabs[i].n_strx < stabstr_end - stabstr))
			continue;
		stab_fn_name = stabstr + stabs[i].n_strx;
		len = strfind(stab_fn_name, ':') - stab_fn_name;
		if (!len)
			continue;
		/* we have a match. */
		if (!strncmp(stab_fn_name, fn_name, len)) {
			printd("FN name: %s, Addr: %p\n", stab_fn_name, stabs[i].n_value);
			retval = (void*)stabs[i].n_value;
			break;
		}
	}
	return retval;
}

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
