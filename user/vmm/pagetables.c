/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Set up paging, using the minphys and maxphys in the vm struct. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ros/arch/mmu.h>
#include <vmm/vmm.h>
#include <vmm/util.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <parlib/uthread.h>
#include <parlib/arch/arch.h>

static bool debug;

struct ptp {
	uintptr_t pte[NPTENTRIES];
};

#define PAGE_RESOLUTION PML3_PTE_REACH

/* We put the page tables after 4Gb, where it exactly is doesn't matter as long
 * as it's accessible by the guest. */
#define PAGE_TABLE_ROOT_START 0x100000000

static void check_jumbo_pages(void *arg)
{
	uint32_t edx;

	parlib_cpuid(0x80000001, 0x0, NULL, NULL, NULL, &edx);
	if (!(edx & (1 << 26)))
		panic("1 GB Jumbo Pages are not supported on this hardware!");
}

/*
 * This function assumes that after the p512 page table, there is memory mapped
 * (though not necessarily populated) for each PML3 page table. This assumes
 * a total of 2M + 4K memory mapped. PML3 table n is located at 4K*(n+1) from
 * the start of the p512 table.
 * This function does a 1:1 mapping of va to pa. vm->root must be set
 * */
void add_pte_entries(struct virtual_machine *vm, uintptr_t start, uintptr_t end)
{
	struct ptp *p512;
	uintptr_t cur_page, aligned_start, aligned_end, pml4, pml3;
	static parlib_once_t once = PARLIB_ONCE_INIT;

	/* We check once if we can use 1Gb pages and die if we can't. */
	parlib_run_once(&once, check_jumbo_pages, NULL);

	uth_mutex_lock(&vm->mtx);
	p512 = vm->root;
	if (!p512)
		panic("vm->root page table pointer was not set!");

	/* We align the start down and the end up to make sure we cover the full
	 * area. */
	aligned_start = ALIGN_DOWN(start, PAGE_RESOLUTION);
	aligned_end = ALIGN(end, PAGE_RESOLUTION);

	cur_page = aligned_start;
	/* We always do end-1 because end from /proc/self/maps is not inclusive */
	for (pml4 = PML4(start); pml4 <= PML4(end - 1); pml4++) {
		struct ptp *p1 = p512 + pml4 + 1;

		/* Create the PML4 entry. Rather than check, I just overwrite it. */
		p512->pte[pml4] = (uintptr_t) p1 | PTE_KERN_RW;

		for (pml3 = PML3(cur_page); pml3 < NPTENTRIES &&
		     cur_page < aligned_end; pml3++, cur_page += PML3_PTE_REACH) {

			/* Create the PML3 entry. */
			p1->pte[pml3] = cur_page | PTE_KERN_RW | PTE_PS;
		}
	}
	uth_mutex_unlock(&vm->mtx);
}

/* This function sets up the default page tables for the guest. It parses
 * /proc/self/maps to figure out what pages are mapped for the uthread, and
 * sets up a 1:1 mapping for the vm guest. This function can be called
 * multiple times after startup to update the page tables, though regular
 * vmms should call add_pte_entries if they mmap something for the guest after
 * calling setup_paging to avoid having to parse /proc/self/maps again. */
void setup_paging(struct virtual_machine *vm)
{
	FILE *maps;
	char *line = NULL;
	size_t line_sz;
	char *strtok_save;
	char *p;
	uintptr_t first, second;

	/* How many page table pages do we need?
	 * If we create 1G PTEs for the whole space, it just takes 2M + 4k worth of
	 * memory. Perhaps we should just identity map the whole space upfront.
	 * Right now we don't MAP_POPULATE because we don't expect all the PTEs
	 * to be used. */
	if (!vm->root)
		vm->root = mmap((void *)PAGE_TABLE_ROOT_START, 0x201000,
		                PROT_READ | PROT_WRITE,
		                MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (vm->root == MAP_FAILED || (uintptr_t)vm->root >= BRK_START)
		panic("page table page alloc");

	/* We parse /proc/self/maps to figure out the currently mapped memory.
	 * This way all the memory that's available to the HR3 process is also
	 * reflected in the page tables. Our smallest PTEs here are 1Gb so there
	 * may be memory locations described in the page tables that are not
	 * mapped though. /proc/self/maps parsing code courtesy of Barret. */
	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		panic("unable to open /proc/self/maps");

	/* returns -1 on error or EOF. */
	while (getline(&line, &line_sz, maps) >= 0) {
		if (debug)
			fprintf(stderr, "Got line %s", line);

		p = strchr(line, ' ');
		/* No space, probably an error */
		if (!p)
			continue;
		*p = '\0';
		p = strtok_r(line, "-", &strtok_save);
		/* No first element! */
		if (!p)
			continue;
		first = strtoul(p, NULL, 16);
		p = strtok_r(NULL, "-", &strtok_save);
		/* No second element! */
		if (!p)
			continue;
		second = strtoul(p, NULL, 16);

		if (debug)
			printf("first %p, second %p\n\n", first, second);

		add_pte_entries(vm, first, second);

	}
	free(line);
	fclose(maps);
}
