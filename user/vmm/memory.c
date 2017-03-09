/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Memory, paging, e820, bootparams and other helpers */

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/vmm.h>
#include <vmm/acpi/acpi.h>
#include <ros/arch/mmu.h>
#include <ros/arch/membar.h>
#include <ros/vmm.h>
#include <parlib/uthread.h>
#include <vmm/linux_bootparam.h>
#include <getopt.h>

#include <vmm/sched.h>
#include <vmm/net.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <parlib/opts.h>

static char *entrynames[] = {
	[E820_RAM] "E820_RAM",
	[E820_RESERVED] "E820_RESERVED",
	[E820_ACPI] "E820_ACPI",
	[E820_NVS] "E820_NVS",
	[E820_UNUSABLE] "E820_UNUSABLE",
};

static void dumpe820(struct e820entry *e, int nr)
{
	for (int i = 0; i < nr; i++) {
		fprintf(stderr, "%d:%p %p %p %s\n",
		        i, e[i].addr, e[i].size, e[i].type,
		        entrynames[e[i].type]);
	}
}

// e820map creates an e820 map in the bootparams struct.  If we've
// gotten here, then memsize and memstart are valid.  It returns
// pointer to the first page after the map for our bump allocator.  We
// assume the ranges passed in are validated already.
void *init_e820map(struct boot_params *bp,
                   unsigned long long memstart,
                   unsigned long long memsize)
{
	unsigned long long lowmem = 0;
	// Everything in Linux at this level is PGSIZE.
	memset(bp, 0, PGSIZE);

	bp->e820_entries = 0;

	// The first page is always reserved.
	bp->e820_map[bp->e820_entries].addr = 0;
	bp->e820_map[bp->e820_entries].size = PGSIZE;
	bp->e820_map[bp->e820_entries++].type = E820_RESERVED;

	/* Give it just a tiny bit of memory -- 60k -- at low memory. */
	bp->e820_map[bp->e820_entries].addr = PGSIZE;
	bp->e820_map[bp->e820_entries].size = LOW64K - PGSIZE;
	bp->e820_map[bp->e820_entries++].type = E820_RAM;

	// All other memory from 64k to memstart is reserved.
	bp->e820_map[bp->e820_entries].addr = LOW64K;
	bp->e820_map[bp->e820_entries].size = memstart - LOW64K;
	bp->e820_map[bp->e820_entries++].type = E820_RESERVED;

	// If memory starts below RESERVED, then add an entry for memstart to
	// the smaller of RESERVED or memsize.
	if (memstart < RESERVED) {
		bp->e820_map[bp->e820_entries].addr = memstart;
		if (memstart + memsize > RESERVED)
			bp->e820_map[bp->e820_entries].size = RESERVED - memstart;
		else
			bp->e820_map[bp->e820_entries].size = memsize;
		lowmem = bp->e820_map[bp->e820_entries].size;
		bp->e820_map[bp->e820_entries++].type = E820_RAM;
	}

	bp->e820_map[bp->e820_entries].addr = RESERVED;
	bp->e820_map[bp->e820_entries].size = RESERVEDSIZE;
	bp->e820_map[bp->e820_entries++].type = E820_RESERVED;

	if ((memstart + memsize) > RESERVED) {
		bp->e820_map[bp->e820_entries].addr = MAX(memstart, _4GiB);
		bp->e820_map[bp->e820_entries].size = memsize - lowmem;
		bp->e820_map[bp->e820_entries++].type = E820_RAM;
	}

	dumpe820(bp->e820_map, bp->e820_entries);
	return (void *)bp + PGSIZE;
}

// memory allocates memory for the VM. It's a complicated mess because of the
// break for APIC and other things. We just go ahead and leave the region from
// RESERVED to _4GiB for that.  The memory is either split, all low, or all
// high.
void mmap_memory(unsigned long long memstart, unsigned long long memsize)
{
	void *r1, *r2;
	unsigned long r1size = memsize;

	// Let's do some minimal validation, so we don't drive
	// people crazy.
	if ((memstart >= RESERVED) && (memstart < _4GiB))
		errx(1, "memstart (%#x) wrong: must be < %#x or >= %#x\n",
		     memstart, RESERVED, _4GiB);
	if (memstart < MinMemory)
		errx(1, "memstart (%#x) wrong: must be > %#x\n",
		     memstart, MinMemory);

	// Note: this test covers the split case as well as the
	// 'all above 4G' case.
	if ((memstart + memsize) > RESERVED) {
		unsigned long long r2start = MAX(memstart, _4GiB);

		r1size = memstart < RESERVED ? RESERVED - memstart : 0;
		r2 = mmap((void *)r2start, memsize - r1size,
		          PROT_READ | PROT_WRITE | PROT_EXEC,
		          MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
		if (r2 != (void *)r2start) {
			fprintf(stderr,
			        "High region: Could not mmap 0x%lx bytes at 0x%lx\n",
			        memsize, r2start);
			exit(1);
		}
		if (memstart >= _4GiB)
			return;
	}

	r1 = mmap((void *)memstart, r1size,
	              PROT_READ | PROT_WRITE | PROT_EXEC,
	              MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	if (r1 != (void *)memstart) {
		fprintf(stderr, "Low region: Could not mmap 0x%lx bytes at 0x%lx\n",
		        memsize, memstart);
		exit(1);
	}
}
