/* Copyright (c) 2009,13 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. 
 *
 * Multiboot parsing. */

#include <multiboot.h>
#include <ros/common.h>
#include <arch/mmu.h>
#include <arch/arch.h>
#include <ros/memlayout.h>
#include <stdio.h>

#ifdef CONFIG_X86
#include <arch/apic.h>
#endif

physaddr_t maxpa;		/* Maximum physical address in the system */
physaddr_t maxaddrpa;	/* Maximum addressable physical address */
size_t npages;			/* Total number of physical memory pages */
size_t naddrpages;		/* num of addressable physical memory pages */

static size_t basemem;  /* Amount of base memory (in bytes) */
static size_t extmem;   /* Amount of extended memory (in bytes) */

/* This only notices bios detectable memory - there's a lot more in the higher
 * paddrs. */
void mboot_detect_memory(multiboot_info_t *mbi)
{
	if (!(mbi->flags & MULTIBOOT_INFO_MEMORY)) {
		printk("No BIOS memory info from multiboot, crash impending!\n");
		return;
	}
	/* mem_lower and upper are measured in KB.  They are 32 bit values, so we're
	 * limited to 4TB total. */
	size_t basemem = ROUNDDOWN((size_t)mbi->mem_lower * 1024, PGSIZE);
	size_t extmem = ROUNDDOWN((size_t)mbi->mem_upper * 1024, PGSIZE);
	/* Calculate the maximum physical address based on whether or not there is
	 * any extended memory. */
	if (extmem)
		maxpa = EXTPHYSMEM + extmem;
	else
		maxpa = basemem;
	npages = maxpa / PGSIZE;
	/* KERN_VMAP_TOP - KERNBASE is the max amount of virtual addresses we can
	 * use for the physical memory mapping (aka - the KERNBASE mapping) */
	maxaddrpa = MIN(maxpa, KERN_VMAP_TOP - KERNBASE);
	naddrpages = maxaddrpa / PGSIZE;
	printk("Physical memory: %luK available, ", maxpa / 1024);
	printk("base = %luK, extended = %luK\n", basemem / 1024, extmem / 1024);
	printk("Maximum directly addressable physical memory: %luK\n",
	       maxaddrpa / 1024);
}

/* TODO: Use the info from this for our free pages, instead of just using
 * the extended memory */
void mboot_print_mmap(multiboot_info_t *mbi)
{
	multiboot_memory_map_t *mmap_b, *mmap_e, *mmap_i;
	if (!(mbi->flags & MULTIBOOT_INFO_ELF_SHDR)) {
		printk("No memory mapping info from multiboot\n");
		return;
	}
	mmap_b = (multiboot_memory_map_t*)((size_t)mbi->mmap_addr + KERNBASE);
	mmap_e = (multiboot_memory_map_t*)((size_t)mbi->mmap_addr + KERNBASE
	                                   + mbi->mmap_length);
	printd("mmap_addr = %p, mmap_length = %p\n", mbi->mmap_addr,
	       mbi->mmap_length);
	printd("mmap_b %p, mmap_e %p\n", mmap_b, mmap_e);
	/* Note when we incremement mmap_i, we add in the value of size... */
	for (mmap_i = mmap_b;
	     mmap_i < mmap_e;
	     mmap_i = (multiboot_memory_map_t*)((void*)mmap_i + mmap_i->size
	                                        + sizeof(mmap_i->size))) {
		printk("base = 0x%016llx, length = 0x%016llx : %s\n",
		       mmap_i->addr, mmap_i->len,
		       mmap_i->type == MULTIBOOT_MEMORY_AVAILABLE ? "FREE" :
		                                                    "RESERVED");
	}
}

