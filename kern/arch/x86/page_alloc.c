/* Copyright (c) 2009 The Regents of the University  of California.
 * See the COPYRIGHT files at the top of this source tree for full
 * license information.
 *
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu> */

#include <sys/queue.h>
#include <page_alloc.h>
#include <pmap.h>
#include <kmalloc.h>
#include <multiboot.h>

/* Can do whatever here.  For now, our page allocator just works with colors,
 * not NUMA zones or anything. */
static void track_free_page(struct page *page)
{
	BSD_LIST_INSERT_HEAD(&page_free_list, page, pg_link);
	nr_free_pages++;
	page->pg_is_free = TRUE;
}

static struct page *pa64_to_page(uint64_t paddr)
{
	return &pages[paddr >> PGSHIFT];
}

static bool pa64_is_in_kernel(uint64_t paddr)
{
	extern char end[];
	/* kernel is linked and loaded here (in kernel{32,64}.ld */
	return (EXTPHYSMEM <= paddr) && (paddr < PADDR(end));
}

/* Helper.  For every page in the entry, this will determine whether or not the
 * page is free, and handle accordingly.  All pages are marked as busy by
 * default, and we're just determining which of them could be free. */
static void parse_mboot_region(struct multiboot_mmap_entry *entry, void *data)
{
	physaddr_t boot_freemem_paddr = (physaddr_t)data;
	bool in_bootzone = (entry->addr <= boot_freemem_paddr) &&
	                   (boot_freemem_paddr < entry->addr + entry->len);

	if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
		return;
	/* TODO: we'll have some issues with jumbo allocation */
	/* Most entries are page aligned, though on some machines below EXTPHYSMEM
	 * we may have some that aren't.  If two regions collide on the same page
	 * (one of them starts unaligned), we need to only handle the page once, and
	 * err on the side of being busy.
	 *
	 * Since these regions happen below EXTPHYSMEM, they are all marked busy (or
	 * else we'll panic).  I'll probably rewrite this for jumbos before I find a
	 * machine with unaligned mboot entries in higher memory. */
	if (PGOFF(entry->addr))
		assert(entry->addr < EXTPHYSMEM);
	for (uint64_t i = ROUNDDOWN(entry->addr, PGSIZE);
	     i < entry->addr + entry->len;
	     i += PGSIZE) {
		/* Skip pages we'll never map (above KERNBASE).  Once we hit one of
		 * them, we know the rest are too (for this entry). */
		if (i >= max_paddr)
			return;
		/* Mark low mem as busy (multiboot stuff is there, usually, too).  Since
		 * that memory may be freed later (like the smp_boot page), we'll treat
		 * it like it is busy/allocated. */
		if (i < EXTPHYSMEM)
			continue;
		/* Mark as busy pages already allocated in boot_alloc() */
		if (in_bootzone && (i < boot_freemem_paddr))
			continue;
		/* Need to double check for the kernel, in case it wasn't in the
		 * bootzone.  If it was in the bootzone, we already skipped it. */
		if (pa64_is_in_kernel(i))
			continue;
		track_free_page(pa64_to_page(i));
	}
}

/* Expect == 1 -> busy, 0 -> free */
static void check_range(uint64_t start, uint64_t end, int expect)
{
	int free;

	if (PGOFF(start))
		printk("Warning: check_range given unaligned addr 0x%016llx\n", start);
	for (uint64_t i = start; i < end; i += PGSIZE)  {
		free = pa64_to_page(i)->pg_is_free ? 0 : 1;
		if (free != expect) {
			printk("Error: while checking range [0x%016llx, 0x%016llx), "
			       "physaddr 0x%016llx free was %d, expected %d\n", start,
			       end, i, free, expect);
			panic("");
		}
	}
}

/* Note this doesn't check all of memory.  There are some chunks of 'memory'
 * that aren't reported by MB at all, like the VRAM sections at 0xa0000. */
static void check_mboot_region(struct multiboot_mmap_entry *entry, void *data)
{
	extern char end[];
	physaddr_t boot_freemem_paddr = (physaddr_t)data;
	bool in_bootzone = (entry->addr <= boot_freemem_paddr) &&
	                   (boot_freemem_paddr < entry->addr + entry->len);
	/* Need to deal with 32b wrap-around */
	uint64_t zone_end = MIN(entry->addr + entry->len, (uint64_t)max_paddr);

	if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) {
		check_range(entry->addr, zone_end, 1);
		return;
	}
	if (zone_end <= EXTPHYSMEM) {
		check_range(entry->addr, zone_end, 1);
		return;
	}
	/* this may include the kernel */
	if (in_bootzone) {
		/* boot_freemem might not be page aligned.  If it's part-way through a
		 * page, that page should be busy */
		check_range(entry->addr, ROUNDUP(PADDR(boot_freemem), PGSIZE), 1);
		check_range(ROUNDUP(PADDR(boot_freemem), PGSIZE), zone_end, 0);
		assert(zone_end == PADDR(boot_freelimit));
		return;
	}
	/* kernel's range (hardcoded in the linker script).  If we're checking now,
	 * it means the kernel is not in the same entry as the bootzone. */
	if (entry->addr == EXTPHYSMEM) {
		check_range(EXTPHYSMEM, PADDR(end), 1);
		check_range(ROUNDUP(PADDR(end), PGSIZE), zone_end, 0);
		return;
	}
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
 * For the busy regions, I don't actually need to mark the pages as busy.  They
 * were marked busy when the pages array was created (same as when we parse
 * multiboot info).  I'll just assert that they are properly marked as busy.
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
	for (physaddr_t i = 0; i < top_of_busy; i += PGSIZE)
		assert(!pa64_to_page(i)->pg_is_free);
	for (physaddr_t i = top_of_busy; i < top_of_free_1; i += PGSIZE)
		track_free_page(pa64_to_page(i));
	/* If max_paddr is less than the start of our potential second free mem
	 * region, we can just leave.  We also don't want to poke around the pages
	 * array either (and accidentally run off the end of the array).
	 *
	 * Additionally, 32 bit doesn't acknowledge pmem above the 4GB mark. */
	start_of_free_2 = 0x0000000100000000;
	if (max_paddr < start_of_free_2)
		return;
	for (physaddr_t i = top_of_free_1; i < start_of_free_2; i += PGSIZE)
		assert(!pa64_to_page(i)->pg_is_free);
	for (physaddr_t i = start_of_free_2; i < max_paddr; i += PGSIZE)
		track_free_page(pa64_to_page(i));
}

/* Initialize the memory free lists.  After this, do not use boot_alloc. */
void page_alloc_init(struct multiboot_info *mbi)
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
	 * It'll get set to 'used' like the others; just FYI.
	 *
	 * Finally, if we want to use actual jumbo page allocation (not just
	 * mapping), we need to round up _end, and make sure all of multiboot's
	 * sections are jumbo-aligned. */
	physaddr_t boot_freemem_paddr = PADDR(ROUNDUP(boot_freemem, PGSIZE));

	if (mboot_has_mmaps(mbi)) {
		mboot_foreach_mmap(mbi, parse_mboot_region, (void*)boot_freemem_paddr);
		/* Test the page alloc - if this gets slow, we can CONFIG it */
		mboot_foreach_mmap(mbi, check_mboot_region, (void*)boot_freemem_paddr);
	} else {
		/* No multiboot mmap regions (probably run from qemu with -kernel) */
		account_for_pages(boot_freemem_paddr);
	}
	printk("Number of free pages: %lu\n", nr_free_pages);
	printk("Page alloc init successful\n");
}
