/* this is INCLUDEd into the arch-specific ld scripts */

	/* Linker-made tables.  Our tables (e.g. devtab) are 2^5 aligned,
	 * independently of us aligning '.'.  We align '.' to get the right start,
	 * e.g.  __devtabstart. */
	. = ALIGN(32);
	/* We shouldn't have to use PROVIDE, but if we don't, we get the wrong
	 * value for '.'.  And items with empty tables get the KLA (basically 0) */
	PROVIDE(__devtabstart = .);
	PROVIDE(devtab = .);
	.devtab : {
		*(.devtab)
	}
	PROVIDE(__devtabend = .);

	. = ALIGN(32);
	PROVIDE(__linkerfunc1start = .);
	.linkerfunc1 : {
		*(.linkerfunc1)
	}
	PROVIDE(__linkerfunc1end = .);

	. = ALIGN(32);
	PROVIDE(__linkerfunc2start = .);
	.linkerfunc2 : {
		*(.linkerfunc2)
	}
	PROVIDE(__linkerfunc2end = .);

	. = ALIGN(32);
	PROVIDE(__linkerfunc3start = .);
	.linkerfunc3 : {
		*(.linkerfunc3)
	}
	PROVIDE(__linkerfunc3end = .);

	. = ALIGN(32);
	PROVIDE(__linkerfunc4start = .);
	.linkerfunc4 : {
		*(.linkerfunc4)
	}
	PROVIDE(__linkerfunc4end = .);
