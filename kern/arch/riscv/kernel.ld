/* Simple linker script for the ROS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
	/* Link the kernel for 0xFFFFFFFF80002000, but load it at 0x2000) */

	.text 0xFFFFFFFF80002000 : AT(0x2000) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
		PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */
	}

	INCLUDE kern/linker_tables.ld
	INCLUDE kern/build_id.ld

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* Include debugging information in kernel memory */
	.stab : {
		PROVIDE(stab = .);
		PROVIDE(__STAB_BEGIN__ = .);
		*(.stab);
		PROVIDE(estab = .);
		PROVIDE(__STAB_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	.stabstr : {
		PROVIDE(stabstr = .);
		PROVIDE(__STABSTR_BEGIN__ = .);
		*(.stabstr);
		PROVIDE(estabstr = .);
		PROVIDE(__STABSTR_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	/* Adjust the address for the data segment to the next page */
	. = ALIGN(0x1000);

	/* The data segment */
	.data : {
		*(.data)
		*(.sdata)
	}

	.bss : {
		PROVIDE(__start_bss = .);
		*(.bss)
		*(.sbss)
		PROVIDE(__stop_bss = .);
	}

	.end : {
		PROVIDE(end = .);
	}

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
