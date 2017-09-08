/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * Memory, paging, e820, bootparams and other helpers */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ros/arch/mmu.h>
#include <vmm/linux_bootparam.h>
#include <vmm/vmm.h>
#include <err.h>
#include <vmm/util.h>
#include <parlib/ros_debug.h>
#include <fcntl.h>


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
void *init_e820map(struct virtual_machine *vm, struct boot_params *bp)
{
	uintptr_t memstart = vm->minphys;
	size_t memsize = vm->maxphys - vm->minphys + 1;
	uintptr_t lowmem = 0;

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

/* checkmemaligned verifies alignment attributes of your memory space.
 * It terminates your process with extreme prejudice if they are
 * incorrect in some way. */
void checkmemaligned(uintptr_t memstart, size_t memsize)
{
	if (!ALIGNED(memstart, PML1_REACH))
		errx(1, "memstart (%#x) wrong: must be aligned to %#x",
                     memstart, PML1_REACH);
	if (!ALIGNED(memsize, PML1_REACH))
		errx(1, "memsize (%#x) wrong: must be aligned to %#x",
                     memsize, PML1_REACH);
}

// memory allocates memory for the VM. It's a complicated mess because of the
// break for APIC and other things. We just go ahead and leave the region from
// RESERVED to _4GiB for that.  The memory is either split, all low, or all
// high. This code is designed for a kernel. Dune-style code does not need it
// as it does not have the RESERVED restrictions. Dune-style code can use this,
// however, by setting memstart to 4 GiB. This code can be called multiple
// times with more ranges. It does not check for overlaps.
void mmap_memory(struct virtual_machine *vm, uintptr_t memstart, size_t memsize)
{
	void *r1, *r2;
	unsigned long r1size = memsize;

	// Let's do some minimal validation, so we don't drive
	// people crazy.
	checkmemaligned(memstart, memsize);
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
		          MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (r2 != (void *)r2start) {
			fprintf(stderr,
			        "High region: Could not mmap 0x%lx bytes at 0x%lx\n",
			        memsize, r2start);
			exit(1);
		}
		if (memstart >= _4GiB)
			goto done;
	}

	r1 = mmap((void *)memstart, r1size,
	              PROT_READ | PROT_WRITE | PROT_EXEC,
	              MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (r1 != (void *)memstart) {
		fprintf(stderr, "Low region: Could not mmap 0x%lx bytes at 0x%lx\n",
		        memsize, memstart);
		exit(1);
	}

done:
	if ((vm->minphys == 0) || (vm->minphys > memstart))
		vm->minphys = memstart;

	if (vm->maxphys < memstart + memsize - 1)
		vm->maxphys = memstart + memsize - 1;
}

bool mmap_file(const char *path, uintptr_t memstart, uintptr_t memsize,
               uint64_t protections, uint64_t offset)
{
	int fd = open(path, O_RDONLY);

	if (fd == -1) {
		fprintf(stderr, "Unable to open %s for reading.\n", path);
		return false;
	}

	void *addr = mmap((void*) memstart, memsize, protections, MAP_PRIVATE,
	                  fd, offset);
	int err = errno;

	close(fd);

	if (addr == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap %s, got error %d\n", path, err);
		return false;
	}

	if ((uint64_t) addr != (uint64_t) memstart) {
		fprintf(stderr, "Could not mmap %s correctly.\n", path);
		if (munmap(addr, memsize) == -1)
			perror("Failed to unmap memory; leaking a mapping");
		return false;
	}
	return true;
}


/* populate_stack fills the stack with an argv, envp, and auxv.
 * We assume the stack pointer is backed by real memory.
 * It will go hard with you if it does not. For your own health,
 * stack should be 16-byte aligned. */
void *populate_stack(uintptr_t *stack, int argc, char *argv[],
                         int envc, char *envp[],
                         int auxc, struct elf_aux auxv[])
{
	/* Function to get the lengths of the argument and environment strings. */
	int get_lens(int argc, char *argv[], int arg_lens[])
	{
		int total = 0;

		if (!argc)
			return 0;
		for (int i = 0; i < argc; i++) {
			arg_lens[i] = strlen(argv[i]) + 1;
			total += arg_lens[i];
		}
		return total;
	}

	/* Function to help map the argument and environment strings, to their
	 * final location. */
	int remap(int argc, char *argv[], char *new_argv[],
              char new_argbuf[], int arg_lens[])
	{
		int offset = 0;

		if (!argc)
			return 0;
		for (int i = 0; i < argc; i++) {
			memcpy(new_argbuf + offset, argv[i], arg_lens[i]);
			fprintf(stderr, "data: memcpy(%p, %p, %ld)\n",
			        new_argbuf + offset, argv[i], arg_lens[i]);
			fprintf(stderr, "arg: set arg %d, @%p, to %p\n", i,
			        &new_argv[i], new_argbuf + offset);
			new_argv[i] = new_argbuf + offset;
			offset += arg_lens[i];
		}
		new_argv[argc] = NULL;
		return offset;
	}

	/* Start tracking the size of the buffer necessary to hold all of our data
	 * on the stack. Preallocate space for argc, argv, envp, and auxv in this
	 * buffer. */
	int bufsize = 0;

	bufsize += 1 * sizeof(size_t);
	bufsize += (auxc + 1) * sizeof(struct elf_aux);
	bufsize += (envc + 1) * sizeof(char**);
	bufsize += (argc + 1) * sizeof(char**);
	fprintf(stderr, "Bufsize for pointers and argc is %d\n", bufsize);

	/* Add in the size of the env and arg strings. */
	int arg_lens[argc];
	int env_lens[envc];

	bufsize += get_lens(argc, argv, arg_lens);
	bufsize += get_lens(envc, envp, env_lens);
	fprintf(stderr, "Bufsize for pointers, argc, and strings is %d\n",
	        bufsize);

	/* Adjust bufsize so that our buffer will ultimately be 16 byte aligned. */
	bufsize = (bufsize + 15) & ~0xf;
	fprintf(stderr,
	        "Bufsize for pointers, argc, and strings is rounded is %d\n",
	        bufsize);

	/* Set up pointers to all of the appropriate data regions we map to. */
	size_t *new_argc = (size_t*)((uint8_t*)stack - bufsize);
	char **new_argv = (char**)(new_argc + 1);
	char **new_envp = new_argv + argc + 1;
	struct elf_aux *new_auxv = (struct elf_aux*)(new_envp + envc + 1);
	char *new_argbuf = (char*)(new_auxv + auxc + 1);

	fprintf(stderr, "There are %d args, %d env, and %d aux\n", new_argc,
	        envc, auxc);
	fprintf(stderr, "Locations: argc: %p, argv: %p, envp: %p, auxv: %p\n",
	        new_argc, new_argv, new_envp, new_auxv);
	fprintf(stderr, "Locations: argbuf: %p, ", new_argbuf);
	fprintf(stderr, "Sizeof argc is %d\n", sizeof(size_t));
	/* Map argc into its final location. */
	*new_argc = argc;

	/* Map all data for argv and envp into its final location. */
	int offset = 0;

	offset = remap(argc, argv, new_argv, new_argbuf, arg_lens);
	if (offset == -1)
		return 0;
	fprintf(stderr, "Locations: argbuf: %p, envbuf: %p, ", new_argbuf,
	        new_argbuf + offset);
	offset = remap(envc, envp, new_envp, new_argbuf + offset, env_lens);
	if (offset == -1)
		return 0;

	/* Map auxv into its final location. */
	struct elf_aux null_aux = {0, 0};

	memcpy(new_auxv, auxv, auxc * sizeof(struct elf_aux));
	memcpy(new_auxv + auxc, &null_aux, sizeof(struct elf_aux));
	fprintf(stderr, "auxbuf: %p\n", new_auxv);
	hexdump(stdout, new_auxv, auxc * sizeof(struct elf_aux));
	return (uint8_t*)stack - bufsize;
}
