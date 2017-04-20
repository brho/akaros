#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <gelf.h>
#include <errno.h>
#include <libelf.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/vmm.h>
#include <vmm/acpi/acpi.h>
#include <vmm/acpi/vmm_simple_dsdt.h>
#include <ros/arch/mmu.h>
#include <ros/arch/membar.h>
#include <ros/vmm.h>
#include <parlib/uthread.h>
#include <vmm/linux_bootparam.h>
#include <getopt.h>

#include <vmm/sched.h>
#include <sys/eventfd.h>
#include <sys/uio.h>

static struct virtual_machine local_vm, *vm = &local_vm;
struct vmm_gpcore_init gpci;
static void *ram;
static unsigned long long memsize = GiB;
static uintptr_t memstart = MinMemory;
static uintptr_t stack;
static unsigned long long *p512, *p1, *p2m;

static int debug = 0;

int main(int argc, char **argv)
{
	int vmmflags = VMM_VMCALL_PRINTF;
	uint64_t entry = 0;
	int ret;
	struct vm_trapframe *vm_tf;
	int c;
	int option_index;
	static struct option long_options[] = {
		{"debug",         no_argument,       0, 'd'},
		{"vmmflags",      required_argument, 0, 'v'},
		{"memsize",       required_argument, 0, 'm'},
		{"memstart",      required_argument, 0, 'M'},
		{"stack",         required_argument, 0, 'S'},
		{"cmdline_extra", required_argument, 0, 'c'},
		{"greedy",        no_argument,       0, 'g'},
		{"scp",           no_argument,       0, 's'},
		{"help",          no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	fprintf(stderr, "%p %p %p %p\n", PGSIZE, PGSHIFT, PML1_SHIFT,
			PML1_PTE_REACH);

	if ((uintptr_t)__procinfo.program_end >= MinMemory) {
		fprintf(stderr,
		        "Panic: vmrunkernel binary extends into guest memory\n");
		exit(1);
	}

	while ((c = getopt_long(argc, argv, "dv:m:M:S:gsh", long_options,
	                        &option_index)) != -1) {
		switch (c) {
			case 'd':
				debug++;
				break;
			case 'v':
				vmmflags = strtoull(optarg, 0, 0);
				break;
			case 'm':
				memsize = strtoull(optarg, 0, 0);
				break;
			case 'M':
				memstart = strtoull(optarg, 0, 0);
				break;
			case 'S':
				stack = strtoull(optarg, 0, 0);
				break;
			case 'g':	/* greedy */
				parlib_never_yield = TRUE;
				break;
			case 's':	/* scp */
				parlib_wants_to_be_mcp = FALSE;
				break;
			case 'h':
			default:
				// Sadly, the getopt_long struct does
				// not have a pointer to help text.
				for (int i = 0;
				    i < sizeof(long_options)/sizeof(long_options[0]) - 1;
				    i++) {
					struct option *l = &long_options[i];

					fprintf(stderr, "%s or %c%s\n", l->name, l->val,
						l->has_arg ? " <arg>" : "");
				}
				exit(0);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		fprintf(stderr, "Usage: %s vmimage [-n (no vmcall printf)]\n", argv[0]);
		exit(1);
	}

	if ((uintptr_t)(memstart + memsize) >= (uintptr_t)BRK_START) {
		fprintf(stderr,
		        "memstart 0x%lx memsize 0x%lx -> 0x%lx is too large; overlaps BRK_START at %p\n",
			memstart, memsize, memstart + memsize, BRK_START);
		exit(1);
	}

	ram = mmap((void *)memstart, memsize,
	           PROT_READ | PROT_WRITE | PROT_EXEC,
	           MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	if (ram != (void *)memstart) {
		fprintf(stderr, "Could not mmap 0x%lx bytes at 0x%lx\n",
		        memsize, memstart);
		exit(1);
	}

	entry = load_elf(argv[0]);
	if (entry == 0) {
		fprintf(stderr, "Unable to load kernel %s\n", argv[0]);
		exit(1);
	}

	vm->nr_gpcs = 1;
	vm->gpcis = &gpci;
	ret = vmm_init(vm, vmmflags);
	if (ret) {
		fprintf(stderr, "vmm_init failed: %r\n");
		exit(1);
	}

	/* Allocate 3 pages for page table pages: a page of 512 GiB
	 * PTEs with only one entry filled to point to a page of 1 GiB
	 * PTEs; a page of 1 GiB PTEs with only one entry filled to
	 * point to a page of 2 MiB PTEs; and a page of 2 MiB PTEs,
	 * all of which may be filled. For now, we don't handle
	 * starting addresses not aligned on 512 GiB boundaries or
	 * sizes > GiB */
	ret = posix_memalign((void **)&p512, PGSIZE, 3 * PGSIZE);
	if (ret) {
		perror("ptp alloc");
		exit(1);
	}

	/* Set up a 1:1 ("identity") page mapping from guest virtual
	 * to guest physical using the (host virtual)
	 * `kerneladdress`. This mapping may be used for only a short
	 * time, until the guest sets up its own page tables. Be aware
	 * that the values stored in the table are physical addresses.
	 * This is subtle and mistakes are easily disguised due to the
	 * identity mapping, so take care when manipulating these
	 * mappings. */
	p1 = &p512[NPTENTRIES];
	p2m = &p512[2 * NPTENTRIES];

	fprintf(stderr, "Map %p for %zu bytes\n", memstart, memsize);
	/* TODO: fix this nested loop so it's correct for more than
	 * one GiB. */
	for(uintptr_t p4 = memstart; p4 < memstart + memsize;
	    p4 += PML4_PTE_REACH) {
		p512[PML4(p4)] = (uint64_t)p1 | PTE_KERN_RW;
		for (uintptr_t p3 = p4; p3 < memstart + memsize;
		     p3 += PML3_PTE_REACH) {
			p1[PML3(p3)] = (uint64_t)p2m | PTE_KERN_RW;
			for (uintptr_t p2 = p3; p2 < memstart + memsize; p2 += PML2_PTE_REACH) {
				p2m[PML2(p2)] =
					(uint64_t)(p2) | PTE_KERN_RW | PTE_PS;
			}
		}
	}

	fprintf(stderr, "p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);

	vm_tf = gth_to_vmtf(vm->gths[0]);
	vm_tf->tf_cr3 = (uint64_t) p512;
	vm_tf->tf_rip = entry;
	vm_tf->tf_rsp = stack;
	vm_tf->tf_rsi = (uint64_t) 0;
	start_guest_thread(vm->gths[0]);

	uthread_sleep_forever();
	return 0;
}
