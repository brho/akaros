/* Simple linker script for the ROS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

/* This script needs to be invoked with -z max-page-size=0x1000.  Otherwise,
 * ld will offset our first section to 1MB within the actual file.  Multiboot
 * requires the header to be in the first two pages. */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
ENTRY(_start)
KERN_LOAD_ADDR = 0xffffffffc0000000;

SECTIONS
{
	/* Entry Linked and loaded at 0x00100000 (includes multiboot) */
	. = 0x00100000;

	.bootstrap : {
		*(.boottext .bootdata .bootbss)
	}

	/* Needed this for syslinux, which was sitting around 16MB phys.  This just
	 * jumps us forward to the 32MB mark, both physically and virtually. */
	. = 0x02000000;

	/* Link the main kernel for the space after entry + KERN_LOAD_ADDR.  We'll
	 * still load it adjacent in physical memory */
	. += KERN_LOAD_ADDR;

	.text : AT(ADDR(.text) - KERN_LOAD_ADDR) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
		PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */
	}

	INCLUDE kern/linker_tables.ld
	INCLUDE kern/build_id.ld

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* TODO: add some debug info.  i hear stabs are 32 bit only, so we'll need
	 * to bring in some dwarves.  for now, hack in the symbols to compile. */
	.stab : {
		PROVIDE(__STAB_BEGIN__ = .);
		PROVIDE(__STAB_END__ = .);
	}
	.stabstr : {
		PROVIDE(__STABSTR_BEGIN__ = .);
		PROVIDE(__STABSTR_END__ = .);
	}

	/* Adjust the address for the data segment to the next page */
	. = ALIGN(0x1000);

	/* The data segment */
	.data : {
		*(.data)
	}

	.bss : {
		PROVIDE(__start_bss = .);
		*(.bss)
		*(COMMON)
		PROVIDE(__stop_bss = .);
	}

	/* 'end' isn't a real section, but everything needs a name.  It'll show up
	 * as 'N' (debug) in the nm / ksyms.map output. */
	.end : {
		PROVIDE(end = .);
	}

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
