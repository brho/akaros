/* Simple linker script for the ROS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
KERNBASE = 0xc0000000;

SECTIONS
{
	/* Entry Linked and loaded at 0x00100000 (includes multiboot) */
	. = 0x00100000;

	.bootstrap : {
		*(.boottext .bootdata)
	}

	/* Needed this for syslinux, which was sitting around 16MB phys.  This just
	 * jumps us forward to the 32MB mark, both physically and virtually. */
	. = 0x02000000;

	/* Link the main kernel for the space after entry + KERNBASE.  We'll still
	 * load it adjacent in physical memory */
	. += KERNBASE;

	.text : AT(ADDR(.text) - KERNBASE) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */

	INCLUDE kern/linker_tables.ld

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
	}

	PROVIDE(edata = .);

	.bss : {
		*(.bss)
		*(COMMON)
	}

	PROVIDE(end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
