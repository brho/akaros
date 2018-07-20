/* Copyright (c) 2013 The Regents of the University of California.
 * Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <multiboot.h>
#include <arena.h>

physaddr_t mefs_start;
size_t mefs_size;

/* Helper.  Adds free entries to the base arena.  Most entries are page aligned,
 * though on some machines below EXTPHYSMEM we may have some that aren't. */
static void parse_mboot_region(struct multiboot_mmap_entry *entry, void *data)
{
	physaddr_t boot_freemem_paddr = (physaddr_t)data;
	physaddr_t start = entry->addr;
	size_t len = entry->len;
	extern char end[];

// XXX
if (start == 0x0000000100000000) {
	mefs_start = (uintptr_t)KADDR(start);
	mefs_size = len;
	return;
}


	if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
		return;
	/* Skip anything over max_paddr - might be bad entries(?) */
	if (start >= max_paddr)
		return;
	if (start + len > max_paddr)
		len = max_paddr - start;
	/* For paranoia, skip anything below EXTPHYSMEM.  If we ever change this,
	 * we'll need to deal with smp_boot's trampoline page. */
	if ((start < EXTPHYSMEM) && (start + len < EXTPHYSMEM))
		return;
	if ((start < EXTPHYSMEM) && (EXTPHYSMEM <= start + len)) {
		len = start + len - EXTPHYSMEM;
		start = EXTPHYSMEM;
	}
	/* Skip over any pages already allocated in boot_alloc().
	 * (boot_freemem_paddr is the next free addr.) */
	if ((start < boot_freemem_paddr) && (boot_freemem_paddr <= start + len)) {
		len = start + len - boot_freemem_paddr;
		start = boot_freemem_paddr;
	}
	/* Skip any part that intersects with the kernel, which is linked and loaded
	 * from EXTPHYSMEM to end in kernel64.ld */
	if (regions_collide_unsafe(EXTPHYSMEM, PADDR(end), start, start + len)) {
		len = start + len - PADDR(end);
		start = PADDR(end);
	}
	/* We need to give the arena PGSIZE-quantum segments. */
	if (PGOFF(start)) {
		len -= PGOFF(start);
		start = ROUNDUP(start, PGSIZE);
	}
	len = ROUNDDOWN(len, PGSIZE);
	if (!len)
		return;
	arena_add(base_arena, KADDR(start), len, MEM_WAIT);
}

/* Since we can't parse multiboot mmap entries, we need to just guess at what
 * pages are free and which ones aren't.
 *
 * Despite the lack of info from mbi, I know there is a magic hole in physical
 * memory that we can't use, from the IOAPIC_PBASE on up [0xfec00000,
 * 0xffffffff] (I'm being pessimistic).  But, that's not pessimistic enough!
 * Qemu still doesn't like that.   From using 0xe0000000 instead works for mine.
 * According to http://wiki.osdev.org/Memory_Map_(x86), some systems could
 * reserve from [0xc0000000, 0xffffffff].  Anyway, in lieu of real memory
 * detection, I'm just skipping that entire region.
 *
 * We may or may not have more free memory above this magic hole, depending on
 * both the amount of RAM we have as well as 32 vs 64 bit.
 *
 * So we'll go with two free memory regions:
 *
 * 		[ 0, ROUNDUP(boot_freemem_paddr, PGSIZE) ) = busy
 * 		[ ROUNDUP(boot_freemem_paddr, PGSIZE), TOP_OF_1 ) = free
 * 		[ MAGIC_HOLE, 0x0000000100000000 ) = busy
 * 		(and maybe this:)
 * 		[ 0x0000000100000000, max_paddr ) = free
 *
 * where TOP_OF_1 is the min of IOAPIC_PBASE and max_paddr.
 *
 * As with parsing mbi regions, this will ignore the hairy areas below
 * EXTPHYSMEM, and mark the entire kernel and anything we've boot alloc'd as
 * busy. */
static void account_for_pages(physaddr_t boot_freemem_paddr)
{
	physaddr_t top_of_busy = ROUNDUP(boot_freemem_paddr, PGSIZE);
	physaddr_t top_of_free_1 = MIN(0xc0000000, max_paddr);
	physaddr_t start_of_free_2;

	printk("Warning: poor memory detection (qemu?).  May lose 1GB of RAM\n");
	arena_add(base_arena, KADDR(top_of_busy), top_of_free_1 - top_of_busy,
	          MEM_WAIT);
	/* If max_paddr is less than the start of our potential second free mem
	 * region, we can just leave.  We also don't want to poke around the pages
	 * array either (and accidentally run off the end of the array).
	 *
	 * Additionally, 32 bit doesn't acknowledge pmem above the 4GB mark. */
	start_of_free_2 = 0x0000000100000000;
	if (max_paddr < start_of_free_2)
		return;
	arena_add(base_arena, KADDR(start_of_free_2), max_paddr - start_of_free_2,
	          MEM_WAIT);
}

/* Initialize base arena based on available free memory.  After this, do not use
 * boot_alloc. */
void base_arena_init(struct multiboot_info *mbi)
{
	/* First, all memory is busy / not free by default.
	 *
	 * To avoid a variety of headaches, any memory below 1MB is considered busy.
	 * Likewise, everything in the kernel, up to _end is also busy.  And
	 * everything we've already boot_alloc'd is busy.  These chunks of memory
	 * are reported as 'free' by multiboot.  All of this memory is below
	 * boot_freemem_paddr.  We don't treat anything below that as free.
	 *
	 * We'll also abort the mapping for any addresses over max_paddr, since
	 * we'll never use them.  'pages' does not track them either.
	 *
	 * One special note: we actually use the memory at 0x1000 for smp_boot.
	 * It'll never get freed; just FYI. */
	physaddr_t boot_freemem_paddr;
	void *base_pg;

	/* Need to do the boot-allocs before our last look at the top of
	 * boot_freemem. */
	base_pg = boot_alloc(PGSIZE, PGSHIFT);
	base_arena = arena_builder(base_pg, "base", PGSIZE, NULL, NULL, NULL,
	                           0);
	boot_freemem_paddr = PADDR(ROUNDUP(boot_freemem, PGSIZE));
	if (mboot_has_mmaps(mbi)) {
		mboot_foreach_mmap(mbi, parse_mboot_region, (void*)boot_freemem_paddr);
	} else {
		/* No multiboot mmap regions (probably run from qemu with -kernel) */
		account_for_pages(boot_freemem_paddr);
	}
}
