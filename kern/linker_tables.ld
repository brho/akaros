/* this is INCLUDEd into the arch-specific ld scripts */

	/* Linker-made tables.  Our tables (e.g. devtab) are 2^6 aligned,
	 * independently of us aligning '.'.  We align '.' to get the right
	 * start, e.g.  __devtabstart. */
	. = ALIGN(64);
	/* We shouldn't have to use PROVIDE, but if we don't, we get the wrong
	 * value for '.'.  And items with empty tables get the KLA (basically
	 * 0) */
	.devtab : {
		PROVIDE(__devtabstart = .);
		PROVIDE(devtab = .);
		*(.devtab)
		PROVIDE(__devtabend = .);
	}

	/* Not sure if these need to be aligned to 64 bytes or not.  We had to
	 * change the alignment above for the devtab, so we just changed it here
	 * too, but it's unclear if this is 100% necessary.  In any event, it
	 * doesn't hurt anything, it just wastes some bytes in our binary. */
	. = ALIGN(64);
	.linkerfunc1 : {
		PROVIDE(__linkerfunc1start = .);
		*(.linkerfunc1)
		PROVIDE(__linkerfunc1end = .);
	}

	. = ALIGN(64);
	.linkerfunc2 : {
		PROVIDE(__linkerfunc2start = .);
		*(.linkerfunc2)
		PROVIDE(__linkerfunc2end = .);
	}

	. = ALIGN(64);
	.linkerfunc3 : {
		PROVIDE(__linkerfunc3start = .);
		*(.linkerfunc3)
		PROVIDE(__linkerfunc3end = .);
	}

	. = ALIGN(64);
	.linkerfunc4 : {
		PROVIDE(__linkerfunc4start = .);
		*(.linkerfunc4)
		PROVIDE(__linkerfunc4end = .);
	}
