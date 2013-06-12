/* Simple linker script for the ROS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

/* This script needs to be invoked with -z max-page-size=0x1000.  Otherwise,
 * ld will offset our first section to 1MB within the actual file.  Multiboot
 * requires the header to be in the first two pages. */

/* We're lying about our output format, so that grub thinks we are a 32 bit elf
 * and just loads our sections.  Since we're getting loaded into low memory, it
 * doesn't really matter (to grub) what our VMAs are.  Our LMAs are fine as
 * either 32 or 64 bit. */
OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386:x86-64)
/* This is for a pure 64 bit elf: 
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
*/
ENTRY(_start)
KERN_LOAD_ADDR = 0xffffffffc0000000;

SECTIONS
{
	/* Entry Linked and loaded at 0x00100000 (includes multiboot) */
	. = 0x00100000;

	.bootstrap : {
		*(.boottext .bootdata)
	}

	/* Link the main kernel for the space after entry + KERN_LOAD_ADDR.  We'll
	 * still load it adjacent in physical memory */
	. += KERN_LOAD_ADDR;

	.text : AT(ADDR(.text) - KERN_LOAD_ADDR) {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* TODO: add some debug info.  i hear stabs are 32 bit only, so we'll need
	 * to bring in some dwarves.  for now, hack in the symbols to compile. */
	PROVIDE(__STAB_BEGIN__ = .);
	PROVIDE(__STAB_END__ = .);
	PROVIDE(__STABSTR_BEGIN__ = .);
	PROVIDE(__STABSTR_END__ = .);

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
