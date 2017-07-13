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
#include <iplib/iplib.h>
#include <vmm/sched.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <err.h>
#include <vmm/linuxemu.h>

struct vmm_gpcore_init gpci;
bool linuxemu(struct guest_thread *gth, struct vm_trapframe *tf);

/* ah, elf. */
struct elf_aux {
	unsigned long v[2];
};

extern char **environ;

static struct virtual_machine vm = {.halt_exit = true,};

static unsigned long long memsize = GiB;
static uintptr_t memstart = MinMemory;

static int dune_debug;

static void hlt(void)
{
	__asm__ __volatile__("\thlt\n\t");
}

static int pc(char *c)
{
	__asm__ __volatile__("movq $1, %%rax\n"
	                     "movq $1, %%rdi\n"
	                     "movq %0, %%rsi\n"
	                     "movq $1, %%rdx\n"
	                     "vmcall\n" ::
	                     "m"(c) : "rdi", "rax", "rsi", "rdx");
	return 0;
}

static void xnum(uint64_t x)
{
	static char *hex = "0123456789abcdef";

	for (int i = 0; i < 8; i++) {
		uint8_t v = ((uint8_t*)&x)[7 - i];
		pc(&hex[v >> 4]);
		pc(&hex[v & 0xf]);
	}
}

static void show(char *s)
{
	static char *showedoff = "NULL POINTER: That's bad.\n";

	if (!s) {
		show(showedoff);
		return;
	}
	while (*s) {
		pc(s);
		s++;
	}
}

/* This is a small test that runs in gr0 and tests our argument setup.
 * This test can grow in capability as we find more broken bits in our
 * dune-like environment. */

void dune_test(void *stack)
{
	show("Hello this is dune's test\n");

	int argc;
	char **argv;
	struct elf_aux *auxv;

	show("dune_test: dumping argv, env, and aux\n");

	argc = *((uint64_t*)stack);
	argv = &((char**)stack)[1];
	show("argc: "); xnum(argc); show("\n");
	show("argv: "); xnum((uint64_t)argv); show("\n");

	for (int i = 0; i < argc; i++, argv++) {
		show("arg["); xnum(i); show("]:");
		show(argv[0]);
		show("\n");
	}
	// skip the null and move on to envp.
	argv++;
	for (int i = 0; argv[0]; i++, argv++) {
		show("env["); xnum(i); show("]:");
		show(argv[0]);
		show("\n");
	}
	// skip the null and move on to auxv.
	argv++;
	auxv = (void *)argv;
	for (int i = 0; auxv[i].v[0]; i++) {
		show("auxv["); xnum(i); show("]:");
		xnum(auxv[i].v[0]); show(":");
		xnum(auxv[i].v[1]); show("\n");
	}
	show("Done dumping [argv, env, auxv]\n");
	hlt();
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
			if (dune_debug) {
				fprintf(stderr, "data: memcpy(%p, %p, %ld)\n",
				        new_argbuf + offset, argv[i], arg_lens[i]);
				fprintf(stderr, "arg: set arg %d, @%p, to %p\n", i,
				        &new_argv[i], new_argbuf + offset);
			}
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
	if (dune_debug)
		fprintf(stderr, "Bufsize for pointers and argc is %d\n", bufsize);

	/* Add in the size of the env and arg strings. */
	int arg_lens[argc];
	int env_lens[envc];

	bufsize += get_lens(argc, argv, arg_lens);
	bufsize += get_lens(envc, envp, env_lens);
	if (dune_debug)
		fprintf(stderr, "Bufsize for pointers, argc, and strings is %d\n",
		        bufsize);

	/* Adjust bufsize so that our buffer will ultimately be 16 byte aligned. */
	bufsize = (bufsize + 15) & ~0xf;
	if (dune_debug)
		fprintf(stderr,
		        "Bufsize for pointers, argc, and strings is rounded is %d\n",
		        bufsize);

	/* Set up pointers to all of the appropriate data regions we map to. */
	size_t *new_argc = (size_t*)((uint8_t*)stack - bufsize);
	char **new_argv = (char**)(new_argc + 1);
	char **new_envp = new_argv + argc + 1;
	struct elf_aux *new_auxv = (struct elf_aux*)(new_envp + envc + 1);
	char *new_argbuf = (char*)(new_auxv + auxc + 1);

	if (dune_debug) {
		fprintf(stderr, "There are %d args, %d env, and %d aux\n", new_argc,
		        envc, auxc);
		fprintf(stderr, "Locations: argc: %p, argv: %p, envp: %p, auxv: %p\n",
				new_argc, new_argv, new_envp, new_auxv);
		fprintf(stderr, "Locations: argbuf: %p, ", new_argbuf);
		fprintf(stderr, "Sizeof argc is %d\n", sizeof(size_t));
	}
	/* Map argc into its final location. */
	*new_argc = argc;

	/* Map all data for argv and envp into its final location. */
	int offset = 0;

	offset = remap(argc, argv, new_argv, new_argbuf, arg_lens);
	if (offset == -1)
		return 0;
	if (dune_debug) {
		fprintf(stderr, "Locations: argbuf: %p, envbuf: %p, ", new_argbuf,
			new_argbuf + offset);

	}
	offset = remap(envc, envp, new_envp, new_argbuf + offset, env_lens);
	if (offset == -1)
		return 0;

	/* Map auxv into its final location. */
	struct elf_aux null_aux = {0, 0};

	memcpy(new_auxv, auxv, auxc * sizeof(struct elf_aux));
	memcpy(new_auxv + auxc, &null_aux, sizeof(struct elf_aux));
	if (dune_debug) {
		fprintf(stderr, "auxbuf: %p\n", new_auxv);
		hexdump(stdout, new_auxv, auxc * sizeof(struct elf_aux));
	}
	return (uint8_t*)stack - bufsize;
}

static struct option long_options[] = {
	{"aux",           required_argument, 0, 'a'},
	{"debug",         no_argument,       0, 'd'},
	{"vmmflags",      required_argument, 0, 'v'},
	{"memsize",       required_argument, 0, 'm'},
	{"memstart",      required_argument, 0, 'M'},
	{"cmdline_extra", required_argument, 0, 'c'},
	{"greedy",        no_argument,       0, 'g'},
	{"scp",           no_argument,       0, 's'},
	{"test",          no_argument,       0, 't'},
	{"help",          no_argument,       0, 'h'},
	{0, 0, 0, 0}
};

static void
usage(void)
{
	// Sadly, the getopt_long struct does
	// not have a pointer to help text.
	fprintf(stderr,
	      "Usage: dune [options] <ELF file] [<ELF file>...]\n");
	fprintf(stderr,
	      "Or for testing: dune -t [options]\nOptions are:\n");
	for (int i = 0;
	     i < COUNT_OF(long_options) - 1;
	     i++) {
		struct option *l = &long_options[i];

		fprintf(stderr, "%s or %c%s\n", l->name, l->val,
		        l->has_arg ? " <arg>" : "");
	}
	exit(0);
}

static struct elf_aux *
getextra(int *auxc, char *_s)
{
	struct elf_aux *auxv;
	char *s = strdup(_s);
	// icky hardcode, but realistic.
	char *auxpairs[32];

	*auxc = gettokens(s, auxpairs, 32, ",");
	if (dune_debug)
		fprintf(stderr, "Found %d extra aux pairs\n", *auxc);
	if (*auxc < 1)
		return NULL;
	auxv = malloc(sizeof(*auxv) * *auxc);
	if (!auxv)
		errx(1, "auxv malloc: %r");
	for (int i = 0; i < *auxc; i++) {
		char *aux[2];
		int j;
		uint32_t t, v;

		j = gettokens(auxpairs[i], aux, 2, "=");
		if (j < 2) {
			fprintf(stderr, "%s: should be in the form type=val\n",
			        auxpairs[i]);
			free(auxv);
			return NULL;
		}
		t = strtoul(aux[0], 0, 0);
		v = strtoul(aux[1], 0, 0);
		auxv[i].v[0] = t;
		auxv[i].v[1] = v;
		if (dune_debug)
			fprintf(stderr, "Adding aux pair 0x%x:0x%x\n", auxv[i].v[0],
			        auxv[i].v[1]);
	}
	return auxv;

}

static struct elf_aux *
buildaux(struct elf_aux *base, int basec, struct elf_aux *extra, int extrac)
{
	int total = basec + extrac;
	struct elf_aux *ret;

	ret = realloc(extra, total * sizeof(*ret));
	if (!ret)
		return NULL;

	if (dune_debug)
		fprintf(stderr, "buildaux: consolidating %d aux and %d extra\n",
			basec, extrac);
	/* TOOD: check for dups. */
	if (basec)
		memmove(&ret[extrac], base, sizeof(*base)*basec);
	return ret;
}

int main(int argc, char **argv)
{
	void *tos;
	int envc, auxc, extrac = 0;
	struct elf_aux *auxv, *extra = NULL;
	int vmmflags = 0;
	uint64_t entry = 0;
	int ret;
	struct vm_trapframe *vm_tf;
	int c;
	int test = 0;
	int option_index;
	int ac = argc;
	char **av = argv;

	fprintf(stderr, "%p %p %p %p\n", PGSIZE, PGSHIFT, PML1_SHIFT,
	        PML1_PTE_REACH);

	if ((uintptr_t)__procinfo.program_end >= MinMemory) {
		fprintf(stderr,
		        "Panic: vmrunkernel binary extends into guest memory\n");
		exit(1);
	}

	while ((c = getopt_long(argc, argv, "a:dv:m:M:gsth", long_options,
	                        &option_index)) != -1) {
		switch (c) {
		case 'a':
			extra = getextra(&extrac, optarg);
			if (dune_debug)
				fprintf(stderr, "Added %d aux items\n", extrac);
			break;
		case 'd':
			fprintf(stderr, "SET DEBUG\n");
			dune_debug++;
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
		case 'g':	/* greedy */
			parlib_never_yield = TRUE;
			break;
		case 's':	/* scp */
			parlib_wants_to_be_mcp = FALSE;
			break;
		case 't':
			test = 1;
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if ((!test) && (argc < 1)) {
		usage();
	}

	init_lemu_logging(dune_debug);
	init_syscall_table();

	if ((uintptr_t)(memstart + memsize) >= (uintptr_t)BRK_START) {
		fprintf(stderr,
		        "memstart 0x%lx memsize 0x%lx -> 0x%lx is too large; overlaps BRK_START at %p\n",
			memstart, memsize, memstart + memsize, BRK_START);
		exit(1);
	}

	mmap_memory(&vm, memstart, memsize);

	if (dune_debug)
		fprintf(stderr, "mmap guest physical memory at %p for 0x%lx bytes\n",
			memstart, memsize);

	// TODO: find out why we can't use memstart + memsize as TOS.
	tos = (void *)(memstart + 0x800000);

	for (envc = 0; environ[envc]; envc++)
		;
	auxv = (struct elf_aux *)&environ[envc+1];
	for (auxc = 0; auxv[auxc].v[0]; auxc++)
		;
	auxv = buildaux(auxv, auxc, extra, extrac);
	if (!auxv) {
		fprintf(stderr, "Can't build auxv: %r");
		exit(1);
	}
	auxc = auxc + extrac;

	if (!test) {
		entry = load_elf(argv[0], MinMemory, NULL);
		if (entry == 0) {
			fprintf(stderr, "Unable to load kernel %s\n", argv[0]);
			exit(1);
		}
	} else {
		fprintf(stderr, "Running dune test\n");
		entry = (uintptr_t) dune_test;
	}
	if (dune_debug)
		fprintf(stderr, "Test: Populate stack at %p\n", tos);
	tos = populate_stack(tos, ac, av, envc, environ, auxc, auxv);
	if (dune_debug)
		fprintf(stderr, "populated stack at %p; argc %d, envc %d, auxc %d\n",
		        tos, ac, envc, auxc);

	ret = vthread_attr_init(&vm, vmmflags);
	if (ret) {
		fprintf(stderr, "vmm_init failed: %r\n");
		exit(1);
	}

	vm.gths[0]->vmcall = linuxemu;
	vm_tf = gth_to_vmtf(vm.gths[0]);

	/* we can't use the default stack since we set one up
	 * ourselves. */
	vm_tf->tf_rsp = (uint64_t)tos;
	if (dune_debug)
		fprintf(stderr, "stack is %p\n", tos);

	vthread_create(&vm, 0, (void *)entry, tos);

	uthread_sleep_forever();
	return 0;
}
