/* Copyright (c) 2009,13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. 
 *
 * Multiboot parsing and helper functions. */

#include <multiboot.h>
#include <ros/common.h>
#include <arch/mmu.h>
#include <arch/arch.h>
#include <ros/memlayout.h>
#include <stdio.h>
#include <pmap.h>

#ifdef CONFIG_X86
#include <arch/apic.h>
#endif

/* This only notices bios detectable memory - there's a lot more in the higher
 * paddrs. */
void mboot_detect_memory(struct multiboot_info *mbi)
{
	physaddr_t max_bios_mem;
	physaddr_t max_bios_addr;
	size_t basemem;
	size_t extmem;
	if (!(mbi->flags & MULTIBOOT_INFO_MEMORY)) {
		printk("No BIOS memory info from multiboot, crash impending!\n");
		return;
	}
	/* mem_lower and upper are measured in KB.  They are 32 bit values, so we're
	 * limited to 4TB total. */
	basemem = ROUNDDOWN((size_t)mbi->mem_lower * 1024, PGSIZE);
	extmem = ROUNDDOWN((size_t)mbi->mem_upper * 1024, PGSIZE);
	/* Calculate the maximum physical address based on whether or not there is
	 * any extended memory. */
	if (extmem)
		max_bios_mem = EXTPHYSMEM + extmem;
	else
		max_bios_mem = basemem;
	max_bios_addr = MIN(max_bios_mem, KERN_VMAP_TOP - KERNBASE);
	printk("Base memory: %luK, Extended memory: %luK\n", basemem / 1024,
	       extmem / 1024);
	printk("Maximum directly addressable base and extended memory: %luK\n",
	       max_bios_addr / 1024);
	/* Take a first stab at the max pmem, in case there are no memory mappings
	 * (like in riscv) */
	max_pmem = max_bios_mem;
}

void mboot_foreach_mmap(struct multiboot_info *mbi, mboot_foreach_t func,
                        void *data)
{
	struct multiboot_mmap_entry *mmap_b, *mmap_e, *mmap_i;
	if (!(mbi->flags & MULTIBOOT_INFO_ELF_SHDR)) {
		printk("No memory mapping info from multiboot\n");
		return;
	}
	mmap_b = (struct multiboot_mmap_entry*)((size_t)mbi->mmap_addr + KERNBASE);
	mmap_e = (struct multiboot_mmap_entry*)((size_t)mbi->mmap_addr + KERNBASE
	                                   + mbi->mmap_length);
	printd("mmap_addr = %p, mmap_length = %p\n", mbi->mmap_addr,
	       mbi->mmap_length);
	/* Note when we incremement mmap_i, we add in the value of size... */
	for (mmap_i = mmap_b;
	     mmap_i < mmap_e;
	     mmap_i = (struct multiboot_mmap_entry*)((void*)mmap_i + mmap_i->size
	                                             + sizeof(mmap_i->size))) {
		func(mmap_i, data);
	}
}

void mboot_print_mmap(struct multiboot_info *mbi)
{
	void print_entry(struct multiboot_mmap_entry *entry, void *data)
	{
		printk("Base = 0x%016llx, Length = 0x%016llx : %s\n",
		       entry->addr, entry->len,
		       entry->type == MULTIBOOT_MEMORY_AVAILABLE ? "FREE" :
		                                                   "RESERVED");
	}
	mboot_foreach_mmap(mbi, print_entry, 0);
}

/* Given a range of memory, will tell us if multiboot is using anything we care
 * about in that range.  It usually uses memory below 1MB, so boot_alloc is
 * fine.  This is pre, so MBI is still a paddr. */
bool mboot_region_collides(struct multiboot_info *mbi, uintptr_t base,
                           uintptr_t end)
{
	if (regions_collide_unsafe((uintptr_t)mbi,
	                           (uintptr_t)mbi + sizeof(struct multiboot_info),
	                           base, end))
		return TRUE;
	if ((mbi->flags & MULTIBOOT_INFO_ELF_SHDR)) {
		if (regions_collide_unsafe((uintptr_t)mbi->mmap_addr + KERNBASE,
		                           (uintptr_t)mbi->mmap_addr + KERNBASE
		                                                     + mbi->mmap_length,
		                           base, end))
			return TRUE;
	}
	return FALSE;
}
