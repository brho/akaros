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

#include <vmm/virtio.h>
#include <vmm/virtio_blk.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>
#include <vmm/virtio_console.h>
#include <vmm/virtio_net.h>
#include <vmm/virtio_lguest_console.h>

#include <vmm/sched.h>
#include <vmm/net.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <parlib/opts.h>

struct virtual_machine local_vm, *vm = &local_vm;

struct vmm_gpcore_init gpci;

/* By 1999, you could just scan the hardware
 * and work it out. But 2005, that was no longer possible. How sad.
 * so we have to fake acpi to make it all work.
 * This will be copied to memory at 0xe0000, so the kernel can find it.
 */

/* assume they're all 256 bytes long just to make it easy.
 * Just have pointers that point to aligned things.
 */

struct acpi_table_rsdp rsdp = {
	.signature = ACPI_SIG_RSDP,
	.oem_id = "AKAROS",
	.revision = 2,
	.length = 36,
};

struct acpi_table_xsdt xsdt = {
	.header = {
		.signature = ACPI_SIG_DSDT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
};
struct acpi_table_fadt fadt = {
	.header = {
		.signature = ACPI_SIG_FADT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
};


/* This has to be dropped into memory, then the other crap just follows it.
 */
struct acpi_table_madt madt = {
	.header = {
		.signature = ACPI_SIG_MADT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},

	.address = 0xfee00000ULL,
	.flags = 0,
};

struct acpi_madt_local_apic Apic0 = {.header = {.type = ACPI_MADT_TYPE_LOCAL_APIC, .length = sizeof(struct acpi_madt_local_apic)},
                                     .processor_id = 0, .id = 0, .lapic_flags = 1};
struct acpi_madt_io_apic Apic1 = {.header = {.type = ACPI_MADT_TYPE_IO_APIC, .length = sizeof(struct acpi_madt_io_apic)},
                                  .id = 0, .address = 0xfec00000, .global_irq_base = 0};
struct acpi_madt_local_x2apic X2Apic0 = {
	.header = {
		.type = ACPI_MADT_TYPE_LOCAL_X2APIC,
		.length = sizeof(struct acpi_madt_local_x2apic)
	},
	.local_apic_id = 0,
	.uid = 0
};

struct acpi_madt_interrupt_override isor[] = {
	/* From the ACPI Specification Version 6.1:
	 * For example, if your machine has the ISA Programmable Interrupt Timer
	 * (PIT) connected to ISA IRQ 0, but in APIC mode, it is connected to I/O
	 * APIC interrupt input 2, then you would need an Interrupt Source Override
	 * where the source entry is ‘0’ and the Global System Interrupt is ‘2.’
	 */
};


/* this test will run the "kernel" in the negative address space. We hope. */
void *low1m;
volatile int shared = 0;
volatile int quit = 0;

/* total hack. If the vm runs away we want to get control again. */
unsigned int maxresume = (unsigned int) -1;

#define MiB 0x100000ull
#define GiB (1ull << 30)
#define MinMemory (16*MiB)
void *kernel;
unsigned long long memsize = GiB;
uintptr_t memstart = MinMemory;
uintptr_t stack;

typedef struct {
	uint64_t pte[512];
} ptp;

ptp *p512, *p1, *p2m;

void **my_retvals;
int nr_threads = 4;
int debug = 0;
int resumeprompt = 0;
/* unlike Linux, this shared struct is for both host and guest. */
//	struct virtqueue *constoguest =
//		vring_new_virtqueue(0, 512, 8192, 0, inpages, NULL, NULL, "test");

void vapic_status_dump(FILE *f, void *vapic);

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1)
#error "Get a gcc newer than 4.4.0"
#else
#define BITOP_ADDR(x) "+m" (*(volatile long *) (x))
#endif

#define LOCK_PREFIX "lock "
#define ADDR				BITOP_ADDR(addr)
static inline int test_and_set_bit(int nr, volatile unsigned long *addr);

pthread_t timerthread_struct;

void timer_thread(void *arg)
{
	uint8_t vector;
	uint32_t initial_count;
	while (1) {
		vector = ((uint32_t *)gpci.vapic_addr)[0x32] & 0xff;
		initial_count = ((uint32_t *)gpci.vapic_addr)[0x38];
		if (vector && initial_count)
			vmm_interrupt_guest(vm, 0, vector);
		uthread_usleep(100000);
	}
	fprintf(stderr, "SENDING TIMER\n");
}


// FIXME.
volatile int consdata = 0;

/* TODO: pass a core id to poke_guest */
static void virtio_poke_guest(uint8_t vec)
{
	vmm_interrupt_guest(vm, 0, vec);
}

static struct virtio_mmio_dev cons_mmio_dev = {
	.poke_guest = virtio_poke_guest,
};

static struct virtio_console_config cons_cfg;
static struct virtio_console_config cons_cfg_d;

static struct virtio_vq_dev cons_vqdev = {
	.name = "console",
	.dev_id = VIRTIO_ID_CONSOLE,
	.dev_feat =
	(1ULL << VIRTIO_F_VERSION_1) | (1 << VIRTIO_RING_F_INDIRECT_DESC),
	.num_vqs = 2,
	.cfg = &cons_cfg,
	.cfg_d = &cons_cfg_d,
	.cfg_sz = sizeof(struct virtio_console_config),
	.transport_dev = &cons_mmio_dev,
	.vqs = {
		{
			.name = "cons_receiveq",
			.qnum_max = 64,
			.srv_fn = cons_receiveq_fn,
			.vqdev = &cons_vqdev
		},
		{
			.name = "cons_transmitq",
			.qnum_max = 64,
			.srv_fn = cons_transmitq_fn,
			.vqdev = &cons_vqdev
		},
	}
};

static struct virtio_mmio_dev net_mmio_dev = {
	.poke_guest = virtio_poke_guest,
};

static struct virtio_net_config net_cfg = {
	.max_virtqueue_pairs = 1
};
static struct virtio_net_config net_cfg_d = {
	.max_virtqueue_pairs = 1
};

static struct virtio_vq_dev net_vqdev = {
	.name = "network",
	.dev_id = VIRTIO_ID_NET,
	.dev_feat = (1ULL << VIRTIO_F_VERSION_1 | 1 << VIRTIO_NET_F_MAC),

	.num_vqs = 2,
	.cfg = &net_cfg,
	.cfg_d = &net_cfg_d,
	.cfg_sz = sizeof(struct virtio_net_config),
	.transport_dev = &net_mmio_dev,
	.vqs = {
		{
			.name = "net_receiveq",
			.qnum_max = 64,
			.srv_fn = net_receiveq_fn,
			.vqdev = &net_vqdev
		},
		{
			.name = "net_transmitq",
			.qnum_max = 64,
			.srv_fn = net_transmitq_fn,
			.vqdev = &net_vqdev
		},
	}
};

static struct virtio_mmio_dev blk_mmio_dev = {
	.poke_guest = virtio_poke_guest,
};

static struct virtio_blk_config blk_cfg = {
};

static struct virtio_blk_config blk_cfg_d = {
};

static struct virtio_vq_dev blk_vqdev = {
	.name = "block",
	.dev_id = VIRTIO_ID_BLOCK,
	.dev_feat = (1ULL << VIRTIO_F_VERSION_1),

	.num_vqs = 1,
	.cfg = &blk_cfg,
	.cfg_d = &blk_cfg_d,
	.cfg_sz = sizeof(struct virtio_blk_config),
	.transport_dev = &blk_mmio_dev,
	.vqs = {
		{
			.name = "blk_request",
			.qnum_max = 64,
			.srv_fn = blk_request,
			.vqdev = &blk_vqdev
		},
	}
};

void lowmem() {
	__asm__ __volatile__ (".section .lowmem, \"aw\"\n\tlow: \n\t.=0x1000\n\t.align 0x100000\n\t.previous\n");
}

static uint8_t acpi_tb_checksum(uint8_t *buffer, uint32_t length)
{
	uint8_t sum = 0;
	uint8_t *end = buffer + length;
	fprintf(stderr, "tbchecksum %p for %d", buffer, length);
	while (buffer < end) {
		if (end - buffer < 2)
			fprintf(stderr, "%02x\n", sum);
		sum = (uint8_t)(sum + *(buffer++));
	}
	fprintf(stderr, " is %02x\n", sum);
	return (sum);
}

static void gencsum(uint8_t *target, void *data, int len)
{
	uint8_t csum;
	// blast target to zero so it does not get counted
	// (it might be in the struct we checksum) And, yes, it is, goodness.
	fprintf(stderr, "gencsum %p target %p source %d bytes\n", target, data, len);
	*target = 0;
	csum  = acpi_tb_checksum((uint8_t *)data, len);
	*target = ~csum + 1;
	fprintf(stderr, "Cmoputed is %02x\n", *target);
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	int oldbit;

	asm volatile(LOCK_PREFIX "bts %2,%1\n\t"
	             "sbb %0,%0" : "=r" (oldbit), ADDR : "Ir" (nr) : "memory");

	return oldbit;
}

/* load_kernel loads an ELF file as a kernel. */
uintptr_t
load_kernel(char *filename)
{
	Elf64_Ehdr *ehdr;
	Elf *elf;
	size_t phnum = 0;
	Elf64_Phdr *hdrs;
	int fd;

	elf_version(EV_CURRENT);
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open %s: %r\n", filename);
		return 0;
	}

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL) {
		fprintf(stderr, "%s: cannot read %s ELF file.\n", __func__, filename);
		close(fd);
		return 0;
	}

	ehdr = elf64_getehdr(elf);
	if (ehdr == NULL) {
		fprintf(stderr, "%s: cannot get exec header of %s.\n",
		        __func__, filename);
		goto fail;
	}
	fprintf(stderr, "%s ELF entry point is %p\n", filename,
	        (void *)ehdr->e_entry);

	if (elf_getphdrnum(elf, &phnum) < 0) {
		fprintf(stderr, "%s: cannot get program header num of %s.\n",
		        __func__, filename);
		goto fail;
	}
	fprintf(stderr, "%s has %p program headers\n", filename, phnum);

	hdrs = elf64_getphdr(elf);
	if (hdrs == NULL) {
		fprintf(stderr, "%s: cannot get program headers of %s.\n",
		        __func__, filename);
		goto fail;
	}

	for (int i = 0; i < phnum; i++) {
		size_t tot;
		Elf64_Phdr *h = &hdrs[i];
		uintptr_t pa;

		fprintf(stderr,
		        "%d: type 0x%lx flags 0x%lx  offset 0x%lx vaddr 0x%lx paddr 0x%lx size 0x%lx  memsz 0x%lx align 0x%lx\n",
		        i,
		        h->p_type,		/* Segment type */
		        h->p_flags,		/* Segment flags */
		        h->p_offset,		/* Segment file offset */
		        h->p_vaddr,		/* Segment virtual address */
		        h->p_paddr,		/* Segment physical address */
		        h->p_filesz,		/* Segment size in file */
		        h->p_memsz,		/* Segment size in memory */
		        h->p_align		/* Segment alignment */);
		if (h->p_type != PT_LOAD)
			continue;
		if ((h->p_flags & (PF_R | PF_W | PF_X)) == 0)
			continue;

		pa = h->p_paddr;
		fprintf(stderr,
		        "Read header %d @offset %p to %p (elf PA is %p) %d bytes:",
		        i, h->p_offset, pa, h->p_paddr, h->p_filesz);
		tot = 0;
		while (tot < h->p_filesz) {
			int amt = pread(fd, (void *)(pa + tot), h->p_filesz - tot,
			                h->p_offset + tot);
			if (amt < 1)
				break;
			tot += amt;
		}
		fprintf(stderr, "read a total of %d bytes\n", tot);
		if (tot < h->p_filesz) {
			fprintf(stderr, "%s: got %d bytes, wanted %d bytes\n",
			        filename, tot, h->p_filesz);
			goto fail;
		}
	}

	close(fd);
	elf_end(elf);
	return ehdr->e_entry;
fail:
	close(fd);
	elf_end(elf);
	return 0;
}

/* TODO: put this in a library somewhere */
int cat(char *file, void *where)
{
	int fd;
	int amt, tot = 0;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		return -1;

	while (amt = read(fd, where, 4096)) {
		if (amt < 0) {
			close(fd);
			return -1;
		}
		tot += amt;
		where += amt;
	}
	close(fd);
	return tot;
}

int smbios(char *smbiostable, void *esegment)
{
	int amt;

	amt = cat(smbiostable, esegment);
	if (amt < 0) {
		fprintf(stderr, "%s: %r\n", smbiostable);
		exit(1);
	}

	return amt;
}

/* Parse func: given a line of text, it sets any vnet options */
static void __parse_vnet_opts(char *_line)
{
	char *eq, *spc;

	/* Check all bools first */
	if (!strcmp(_line, "snoop")) {
		vnet_snoop = TRUE;
		return;
	}
	if (!strcmp(_line, "map_diagnostics")) {
		vnet_map_diagnostics = TRUE;
		return;
	}
	if (!strcmp(_line, "real_address")) {
		vnet_real_ip_addrs = TRUE;
		return;
	}
	/* Numeric fields, must have an = */
	eq = strchr(_line, '=');
	if (!eq)
		return;
	*eq++ = 0;
	/* Drop spaces before =.  atoi trims any spaces after =. */
	while ((spc = strrchr(_line, ' ')))
		*spc = 0;
	if (!strcmp(_line, "nat_timeout")) {
		vnet_nat_timeout = atoi(eq);
		return;
	}
}

static void set_vnet_opts(char *net_opts)
{
	if (parse_opts_file(net_opts, __parse_vnet_opts))
		perror("parse opts file");
}

/* Parse func: given a line of text, it builds any vnet port forwardings. */
static void __parse_vnet_port_fwds(char *_line)
{
	char *tok, *tok_save = 0;
	char *proto, *host_port, *guest_port;

	tok = strtok_r(_line, ":", &tok_save);
	if (!tok)
		return;
	if (strcmp(tok, "port"))
		return;
	tok = strtok_r(NULL, ":", &tok_save);
	if (!tok) {
		fprintf(stderr, "%s, port with no proto!", __func__);
		return;
	}
	proto = tok;
	tok = strtok_r(NULL, ":", &tok_save);
	if (!tok) {
		fprintf(stderr, "%s, port with no host port!", __func__);
		return;
	}
	host_port = tok;
	tok = strtok_r(NULL, ":", &tok_save);
	if (!tok) {
		fprintf(stderr, "%s, port with no guest port!", __func__);
		return;
	}
	guest_port = tok;
	vnet_port_forward(proto, host_port, guest_port);
}

static void set_vnet_port_fwds(char *net_opts)
{
	if (parse_opts_file(net_opts, __parse_vnet_port_fwds))
		perror("parse opts file");
}

int main(int argc, char **argv)
{
	struct boot_params *bp;
	char cmdline_default[512] = {0};
	char *cmdline_extra = "\0";
	char *cmdline;
	void *a = (void *)0xe0000;
	struct acpi_table_rsdp *r;
	struct acpi_table_fadt *f;
	struct acpi_table_madt *m;
	struct acpi_table_xsdt *x;
	int vmmflags = 0; // Disabled probably forever. VMM_VMCALL_PRINTF;
	uint64_t entry = 0;
	int ret;
	uint8_t csum;
	void *a_page;
	struct vm_trapframe *vm_tf;
	uint64_t tsc_freq_khz;
	char *cmdlinep;
	int cmdlinesz, len, cmdline_fd;
	char *disk_image_file = NULL;
	int c;
	struct stat stat_result;
	int num_read;
	int option_index;
	char *smbiostable = NULL;
	int nptp, npml4, npml3, npml2;
	char *net_opts = NULL;

	static struct option long_options[] = {
		{"debug",         no_argument,       0, 'd'},
		{"vmm_vmcall",    no_argument,       0, 'v'},
		{"maxresume",     required_argument, 0, 'R'},
		{"memsize",       required_argument, 0, 'm'},
		{"memstart",      required_argument, 0, 'M'},
		{"stack",         required_argument, 0, 'S'},
		{"cmdline_extra", required_argument, 0, 'c'},
		{"greedy",        no_argument,       0, 'g'},
		{"scp",           no_argument,       0, 's'},
		{"image_file",    required_argument, 0, 'f'},
		{"cmdline",       required_argument, 0, 'k'},
		{"net",           required_argument, 0, 'n'},
		{"smbiostable",   required_argument, 0, 't'},
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

	vm->low4k = malloc(PGSIZE);
	memset(vm->low4k, 0xff, PGSIZE);
	vm->low4k[0x40e] = 0;
	vm->low4k[0x40f] = 0;

	//Place mmap(Gan)
	a_page = mmap((void *)0xfee00000, PGSIZE, PROT_READ | PROT_WRITE,
	              MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	fprintf(stderr, "a_page mmap pointer %p\n", a_page);

	if (a_page != (void *)0xfee00000) {
		perror("Could not mmap APIC");
		exit(1);
	}
	if (((uint64_t)a_page & 0xfff) != 0) {
		perror("APIC page mapping is not page aligned");
		exit(1);
	}

	((uint32_t *)a_page)[0x30/4] = 0x01060015;
	//((uint32_t *)a_page)[0x30/4] = 0xDEADBEEF;

	while ((c = getopt_long(argc, argv, "dvm:M:S:c:gsf:k:n:t:hR:",
				long_options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'v':
			vmmflags |= VMM_VMCALL_PRINTF;
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
		case 'R':
			maxresume = strtoull(optarg, 0, 0);
			break;
		case 'c':
			cmdline_extra = optarg;
		case 'g':	/* greedy */
			parlib_never_yield = TRUE;
			break;
		case 's':	/* scp */
			parlib_wants_to_be_mcp = FALSE;
			break;
		case 'f':	/* file to pass to blk_init */
			disk_image_file = optarg;
			break;
		case 'k':	/* specify file to get cmdline args from */
			cmdline_fd = open(optarg, O_RDONLY);
			if (cmdline_fd < 0) {
				fprintf(stderr, "failed to open file: %s\n", optarg);
				exit(1);
			}
			if (stat(optarg, &stat_result) == -1) {
				fprintf(stderr, "stat of %s failed\n", optarg);
				exit(1);
			}
			len = stat_result.st_size;
			if (len > 512) {
				fprintf(stderr, "command line options exceed 512 bytes!");
				exit(1);
			}
			num_read = read(cmdline_fd, cmdline_default, len);
			if (num_read != len) {
				fprintf(stderr, "read failed len was : %d, num_read was: %d\n",
				        len, num_read);
				exit(1);
			}
			close(cmdline_fd);
			break;
		case 't':
			smbiostable = optarg;
			break;
		case 'n':
			net_opts = optarg;
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
	if (strlen(cmdline_default) == 0) {
		fprintf(stderr, "WARNING: No command line parameter file specified.\n");
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

	kernel = mmap((void *)memstart, memsize,
	              PROT_READ | PROT_WRITE | PROT_EXEC,
	              MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	if (kernel != (void *)memstart) {
		fprintf(stderr, "Could not mmap 0x%lx bytes at 0x%lx\n",
		        memsize, memstart);
		exit(1);
	}

	entry = load_kernel(argv[0]);
	if (entry == 0) {
		fprintf(stderr, "Unable to load kernel %s\n", argv[0]);
		exit(1);
	}


	// The low 1m so we can fill in bullshit like ACPI. */
	// And, sorry, due to the STUPID format of the RSDP for now we need the low 1M.
	low1m = mmap((int*)4096, MiB-4096, PROT_READ | PROT_WRITE,
	             MAP_ANONYMOUS, -1, 0);
	if (low1m != (void *)4096) {
		perror("Unable to mmap low 1m");
		exit(1);
	}
	r = a;
	fprintf(stderr, "install rsdp to %p\n", r);
	*r = rsdp;
	a += sizeof(*r);
	r->xsdt_physical_address = (uint64_t)a;
	gencsum(&r->checksum, r, ACPI_RSDP_CHECKSUM_LENGTH);
	if ((csum = acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_CHECKSUM_LENGTH)) != 0) {
		fprintf(stderr, "RSDP has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	/* Check extended checksum if table version >= 2 */
	gencsum(&r->extended_checksum, r, ACPI_RSDP_XCHECKSUM_LENGTH);
	if ((rsdp.revision >= 2) &&
	    (acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_XCHECKSUM_LENGTH) != 0)) {
		fprintf(stderr, "RSDP has bad checksum v2\n");
		exit(1);
	}

	/* just leave a bunch of space for the xsdt. */
	/* we need to zero the area since it has pointers. */
	x = a;
	a += sizeof(*x) + 8*sizeof(void *);
	memset(x, 0, a - (void *)x);
	fprintf(stderr, "install xsdt to %p\n", x);
	*x = xsdt;
	x->table_offset_entry[0] = 0;
	x->table_offset_entry[1] = 0;
	x->header.length = a - (void *)x;

	f = a;
	fprintf(stderr, "install fadt to %p\n", f);
	*f = fadt;
	x->table_offset_entry[0] = (uint64_t)f; // fadt MUST be first in xsdt!
	a += sizeof(*f);
	f->header.length = a - (void *)f;

	f->Xdsdt = (uint64_t) a;
	fprintf(stderr, "install dsdt to %p\n", a);
	memcpy(a, &DSDT_DSDTTBL_Header, 36);
	a += 36;

	gencsum(&f->header.checksum, f, f->header.length);
	if (acpi_tb_checksum((uint8_t *)f, f->header.length) != 0) {
		fprintf(stderr, "fadt has bad checksum v2\n");
		exit(1);
	}

	m = a;
	*m = madt;
	x->table_offset_entry[3] = (uint64_t) m;
	a += sizeof(*m);
	fprintf(stderr, "install madt to %p\n", m);
	memmove(a, &Apic0, sizeof(Apic0));
	a += sizeof(Apic0);
	memmove(a, &Apic1, sizeof(Apic1));
	a += sizeof(Apic1);
	memmove(a, &X2Apic0, sizeof(X2Apic0));
	a += sizeof(X2Apic0);
	memmove(a, &isor, sizeof(isor));
	a += sizeof(isor);
	m->header.length = a - (void *)m;

	gencsum(&m->header.checksum, m, m->header.length);
	if (acpi_tb_checksum((uint8_t *) m, m->header.length) != 0) {
		fprintf(stderr, "madt has bad checksum v2\n");
		exit(1);
	}

	gencsum(&x->header.checksum, x, x->header.length);
	if ((csum = acpi_tb_checksum((uint8_t *) x, x->header.length)) != 0) {
		fprintf(stderr, "XSDT has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	fprintf(stderr, "allchecksums ok\n");

	hexdump(stdout, r, a-(void *)r);

	a = (void *)(((unsigned long)a + 0xfff) & ~0xfff);
	gpci.posted_irq_desc = a;
	memset(a, 0, 4096);
	a += 4096;
	gpci.vapic_addr = a;
	memset(a, 0, 4096);
	((uint32_t *)a)[0x30/4] = 0x01060014;
	// set up apic values? do we need to?
	// qemu does this.
	//((uint8_t *)a)[4] = 1;
	a += 4096;
	gpci.apic_addr = (void*)0xfee00000;

	/* Allocate memory for, and zero the bootparams
	 * page before writing to it, or Linux thinks
	 * we're talking crazy.
	 */
	a += 4096;
	bp = a;
	memset(bp, 0, 4096);

	/* Put the e820 memory region information in the boot_params */
	bp->e820_entries = 5;
	int e820i = 0;

	/* Give it just a tiny bit of memory -- 60k -- at low memory. */
	bp->e820_map[e820i].addr = 0;
	bp->e820_map[e820i].size = 4 * 1024;
	bp->e820_map[e820i++].type = E820_RESERVED;

	bp->e820_map[e820i].addr = 4 * 1024;
	bp->e820_map[e820i].size = 64 * 1024 - 4 * 1024;
	bp->e820_map[e820i++].type = E820_RAM;

	bp->e820_map[e820i].addr = 64 * 1024;
	bp->e820_map[e820i].size = memstart - 64 * 1024;
	bp->e820_map[e820i++].type = E820_RESERVED;

	bp->e820_map[e820i].addr = memstart;
	bp->e820_map[e820i].size = memsize;
	bp->e820_map[e820i++].type = E820_RAM;

	bp->e820_map[e820i].addr = 0xf0000000;
	bp->e820_map[e820i].size = 0x10000000;
	bp->e820_map[e820i++].type = E820_RESERVED;

	/* The MMIO address of the console device is really the address of an
	 * unbacked EPT page: accesses to this page will cause a page fault that
	 * traps to the host, which will examine the fault, see it was for the
	 * known MMIO address, and fulfill the MMIO read or write on the guest's
	 * behalf accordingly. We place the virtio space at 512 GB higher than the
	 * guest physical memory to avoid a full page table walk. */
	uint64_t virtio_mmio_base_addr = ROUNDUP((bp->e820_map[e820i - 1].addr +
	                                          bp->e820_map[e820i - 1].size),
	                                         512 * GiB);

	cons_mmio_dev.addr =
		virtio_mmio_base_addr + PGSIZE * VIRTIO_MMIO_CONSOLE_DEV;
	cons_mmio_dev.vqdev = &cons_vqdev;
	vm->virtio_mmio_devices[VIRTIO_MMIO_CONSOLE_DEV] = &cons_mmio_dev;

	net_mmio_dev.addr =
		virtio_mmio_base_addr + PGSIZE * VIRTIO_MMIO_NETWORK_DEV;
	net_mmio_dev.vqdev = &net_vqdev;
	vm->virtio_mmio_devices[VIRTIO_MMIO_NETWORK_DEV] = &net_mmio_dev;

	if (disk_image_file != NULL) {
		blk_mmio_dev.addr =
			virtio_mmio_base_addr + PGSIZE * VIRTIO_MMIO_BLOCK_DEV;
		blk_mmio_dev.vqdev = &blk_vqdev;
		vm->virtio_mmio_devices[VIRTIO_MMIO_BLOCK_DEV] = &blk_mmio_dev;
		blk_init_fn(&blk_vqdev, disk_image_file);
	}

	set_vnet_opts(net_opts);
	vnet_init(vm, &net_vqdev);
	set_vnet_port_fwds(net_opts);

	/* Set the kernel command line parameters */
	a += 4096;
	cmdline = a;
	a += 4096;

	if (smbiostable) {
		if (a > (void *)0xf0000) {
			fprintf(stderr, "No room for SMBIOS table: current table pointer is %p\n",
				a);
			exit(1);
		}
		a = (void *)0xf0000;
		fprintf(stderr, "Using SMBIOS table %s\n", smbiostable);
		a += smbios(smbiostable, a);
	}

	bp->hdr.cmd_line_ptr = (uintptr_t) cmdline;

	tsc_freq_khz = get_tsc_freq()/1000;
	len = snprintf(cmdline, 4096, "%s tscfreq=%lld %s", cmdline_default,
	               tsc_freq_khz, cmdline_extra);

	cmdlinesz = 4096 - len;
	cmdlinep = cmdline + len;

	for (int i = 0; i < VIRTIO_MMIO_MAX_NUM_DEV; i++) {
		if (vm->virtio_mmio_devices[i] == NULL)
			continue;

		/* Append all the virtio mmio base addresses. */

		/* Since the lower number irqs are no longer being used, the irqs
		 * can now be assigned starting from 0.
		 */
		vm->virtio_mmio_devices[i]->irq = i;
		len = snprintf(cmdlinep, cmdlinesz,
		               " virtio_mmio.device=1K@0x%llx:%lld",
		               vm->virtio_mmio_devices[i]->addr,
		               vm->virtio_mmio_devices[i]->irq);
		if (len >= cmdlinesz) {
			fprintf(stderr, "Too many arguments to the linux command line.");
			exit(1);
		}
		cmdlinesz -= len;
		cmdlinep += len;
	}

	vm->nr_gpcs = 1;
	vm->gpcis = &gpci;
	ret = vmm_init(vm, vmmflags);
	assert(!ret);

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

	fprintf(stderr, "Memstart + memsize is %llx; %d pml4 %d pml3 %d pml2\n",
		memstart + memsize, npml4, npml3, npml2);

	/* Place these page tables right after VM memory. We
	 * used to use posix_memalign but that puts them
	 * outside EPT-accessible space on some CPUs. */
	p512 = mmap((void *)memstart + memsize, nptp * 4096, PROT_READ | PROT_WRITE,
	             MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
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
	for (uintptr_t p4 = memstart; p4 < memstart + memsize;
	     p4 += PML4_PTE_REACH, p1++) {
		p512->pte[PML4(p4)] = (uint64_t)p1 | PTE_KERN_RW;
		if (debug)
			fprintf(stderr, "l4@%p: %p set index 0x%x to 0x%llx\n",
				&p512->pte[PML4(p4)],
				p4, PML4(p4), p512->pte[PML4(p4)]);
		for (uintptr_t p3 = p4; p3 < memstart + memsize;
		     p3 += PML3_PTE_REACH, p2m++) {
			p1->pte[PML3(p3)] = (uint64_t)p2m | PTE_KERN_RW;
			if (debug)
				fprintf(stderr, "\tl3@%p: %p set index 0x%x to 0x%llx\n",
				&p1->pte[PML3(p3)],
				p3, PML3(p3), p1->pte[PML3(p3)]);
			for (uintptr_t p2 = p3; p2 < memstart + memsize;
			     p2 += PML2_PTE_REACH) {
				p2m->pte[PML2(p2)] = (uint64_t)p2 | PTE_KERN_RW | PTE_PS;
				if (debug)
					fprintf(stderr, "\t\tl2@%p: %p set index 0x%x to 0x%llx\n",
						&p2m->pte[PML2(p2)],
						p2, PML2(p2), p2m->pte[PML2(p2)]);
			}
		}

	}

	vmm_run_task(vm, timer_thread, 0);

	vm_tf = gth_to_vmtf(vm->gths[0]);
	vm_tf->tf_cr3 = (uint64_t) p512;
	vm_tf->tf_rip = entry;
	vm_tf->tf_rsp = stack;
	vm_tf->tf_rsi = (uint64_t) bp;
	start_guest_thread(vm->gths[0]);

	uthread_sleep_forever();
	return 0;
}
