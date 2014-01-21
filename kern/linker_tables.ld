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
	PROVIDE(__devlinkstart = .);
	.devlink : {
		*(.devlink)
	}
	PROVIDE(__devlinkend = .);

	. = ALIGN(32);
	PROVIDE(__etherlinkstart = .);
	.etherlink : {
		*(.etherlink)
	}
	PROVIDE(__etherlinkend = .);

	. = ALIGN(32);
	PROVIDE(__mediastart = .);
	.media : {
		*(.media)
	}
	PROVIDE(__mediaend = .);
