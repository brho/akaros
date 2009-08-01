#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/x86.h>
#include <arch/arch.h>
#include <elf.h>

/**********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an elf kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(boot.S and main.c) is the bootloader.  It should
 *    be stored in the first sector of the disk.
 * 
 *  * The 2nd sector onward holds the kernel image.
 *	
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS	
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive) 
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in bootloader.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls cmain()
 *
 *  * cmain() in this file takes over, reads in the kernel and jumps to it.
 **********************************************************************/

#define SECTSIZE	512
#define ELFHDR		((elf_t *) 0x10000) // scratch space

void readsect(void*, uint32_t);
void readseg(uint32_t, uint32_t, uint32_t);

void
cmain(void)
{
	proghdr_t *ph, *eph;

	// read 1st page off disk
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// is this a valid ELF?
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;

	// load each program segment (ignores ph flags)
	ph = (proghdr_t *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		readseg(ph->p_va, ph->p_memsz, ph->p_offset);

	// call the entry point from the ELF header
	// note: does not return!
	((void (*)(void)) (ELFHDR->e_entry & 0x0FFFFFFF))();

bad:
	outw(0x8A00, 0x8A00);
	outw(0x8A00, 0x8E00);
	while (1)
		/* do nothing */;
}

// Read 'count' bytes at 'offset' from kernel into virtual address 'va'.
// Might copy more than asked
void
readseg(uint32_t va, uint32_t count, uint32_t offset)
{
	uint32_t end_va;

	va &= 0x0FFFFFFF;
	end_va = va + count;
	
	// round down to sector boundary
	va &= ~(SECTSIZE - 1);

	// translate from bytes to sectors, and kernel starts at sector 1
	offset = (offset / SECTSIZE) + 1;

	// If this is too slow, we could read lots of sectors at a time.
	// We'd write more to memory than asked, but it doesn't matter --
	// we load in increasing order.
	while (va < end_va) {
		readsect((uint8_t*) va, offset);
		va += SECTSIZE;
		offset++;
	}
}

void
waitdisk(void)
{
	// wait for disk ready
	while ((inb(0x1F7) & 0xC0) != 0x40)
		/* do nothing */;
}

void
readsect(void *dst, uint32_t offset)
{
	// wait for disk to be ready
	waitdisk();

	/* the ISA uses a specified block of memory, 
	   addresses 0x1F0-0x1F7, that can use the special 
	   instructions inb/outb, as demonstrated in the 
	   following code in order to access the disk
	   Offset is 28 bytes long
	*/

	outb(0x1F2, 1);				// number of sectors to read
	outb(0x1F3, offset);			// bits 0-7 (low bits) of 28-bit offset
	outb(0x1F4, offset >> 8);		// bits 8-15 of 28-bit offset
	outb(0x1F5, offset >> 16);		// bits 16-23 of 28-bit offset
	outb(0x1F6, (offset >> 24) | 0xE0);	// bits 24-27 of 28-bit offset
						// bit 28 (= 0) means Disk 0
						// other bits (29-31) must be set to one
	outb(0x1F7, 0x20);			// cmd 0x20 - read sectors

	// wait for disk to be ready
	waitdisk();

	// read a sector
	insl(0x1F0, dst, SECTSIZE/4);
}

