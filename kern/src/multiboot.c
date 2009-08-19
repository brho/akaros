#include <multiboot.h>
#include <arch/types.h>
#include <arch/mmu.h>
#include <arch/arch.h>
#include <ros/memlayout.h>
#include <stdio.h>

#ifdef __i386__
#include <arch/apic.h>
#endif

// These variables are set by i386_detect_memory()
physaddr_t maxpa;// Maximum physical address
physaddr_t maxaddrpa;    // Maximum directly addressable physical address
void *SNT maxaddrpa_ptr;
size_t npage;   // Amount of physical memory (in pages)
size_t naddrpage;       // Amount of addressable physical memory (in pages)
static size_t basemem;  // Amount of base memory (in bytes)
static size_t extmem;   // Amount of extended memory (in bytes)

void
multiboot_detect_memory(multiboot_info_t *mbi)
{
	// Tells us how many kilobytes there are
	basemem = ROUNDDOWN(mbi->mem_lower*1024, PGSIZE);
	extmem = ROUNDDOWN(mbi->mem_upper*1024, PGSIZE);

	// Calculate the maximum physical address based on whether
	// or not there is any extended memory.  See comment in <inc/memlayout.h>
	if (extmem)
		maxpa = EXTPHYSMEM + extmem;
	else
		maxpa = basemem;

	npage = maxpa / PGSIZE;

	// IOAPIC - KERNBASE is the max amount of virtual addresses we can use
	// for the physical memory mapping (aka - the KERNBASE mapping)
	maxaddrpa = MIN(maxpa, IOAPIC_BASE - KERNBASE);
	maxaddrpa_ptr = (void *SNT)maxaddrpa;

	naddrpage = maxaddrpa / PGSIZE;

	cprintf("Physical memory: %dK available, ", (int)(maxpa/1024));
	cprintf("base = %dK, extended = %dK\n", (int)(basemem/1024), (int)(extmem/1024));
	printk("Maximum directly addressable physical memory: %dK\n", (int)(maxaddrpa/1024));
}

void
multiboot_print_memory_map(multiboot_info_t *mbi) {
	const char *NTS memory_type[] = {"", "FREE", "RESERVED", "UNDEFINED", "UNDEFINED4"};


	if(CHECK_FLAG(mbi->flags, 6)) {
		memory_map_t *SNT mmap_b =
			(memory_map_t *SNT)(mbi->mmap_addr + KERNBASE);
		memory_map_t *SNT mmap_e =
			(memory_map_t *SNT)(mbi->mmap_addr + KERNBASE + mbi->mmap_length);
		memory_map_t *BND(mmap_b, mmap_e) mmap = TC(mmap_b);

		cprintf ("mmap_addr = 0x%x, mmap_length = 0x%x\n", (unsigned long)mbi->mmap_addr,
		           (unsigned long)mbi->mmap_length);

		while(mmap < mmap_e) {
			cprintf ("base = 0x%08x%08x, length = 0x%08x%08x, type = %s\n",
			        (unsigned) mmap->base_addr_high,
			        (unsigned) mmap->base_addr_low,
			        (unsigned) mmap->length_high,
			        (unsigned) mmap->length_low,
			        (unsigned) memory_type[mmap->type]);
			mmap = (memory_map_t *BND(mmap_b,mmap_e))((char *BND(mmap_b,mmap_e))mmap + mmap->size + sizeof(mmap->size));
		}
	}
}

