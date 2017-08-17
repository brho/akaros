/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Set up paging, using the minphys and maxphys in the vm struct. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ros/arch/mmu.h>
#include <vmm/vmm.h>

typedef struct {
	uint64_t pte[512];
} ptp;

void *setup_paging(struct virtual_machine *vm, bool debug)
{
	ptp *p512, *p1, *p2m;
	int nptp, npml4, npml3, npml2;
	uintptr_t memstart = vm->minphys;
	size_t memsize = vm->maxphys - vm->minphys + 1;

	/* This test is redundant when booting kernels, as it is also
	 * performed in memory(), but not all users call that function,
	 * hence we must do it here too. */
	checkmemaligned(memstart, memsize);

	/* How many page table pages do we need?  We conservatively
	 * assume that we are in low memory, and hence assume a
	 * 0-based range.  Note that in many cases, kernels will
	 * immediately set up their own map. But for "dune" like
	 * applications, it's necessary. Note also that in most cases,
	 * the total number of pages will be < 16 or so. */
	npml4 = DIV_ROUND_UP(memstart + memsize, PML4_REACH);
	nptp = npml4;

	npml3 = DIV_ROUND_UP(memstart + memsize, PML3_REACH);
	nptp += npml3;

	/* and 1 for each 2 MiB of memory */
	npml2 = DIV_ROUND_UP(memstart + memsize, PML2_REACH);
	nptp += npml2;

	fprintf(stderr,
	        "Memstart is %llx, memsize is %llx, memstart + memsize is %llx; ",
	        memstart, memsize, memstart + memsize);
	fprintf(stderr, " %d pml4 %d pml3 %d pml2\n",
	        npml4, npml3, npml2);

	/* Place these page tables right after VM memory. We
	 * used to use posix_memalign but that puts them
	 * outside EPT-accessible space on some CPUs. */
	p512 = mmap((void *)memstart + memsize, nptp * 4096, PROT_READ | PROT_WRITE,
	             MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p512 == MAP_FAILED) {
		perror("page table page alloc");
		exit(1);
	}
	p1 = &p512[npml4];
	p2m = &p1[npml3];

	/* Set up a 1:1 ("identity") page mapping from guest virtual
	 * to guest physical using the (host virtual)
	 * `kerneladdress`. This mapping may be used for only a short
	 * time, until the guest sets up its own page tables. Be aware
	 * that the values stored in the table are physical addresses.
	 * This is subtle and mistakes are easily disguised due to the
	 * identity mapping, so take care when manipulating these
	 * mappings. */

	p2m->pte[PML2(0)] = (uint64_t)0 | PTE_KERN_RW | PTE_PS;

	fprintf(stderr, "Map %p for %zu bytes\n", memstart, memsize);
	for (uintptr_t p4 = memstart, i4 = PML4(p4);
		 p4 < memstart + memsize && i4 < NPTENTRIES;
	     p4 = ROUNDUP(p4 + 1, PML4_PTE_REACH), p1++, i4++) {
		p512->pte[PML4(p4)] = (uint64_t)p1 | PTE_KERN_RW;
		if (debug)
			fprintf(stderr, "l4@%p: %p set index 0x%x to 0x%llx\n",
					&p512->pte[PML4(p4)],
					p4, PML4(p4), p512->pte[PML4(p4)]);
		for (uintptr_t p3 = p4, i3 = PML3(p3);
			 p3 < memstart + memsize && i3 < NPTENTRIES;
		     p3 = ROUNDUP(p3 + 1, PML3_PTE_REACH), p2m++, i3++) {
			p1->pte[PML3(p3)] = (uint64_t)p2m | PTE_KERN_RW;
			if (debug)
				fprintf(stderr, "\tl3@%p: %p set index 0x%x to 0x%llx\n",
						&p1->pte[PML3(p3)],
						p3, PML3(p3), p1->pte[PML3(p3)]);
			for (uintptr_t p2 = p3, i2 = PML2(p2);
				 p2 < memstart + memsize && i2 < NPTENTRIES;
			     p2 += PML2_PTE_REACH, i2++) {
				p2m->pte[PML2(p2)] = (uint64_t)p2 | PTE_KERN_RW | PTE_PS;
				if (debug)
					fprintf(stderr, "\t\tl2@%p: %p set index 0x%x to 0x%llx\n",
							&p2m->pte[PML2(p2)],
							p2, PML2(p2), p2m->pte[PML2(p2)]);
			}
		}

	}

	return (void *)p512;
}
