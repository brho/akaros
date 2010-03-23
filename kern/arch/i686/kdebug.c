#ifdef __SHARC__
#pragma nosharc
#endif

#include <stab.h>
#include <string.h>
#include <assert.h>
#include <kdebug.h>
#include <pmap.h>
#include <process.h>

#include <ros/memlayout.h>

// Beginning of stabs table
extern const stab_t (RO BND(__this,__STAB_END__) __STAB_BEGIN__)[];

// End of stabs table
extern const stab_t (RO SNT __STAB_END__)[];

// Beginning of string table
extern const char (RO NT BND(__this,__STABSTR_END__) __STABSTR_BEGIN__)[];

 // End of string table
extern const char (RO SNT __STABSTR_END__)[];

typedef struct UserStabData {
	const stab_t *BND(__this,stab_end) stabs;
	const stab_t *SNT stab_end;
	const char *NT BND(__this, stabstr_end) stabstr;
	const char *SNT stabstr_end;
} user_stab_data_t;


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
stab_binsearch(const stab_t *BND(__this, stab_end) stabs,
           const stab_t *SNT stab_end,
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
debuginfo_eip(uintptr_t addr, eipdebuginfo_t *NONNULL info)
{
	const stab_t *SNT stab_end;
	const stab_t *BND(__this,stab_end) stabs;
	const char *SNT stabstr_end;
	const char *NT BND(__this,stabstr_end) stabstr;
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
		// The user-application linker script, user/user.ld,
		// puts information about the application's stabs (equivalent
		// to __STAB_BEGIN__, __STAB_END__, __STABSTR_BEGIN__, and
		// __STABSTR_END__) in a structure located at virtual address
		// USTABDATA.
		const user_stab_data_t *usd = (const user_stab_data_t *COUNT(1))TC(USTABDATA);

		// Make sure this memory is valid.
		// Return -1 if it is not.  Hint: Call user_mem_check.
		// LAB 3: Your code here.

		stab_end = usd->stab_end;
		stabs = usd->stabs;
		stabstr_end = usd->stabstr_end;
		stabstr = usd->stabstr;

		// Make sure the STABS and string table memory is valid.
		// LAB 3: Your code here.
	}

	// String table validity checks
	{
		int stabstrsz = stabstr_end - stabstr;
		if (stabstr_end <= stabstr || stabstr[stabstrsz-1] != 0)
			return -1;
	}

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
	const struct stab *SNT stab_end = __STAB_END__;
	const struct stab *BND(__this,stab_end) stabs = __STAB_BEGIN__;
	const char *SNT stabstr_end = __STABSTR_END__;
	const char *NT BND(__this,stabstr_end) stabstr = __STABSTR_BEGIN__;

	static int first_fn_idx = 0;
	int i = first_fn_idx;
	int len;
	const char *stab_fn_name = 0;
	void *retval = 0;

	// String table validity checks (from above)
	{
		int stabstrsz = stabstr_end - stabstr;
		if (stabstr_end <= stabstr || stabstr[stabstrsz-1] != 0)
			return 0;
	}

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
