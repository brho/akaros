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
#include <vmm/coreboot_tables.h>
#include <vmm/vmm.h>
#include <vmm/acpi/acpi.h>
#include <ros/arch/mmu.h>
#include <ros/vmm.h>
#include <parlib/uthread.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

int msrio(struct vmctl *vcpu, uint32_t opcode);

struct vmctl vmctl;

/* Kind of sad what a total clusterf the pc world is. By 1999, you could just scan the hardware 
 * and work it out. But 2005, that was no longer possible. How sad. 
 * so we have to fake acpi to make it all work. !@#$!@#$#.
 * This will be copied to memory at 0xe0000, so the kernel can find it.
 */
/* assume they're all 256 bytes long just to make it easy. Just have pointers that point to aligned things. */

struct acpi_table_rsdp rsdp = {
	.signature = "RSD PTR ",
	.oem_id = "AKAROS",
	.revision = 2,
	.length = 36,
};

struct acpi_table_xsdt xsdt = {
	.header = {
		.signature= "XSDT",
		// This is so stupid. Incredibly stupid.
		.revision = 0,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
};
struct acpi_table_fadt fadt = {
	.header = {
		.signature= "FADT",
		// This is so stupid. Incredibly stupid.
		.revision = 0,
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
		.signature = "APIC",
		.revision = 0,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
	
	.address = 0xfee00000ULL,
};

struct acpi_madt_local_apic Apic0 = {.header = {.type = ACPI_MADT_TYPE_LOCAL_APIC, .length = sizeof(struct acpi_madt_local_apic)},
				     .processor_id = 0, .id = 0};
struct acpi_madt_io_apic Apic1 = {.header = {.type = ACPI_MADT_TYPE_IO_APIC, .length = sizeof(struct acpi_madt_io_apic)},
				  .id = 1, .address = 0xfec00000, .global_irq_base = 0};
struct acpi_madt_interrupt_override isor[] = {
	/* I have no idea if it should be source irq 2, global 0, or global 2, source 0. Shit. */
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 2, .global_irq = 0, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 1, .global_irq = 1, .inti_flags = 0},
	//{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 //.bus = 0, .source_irq = 2, .global_irq = 2, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 3, .global_irq = 3, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 4, .global_irq = 4, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 5, .global_irq = 5, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 6, .global_irq = 6, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 7, .global_irq = 7, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 8, .global_irq = 8, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 9, .global_irq = 9, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 10, .global_irq = 10, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 11, .global_irq = 11, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 12, .global_irq = 12, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 13, .global_irq = 13, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 14, .global_irq = 14, .inti_flags = 0},
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 15, .global_irq = 15, .inti_flags = 0},
	// VMMCP routes irq 32 to gsi 17
	{.header = {.type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, .length = sizeof(struct acpi_madt_interrupt_override)},
	 .bus = 0, .source_irq = 32, .global_irq = 17, .inti_flags = 5},
};


/* this test will run the "kernel" in the negative address space. We hope. */
void *low1m;
uint8_t low4k[4096];
unsigned long long stack[1024];
volatile int shared = 0;
volatile int quit = 0;
int mcp = 1;
int virtioirq = 17;

/* total hack. If the vm runs away we want to get control again. */
unsigned int maxresume = (unsigned int) -1;

#define MiB 0x100000u
#define GiB (1u<<30)
#define GKERNBASE (16*MiB)
#define KERNSIZE (128*MiB+GKERNBASE)
uint8_t _kernel[KERNSIZE];

unsigned long long *p512, *p1, *p2m;

void **my_retvals;
int nr_threads = 3;
int debug = 0;
int resumeprompt = 0;
/* unlike Linux, this shared struct is for both host and guest. */
//	struct virtqueue *constoguest = 
//		vring_new_virtqueue(0, 512, 8192, 0, inpages, NULL, NULL, "test");
uint64_t virtio_mmio_base = 0x100000000ULL;

void vapic_status_dump(FILE *f, void *vapic);
static void set_posted_interrupt(int vector);

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1)
#error "Get a gcc newer than 4.4.0"
#else
#define BITOP_ADDR(x) "+m" (*(volatile long *) (x))
#endif

#define LOCK_PREFIX "lock "
#define ADDR				BITOP_ADDR(addr)
static inline int test_and_set_bit(int nr, volatile unsigned long *addr);

void *consout(void *arg)
{
	char *line, *consline, *outline;
	static struct scatterlist out[] = { {NULL, sizeof(outline)}, };
	static struct scatterlist in[] = { {NULL, sizeof(line)}, };
	static struct scatterlist iov[32];
	struct virtio_threadarg *a = arg;
	static unsigned int inlen, outlen, conslen;
	struct virtqueue *v = a->arg->virtio;
	fprintf(stderr, "talk thread ..\n");
	uint16_t head, gaveit = 0, gotitback = 0;
	uint32_t vv;
	int i;
	int num;
	if (debug) {
		fprintf(stderr, "----------------------- TT a %p\n", a);
		fprintf(stderr, "talk thread ttargs %x v %x\n", a, v);
	}
	
	for(num = 0;;num++) {
		//int debug = 1;
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(v, iov, &outlen, &inlen);
		if (debug)
			fprintf(stderr, "CCC: vq desc head %d, gaveit %d gotitback %d\n", head, gaveit, gotitback);
		for(i = 0; debug && i < outlen + inlen; i++)
			fprintf(stderr, "CCC: v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);
		/* host: if we got an output buffer, just output it. */
		for(i = 0; i < outlen; i++) {
			num++;
			int j;
			if (debug) {
				fprintf(stderr, "CCC: IOV length is %d\n", iov[i].length);
			}
			for (j = 0; j < iov[i].length; j++)
				printf("%c", ((char *)iov[i].v)[j]);
		}
		fflush(stdout);
		if (debug)
			fprintf(stderr, "CCC: outlen is %d; inlen is %d\n", outlen, inlen);
		/* host: fill in the writeable buffers. */
		/* why we're getting these I don't know. */
		for (i = outlen; i < outlen + inlen; i++) {
			if (debug) fprintf(stderr, "CCC: send back empty writeable");
			iov[i].length = 0;
		}
		if (debug) fprintf(stderr, "CCC: call add_used\n");
		/* host: now ack that we used them all. */
		add_used(v, head, outlen+inlen);
		if (debug) fprintf(stderr, "CCC: DONE call add_used\n");
	}
	fprintf(stderr, "All done\n");
	return NULL;
}

// FIXME. 
volatile int consdata = 0;

void *consin(void *arg)
{
	struct virtio_threadarg *a = arg;
	char *line, *outline;
	static char consline[128];
	static struct scatterlist iov[32];
	static struct scatterlist out[] = { {NULL, sizeof(outline)}, };
	static struct scatterlist in[] = { {NULL, sizeof(line)}, };

	static unsigned int inlen, outlen, conslen;
	struct virtqueue *v = a->arg->virtio;
	fprintf(stderr, "consin thread ..\n");
	uint16_t head, gaveit = 0, gotitback = 0;
	uint32_t vv;
	int i;
	int num;
	//char c[1];

	int fd = open("#cons/vmctl", O_RDWR), ret;
	
	if (debug) fprintf(stderr, "Spin on console being read, print num queues, halt\n");

	for(num = 0;! quit;num++) {
		//int debug = 1;
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(v, iov, &outlen, &inlen);
		if (debug)
			fprintf(stderr, "vq desc head %d, gaveit %d gotitback %d\n", head, gaveit, gotitback);
		for(i = 0; debug && i < outlen + inlen; i++)
			fprintf(stderr, "v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);
		if (debug)
			fprintf(stderr, "outlen is %d; inlen is %d\n", outlen, inlen);
		/* host: fill in the writeable buffers. */
		for (i = outlen; i < outlen + inlen; i++) {
			/* host: read a line. */
			memset(consline, 0, 128);
			if (read(0, consline, 1) < 0) {
				exit(0);
			} 
			if (debug) fprintf(stderr, "CONSIN: GOT A LINE:%s:\n", consline);
			if (debug) fprintf(stderr, "CONSIN: OUTLEN:%d:\n", outlen);
			if (strlen(consline) < 3 && consline[0] == 'q' ) {
				quit = 1;
				break;
			}

			memmove(iov[i].v, consline, strlen(consline)+ 1);
			iov[i].length = strlen(consline) + 1;
		}
		if (debug) fprintf(stderr, "call add_used\n");
		/* host: now ack that we used them all. */
		add_used(v, head, outlen+inlen);
		consdata = 1;
		if (debug) fprintf(stderr, "DONE call add_used\n");

		// Send spurious for testing (Gan)
		set_posted_interrupt(0xE5);
		virtio_mmio_set_vring_irq();

		pwrite(fd, &vmctl, sizeof(vmctl), 1<<12);
	}
	fprintf(stderr, "All done\n");
	return NULL;
}

static struct vqdev vqdev= {
name: "console",
dev: VIRTIO_ID_CONSOLE,
device_features: 0, /* Can't do it: linux console device does not support it. VIRTIO_F_VERSION_1*/
numvqs: 2,
vqs: {
		{name: "consin", maxqnum: 64, f: consin, arg: (void *)0},
		{name: "consout", maxqnum: 64, f: consout, arg: (void *)0},
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
	// blast target to zero so it does not get counted (it might be in the struct we checksum) 
	// And, yes, it is, goodness.
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

static void pir_dump()
{
	unsigned long *pir_ptr = (unsigned long *)vmctl.pir;
	int i;
	fprintf(stderr, "-------Begin PIR dump-------\n");
	for (i = 0; i < 8; i++){
		fprintf(stderr, "Byte %d: 0x%016x\n", i, pir_ptr[i]);
	}
	fprintf(stderr, "-------End PIR dump-------\n");
}

static void set_posted_interrupt(int vector)
{
	unsigned long *bit_vec;
	int bit_offset;
	int i, j;
	unsigned long *pir = (unsigned long *)vmctl.pir;
	// Move to the correct location to set our bit.
	bit_vec = pir + vector/(sizeof(unsigned long)*8);
	bit_offset = vector%(sizeof(unsigned long)*8);
	if(debug) fprintf(stderr, "%s: Pre set PIR dump\n", __func__);
	if(debug) pir_dump();
	if(debug) vapic_status_dump(stderr, (void *)vmctl.vapic);
	if(debug) fprintf(stderr, "%s: Setting pir bit offset %d at 0x%p\n", __func__,
			bit_offset, bit_vec);
	test_and_set_bit(bit_offset, bit_vec);

	// Set outstanding notification bit
	/*bit_vec = pir + 4;
	fprintf(stderr, "%s: Setting pir bit offset 0 at 0x%p", __func__,
			bit_vec);
	test_and_set_bit(0, bit_vec);*/

	if(debug) pir_dump();
}

int main(int argc, char **argv)
{
	uint64_t *p64;
	void *a = (void *)0xe0000;
	struct acpi_table_rsdp *r;
	struct acpi_table_fadt *f;
	struct acpi_table_madt *m;
	struct acpi_table_xsdt *x;
	uint64_t virtiobase = 0x100000000ULL;
	// lowmem is a bump allocated pointer to 2M at the "physbase" of memory 
	void *lowmem = (void *) 0x1000000;
	//struct vmctl vmctl;
	int amt;
	int vmmflags = 0; // Disabled probably forever. VMM_VMCALL_PRINTF;
	uint64_t entry = 0x1200000, kerneladdress = 0x1200000;
	int nr_gpcs = 1;
	int fd = open("#cons/vmctl", O_RDWR), ret;
	void * xp;
	int kfd = -1;
	static char cmd[512];
	int i;
	uint8_t csum;
	void *coreboot_tables = (void *) 0x1165000;
	void *a_page;
fprintf(stderr, "%p %p %p %p\n", PGSIZE, PGSHIFT, PML1_SHIFT, PML1_PTE_REACH);

	// mmap is not working for us at present.
	if ((uint64_t)_kernel > GKERNBASE) {
		fprintf(stderr, "kernel array @%p is above , GKERNBASE@%p sucks\n", _kernel, GKERNBASE);
		exit(1);
	}
	memset(_kernel, 0, sizeof(_kernel));
	memset(lowmem, 0xff, 2*1048576);
	memset(low4k, 0xff, 4096);
	// avoid at all costs, requires too much instruction emulation.
	//low4k[0x40e] = 0;
	//low4k[0x40f] = 0xe0;

	//Place mmap(Gan)
	a_page = mmap((void *)0xfee00000, PGSIZE, PROT_READ | PROT_WRITE,
		              MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	fprintf(stderr, "a_page mmap pointer %p", a_page);

	if (a_page == (void *) -1) {
		perror("Could not mmap APIC");
		exit(1);
	}
	if (((uint64_t)a_page & 0xfff) != 0) {
		perror("APIC page mapping is not page aligned");
		exit(1);
	}

	memset(a_page, 0, 4096);
	//((uint32_t *)a_page)[0x30/4] = 0x01060015;
	((uint32_t *)a_page)[0x30/4] = 0xDEADBEEF;


	if (fd < 0) {
		perror("#cons/sysctl");
		exit(1);
	}
	argc--,argv++;
	// switches ...
	// Sorry, I don't much like the gnu opt parsing code.
	while (1) {
		if (*argv[0] != '-')
			break;
		switch(argv[0][1]) {
		case 'd':
			debug++;
			break;
		case 'v':
			vmmflags |= VMM_VMCALL_PRINTF;
			break;
		case 'm':
			argc--,argv++;
			maxresume = strtoull(argv[0], 0, 0);
			break;
		case 'i':
			argc--,argv++;
			virtioirq = strtoull(argv[0], 0, 0);
			break;
		default:
			fprintf(stderr, "BMAFR\n");
			break;
		}
		argc--,argv++;
	}
	if (argc < 1) {
		fprintf(stderr, "Usage: %s vmimage [-n (no vmcall printf)] [coreboot_tables [loadaddress [entrypoint]]]\n", argv[0]);
		exit(1);
	}
	if (argc > 1)
		coreboot_tables = (void *) strtoull(argv[1], 0, 0);
	if (argc > 2)
		kerneladdress = strtoull(argv[2], 0, 0);
	if (argc > 3)
		entry = strtoull(argv[3], 0, 0);
	kfd = open(argv[0], O_RDONLY);
	if (kfd < 0) {
		perror(argv[0]);
		exit(1);
	}
	// read in the kernel.
	xp = (void *)kerneladdress;
	for(;;) {
		amt = read(kfd, xp, 1048576);
		if (amt < 0) {
			perror("read");
			exit(1);
		}
		if (amt == 0) {
			break;
		}
		xp += amt;
	}
	fprintf(stderr, "Read in %d bytes\n", xp-kerneladdress);
	close(kfd);

	// The low 1m so we can fill in bullshit like ACPI. */
	// And, sorry, due to the STUPID format of the RSDP for now we need the low 1M.
	low1m = mmap((int*)4096, MiB-4096, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (low1m != (void *)4096) {
		perror("Unable to mmap low 1m");
		exit(1);
	}
	memset(low1m, 0xff, MiB-4096);
	r = a;
	fprintf(stderr, "install rsdp to %p\n", r);
	*r = rsdp;
	a += sizeof(*r);
	memmove(&r->xsdt_physical_address, &a, sizeof(a));
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
	x->table_offset_entry[2] = (uint64_t) f;
	a += sizeof(*f);
	f->header.length = a - (void *)f;
	gencsum(&f->header.checksum, f, f->header.length);
	if (acpi_tb_checksum((uint8_t *)f, f->header.length) != 0) {
		fprintf(stderr, "ffadt has bad checksum v2\n");
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
	memmove(a, &isor, sizeof(isor));
	a += sizeof(isor);
	m->header.length = a - (void *)m;
	gencsum(&m->header.checksum, m, m->header.length);
	if (acpi_tb_checksum((uint8_t *) m, m->header.length) != 0) {
		fprintf(stderr, "madt has bad checksum v2\n");
		exit(1);
	}
	fprintf(stderr, "allchecksums ok\n");

	gencsum(&x->header.checksum, x, x->header.length);
	if ((csum = acpi_tb_checksum((uint8_t *) x, x->header.length)) != 0) {
		fprintf(stderr, "XSDT has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	hexdump(stdout, r, a-(void *)r);

	a = (void *)(((unsigned long)a + 0xfff) & ~0xfff);
	vmctl.pir = (uint64_t) a;
	memset(a, 0, 4096);
	a += 4096;
	vmctl.vapic = (uint64_t) a;
	//vmctl.vapic = (uint64_t) a_page;	
	memset(a, 0, 4096);
	((uint32_t *)a)[0x30/4] = 0x01060014;
	p64 = a;
	// set up apic values? do we need to?
	// qemu does this.
	//((uint8_t *)a)[4] = 1;
	a += 4096;

	if (ros_syscall(SYS_setup_vmm, nr_gpcs, vmmflags, 0, 0, 0, 0) != nr_gpcs) {
		perror("Guest pcore setup failed");
		exit(1);
	}

	fprintf(stderr, "Run with %d cores and vmmflags 0x%x\n", nr_gpcs, vmmflags);
	mcp = 1;
	if (mcp) {
		my_retvals = malloc(sizeof(void*) * nr_threads);
		if (!my_retvals)
			perror("Init threads/malloc");

		pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		pthread_need_tls(FALSE);
		pthread_mcp_init();					/* gives us one vcore */
		vcore_request(nr_threads - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_threads; i++) {
			xp = __procinfo.vcoremap;
			fprintf(stderr, "%p\n", __procinfo.vcoremap);
			fprintf(stderr, "Vcore %d mapped to pcore %d\n", i,
			    	__procinfo.vcoremap[i].pcoreid);
		}
	}

	ret = syscall(33, 1);
	if (ret < 0) {
		perror("vm setup");
		exit(1);
	}
	ret = posix_memalign((void **)&p512, 4096, 3*4096);
	fprintf(stderr, "memalign is %p\n", p512);
	if (ret) {
		perror("ptp alloc");
		exit(1);
	}
	p1 = &p512[512];
	p2m = &p512[1024];
	uint64_t kernbase = 0; //0xffffffff80000000;
	uint64_t highkernbase = 0xffffffff80000000;
	p512[PML4(kernbase)] = (unsigned long long)p1 | 7;
	p1[PML3(kernbase)] = /*0x87; */(unsigned long long)p2m | 7;
	p512[PML4(highkernbase)] = (unsigned long long)p1 | 7;
	p1[PML3(highkernbase)] = /*0x87; */(unsigned long long)p2m | 7;
#define _2MiB (0x200000)

	for (i = 0; i < 512; i++) {
		p2m[PML2(kernbase + i * _2MiB)] = 0x87 | i * _2MiB;
	}

	kernbase >>= (0+12);
	kernbase <<= (0 + 12);
	uint8_t *kernel = (void *)GKERNBASE;
	//write_coreboot_table(coreboot_tables, ((void *)VIRTIOBASE) /*kernel*/, KERNSIZE + 1048576);
	hexdump(stdout, coreboot_tables, 512);
	fprintf(stderr, "kernbase for pml4 is 0x%llx and entry is %llx\n", kernbase, entry);
	fprintf(stderr, "p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	vmctl.interrupt = 0;
	vmctl.command = REG_RSP_RIP_CR3;
	vmctl.cr3 = (uint64_t) p512;
	vmctl.regs.tf_rip = entry;
	vmctl.regs.tf_rsp = (uint64_t) &stack[1024];
	if (mcp) {
		/* set up virtio bits, which depend on threads being enabled. */
		register_virtio_mmio(&vqdev, virtio_mmio_base);
	}
	fprintf(stderr, "threads started\n");
	fprintf(stderr, "Writing command :%s:\n", cmd);
	
	if(debug) vapic_status_dump(stderr, (void *)vmctl.vapic);

	ret = pwrite(fd, &vmctl, sizeof(vmctl), 0);

	if(debug) vapic_status_dump(stderr, (void *)vmctl.vapic);

	if (ret != sizeof(vmctl)) {
		perror(cmd);
	}
	while (1) {
		void showstatus(FILE *f, struct vmctl *v);
		int c;
		uint8_t byte;
		vmctl.command = REG_RIP;
		if (maxresume-- == 0) {
			debug = 1;
			resumeprompt = 1;
		}
		if (debug) {
			fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
			showstatus(stderr, &vmctl);
		}
		if (resumeprompt) {
			fprintf(stderr, "RESUME?\n");
			c = getchar();
			if (c == 'q')
				break;
		}
		if (vmctl.shutdown == SHUTDOWN_EPT_VIOLATION) {
			uint64_t gpa, *regp, val;
			uint8_t regx;
			int store, size;
			int advance;
			if (decode(&vmctl, &gpa, &regx, &regp, &store, &size, &advance)) {
				fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				showstatus(stderr, &vmctl);
				quit = 1;
				break;
			}
			if (debug) fprintf(stderr, "%p %p %p %p %p %p\n", gpa, regx, regp, store, size, advance);
			if ((gpa & ~0xfffULL) == virtiobase) {
				if (debug) fprintf(stderr, "DO SOME VIRTIO\n");
				// Lucky for us the various virtio ops are well-defined.
				virtio_mmio(&vmctl, gpa, regx, regp, store);
				if (debug) fprintf(stderr, "store is %d:\n", store);
				if (debug) fprintf(stderr, "REGP IS %16x:\n", *regp);
			} else if ((gpa & 0xfee00000) == 0xfee00000) {
				// until we fix our include mess, just put the proto here.
				//int apic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store);
				//apic(&vmctl, gpa, regx, regp, store);
			} else if ((gpa & 0xfec00000) == 0xfec00000) {
				// until we fix our include mess, just put the proto here.
				int do_ioapic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store);
				do_ioapic(&vmctl, gpa, regx, regp, store);
			} else if (gpa < 4096) {
				uint64_t val = 0;
				memmove(&val, &low4k[gpa], size);
				hexdump(stdout, &low4k[gpa], size);
				fprintf(stderr, "Low 1m, code %p read @ %p, size %d, val %p\n", vmctl.regs.tf_rip, gpa, size, val);
				memmove(regp, &low4k[gpa], size);
				hexdump(stdout, regp, size);
			} else {
				fprintf(stderr, "EPT violation: can't handle %p\n", gpa);
				fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				fprintf(stderr, "Returning 0xffffffff\n");
				showstatus(stderr, &vmctl);
				// Just fill the whole register for now.
				*regp = (uint64_t) -1;
			}
			vmctl.regs.tf_rip += advance;
			if (debug) fprintf(stderr, "Advance rip by %d bytes to %p\n", advance, vmctl.regs.tf_rip);
			vmctl.shutdown = 0;
			vmctl.gpa = 0;
			vmctl.command = REG_ALL;
		} else if (vmctl.shutdown == SHUTDOWN_UNHANDLED_EXIT_REASON) {
			switch(vmctl.ret_code){
			case  EXIT_REASON_VMCALL:
				byte = vmctl.regs.tf_rdi;
				printf("%c", byte);
				if (byte == '\n') printf("%c", '%');
				vmctl.regs.tf_rip += 3;
				break;
			case EXIT_REASON_EXTERNAL_INTERRUPT:
				//debug = 1;
				if (debug) fprintf(stderr, "XINT 0x%x 0x%x\n", vmctl.intrinfo1, vmctl.intrinfo2);
				if (debug) pir_dump();
				vmctl.command = RESUME;
				break;
			case EXIT_REASON_IO_INSTRUCTION:
				fprintf(stderr, "IO @ %p\n", vmctl.regs.tf_rip);
				io(&vmctl);
				vmctl.shutdown = 0;
				vmctl.gpa = 0;
				vmctl.command = REG_ALL;
				break;
			case EXIT_REASON_INTERRUPT_WINDOW:
				if (consdata) {
					if (debug) fprintf(stderr, "inject an interrupt\n");
					virtio_mmio_set_vring_irq();
					vmctl.interrupt = 0x80000000 | virtioirq;
					vmctl.command = RESUME;
					consdata = 0;
				}
				break;
			case EXIT_REASON_MSR_WRITE:
			case EXIT_REASON_MSR_READ:
				fprintf(stderr, "Do an msr\n");
				quit = msrio(&vmctl, vmctl.ret_code);
				if (quit) {
					fprintf(stderr, "MSR FAILED: RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
					showstatus(stderr, &vmctl);
				}
				break;
			case EXIT_REASON_MWAIT_INSTRUCTION:
			  fflush(stdout);
				if (debug)fprintf(stderr, "\n================== Guest MWAIT. =======================\n");
				if (debug)fprintf(stderr, "Wait for cons data\n");
				while (!consdata)
					;
				//debug = 1;
				if(debug) vapic_status_dump(stderr, (void *)vmctl.vapic);
				if (debug)fprintf(stderr, "Resume with consdata ...\n");
				vmctl.regs.tf_rip += 3;
				ret = pwrite(fd, &vmctl, sizeof(vmctl), 0);
				if (ret != sizeof(vmctl)) {
					perror(cmd);
				}
				//fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				//showstatus(stderr, &vmctl);
				break;
			case EXIT_REASON_HLT:
				fflush(stdout);
				if (debug)fprintf(stderr, "\n================== Guest halted. =======================\n");
				if (debug)fprintf(stderr, "Wait for cons data\n");
				while (!consdata)
					;
				//debug = 1;
				if (debug)fprintf(stderr, "Resume with consdata ...\n");
				vmctl.regs.tf_rip += 1;
				ret = pwrite(fd, &vmctl, sizeof(vmctl), 0);
				if (ret != sizeof(vmctl)) {
					perror(cmd);
				}
				//fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				//showstatus(stderr, &vmctl);
				break;
			case EXIT_REASON_APIC_ACCESS:				
				if (1 || debug)fprintf(stderr, "APIC READ EXIT\n");
				
				uint64_t gpa, *regp, val;
				uint8_t regx;
				int store, size;
				int advance;
				if (decode(&vmctl, &gpa, &regx, &regp, &store, &size, &advance)) {
					fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
					showstatus(stderr, &vmctl);
					quit = 1;
					break;
				}

				int apic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store);
				apic(&vmctl, gpa, regx, regp, store);
				vmctl.regs.tf_rip += advance;
				if (debug) fprintf(stderr, "Advance rip by %d bytes to %p\n", advance, vmctl.regs.tf_rip);
				vmctl.shutdown = 0;
				vmctl.gpa = 0;
				vmctl.command = REG_ALL;
				break;
			case EXIT_REASON_APIC_WRITE:
				if (1 || debug)fprintf(stderr, "APIC WRITE EXIT\n");
				break;
			default:
				fprintf(stderr, "Don't know how to handle exit %d\n", vmctl.ret_code);
				fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				showstatus(stderr, &vmctl);
				quit = 1;
				break;
			}
		}
		if (debug) fprintf(stderr, "at bottom of switch, quit is %d\n", quit);
		if (quit)
			break;
		if (consdata) {
			if (debug) fprintf(stderr, "inject an interrupt\n");
			if (debug) fprintf(stderr, "XINT 0x%x 0x%x\n", vmctl.intrinfo1, vmctl.intrinfo2);
			vmctl.interrupt = 0x80000000 | virtioirq;
			virtio_mmio_set_vring_irq();
			consdata = 0;
			//debug = 1;
			vmctl.command = RESUME;
		}
		if (debug) fprintf(stderr, "NOW DO A RESUME\n");
		ret = pwrite(fd, &vmctl, sizeof(vmctl), 0);
		if (ret != sizeof(vmctl)) {
			perror(cmd);
		}
	}

	/* later. 
	for (int i = 0; i < nr_threads-1; i++) {
		int ret;
		if (pthread_join(my_threads[i], &my_retvals[i]))
			perror("pth_join failed");
		fprintf(stderr, "%d %d\n", i, ret);
	}
 */

	fflush(stdout);
	exit(0);
}
