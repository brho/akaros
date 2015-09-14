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
#include <acpi/acpi.h>
#include <ros/arch/mmu.h>
#include <ros/vmx.h>
#include <parlib/uthread.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

int msrio(struct vmctl *vcpu, uint32_t opcode);
#if 0
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
#endif

/* this test will run the "kernel" in the negative address space. We hope. */
void *low1m;
uint8_t low4k[4096];
unsigned long long stack[1024];
volatile int shared = 0;
volatile int quit = 0;
int mcp = 1;

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
uint64_t virtio_mmio_base = 0x100000000;

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
		printf("----------------------- TT a %p\n", a);
		printf("talk thread ttargs %x v %x\n", a, v);
	}
	
	for(num = 0;;num++) {
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(v, iov, &outlen, &inlen);
		if (debug)
			printf("CCC: vq desc head %d, gaveit %d gotitback %d\n", head, gaveit, gotitback);
		for(i = 0; debug && i < outlen + inlen; i++)
			printf("CCC: v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);
		/* host: if we got an output buffer, just output it. */
		for(i = 0; i < outlen; i++) {
			num++;
			int j;
			for (j = 0; j < iov[i].length; j++)
				printf("%c", ((char *)iov[i].v)[j]);
		}
		
		if (debug)
			printf("CCC: outlen is %d; inlen is %d\n", outlen, inlen);
		/* host: fill in the writeable buffers. */
		/* why we're getting these I don't know. */
		for (i = outlen; i < outlen + inlen; i++) {
			if (debug) fprintf(stderr, "CCC: send back empty writeable");
			iov[i].length = 0;
		}
		if (debug) printf("CCC: call add_used\n");
		/* host: now ack that we used them all. */
		add_used(v, head, outlen+inlen);
		if (debug) printf("CCC: DONE call add_used\n");
	}
	fprintf(stderr, "All done\n");
	return NULL;
}

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
	
	if (debug) printf("Spin on console being read, print num queues, halt\n");

	for(num = 0;! quit;num++) {
		int debug = 1;
		/* host: use any buffers we should have been sent. */
		head = wait_for_vq_desc(v, iov, &outlen, &inlen);
		if (debug)
			printf("vq desc head %d, gaveit %d gotitback %d\n", head, gaveit, gotitback);
		for(i = 0; debug && i < outlen + inlen; i++)
			printf("v[%d/%d] v %p len %d\n", i, outlen + inlen, iov[i].v, iov[i].length);
		if (debug)
			printf("outlen is %d; inlen is %d\n", outlen, inlen);
		/* host: fill in the writeable buffers. */
		for (i = outlen; i < outlen + inlen; i++) {
			/* host: read a line. */
			memset(consline, 0, 128);
			if (fgets(consline, 4096-256, stdin) == NULL) {
				exit(0);
			} 
			if (debug) printf("GOT A LINE:%s:\n", consline);
			if (strlen(consline) < 3 && consline[0] == 'q' ) {
				quit = 1;
				break;
			}

			memmove(iov[i].v, consline, strlen(consline)+ 1);
			iov[i].length = strlen(consline) + 1;
		}
		if (debug) printf("call add_used\n");
		/* host: now ack that we used them all. */
		add_used(v, head, outlen+inlen);
		if (debug) printf("DONE call add_used\n");
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
		{name: "consin", maxqnum: 64, f: &consin, arg: (void *)0},
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
	printf("tbchecksum %p for %d", buffer, length);
	while (buffer < end) {
		if (end - buffer < 2)
			printf("%02x\n", sum);
		sum = (uint8_t)(sum + *(buffer++));
	}
	printf(" is %02x\n", sum);
	return (sum);
}

static void gencsum(uint8_t *target, void *data, int len)
{
	uint8_t csum;
	// blast target to zero so it does not get counted (it might be in the struct we checksum) 
	// And, yes, it is, goodness.
	printf("gencsum %p target %p source %d bytes\n", target, data, len);
	*target = 0;
	csum  = acpi_tb_checksum((uint8_t *)data, len);
	*target = ~csum + 1;
	printf("Cmoputed is %02x\n", *target);
}

int main(int argc, char **argv)
{
	void *a = (void *)0xe0000;
#if 0
	struct acpi_table_rsdp *r;
	struct acpi_table_fadt *f;
	struct acpi_table_madt *m;
	struct acpi_table_xsdt *x;
#endif
	uint64_t virtiobase = 0x100000000ULL;
	// lowmem is a bump allocated pointer to 2M at the "physbase" of memory 
	void *lowmem = (void *) 0x1000000;
	struct vmctl vmctl;
	int amt;
	int vmmflags = 0; // Disabled probably forever. VMM_VMCALL_PRINTF;
	uint64_t entry = 0x1200000, kerneladdress = 0x1200000;
	int nr_gpcs = 1;
	int fd = open("#c/vmctl", O_RDWR), ret;
	void * xp;
	int kfd = -1;
	static char cmd[512];
	int i;
	uint8_t csum;
	void *coreboot_tables = (void *) 0x1165000;
printf("%p %p %p %p\n", PGSIZE, PGSHIFT, PML1_SHIFT, PML1_PTE_REACH);

	// mmap is not working for us at present.
	if ((uint64_t)_kernel > GKERNBASE) {
		printf("kernel array @%p is above , GKERNBASE@%p sucks\n", _kernel, GKERNBASE);
		exit(1);
	}
	memset(_kernel, 0, sizeof(_kernel));
	memset(lowmem, 0xff, 2*1048576);
	memset(low4k, 0xff, 4096);
	// avoid at all costs, requires too much instruction emulation.
	//low4k[0x40e] = 0;
	//low4k[0x40f] = 0xe0;

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
		default:
			printf("BMAFR\n");
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
	low1m = mmap((int*)4096, MiB-4096, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (low1m != (void *)4096) {
		perror("Unable to mmap low 1m");
		exit(1);
	}
	memset(low1m, 0xff, MiB-4096);
#if 0
	r = a;
	fprintf(stderr, "install rsdp to %p\n", r);
	*r = rsdp;
	a += sizeof(*r);
	memmove(&r->xsdt_physical_address, &a, sizeof(a));
	gencsum(&r->checksum, r, ACPI_RSDP_CHECKSUM_LENGTH);
	if ((csum = acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_CHECKSUM_LENGTH)) != 0) {
		printf("RSDP has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	/* Check extended checksum if table version >= 2 */
	gencsum(&r->extended_checksum, r, ACPI_RSDP_XCHECKSUM_LENGTH);
	if ((rsdp.revision >= 2) &&
	    (acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_XCHECKSUM_LENGTH) != 0)) {
		printf("RSDP has bad checksum v2\n");
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
		printf("ffadt has bad checksum v2\n");
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
	m->header.length = a - (void *)m;
	gencsum(&m->header.checksum, m, m->header.length);
	if (acpi_tb_checksum((uint8_t *) m, m->header.length) != 0) {
		printf("madt has bad checksum v2\n");
		exit(1);
	}
	fprintf(stderr, "allchecksums ok\n");

	gencsum(&x->header.checksum, x, x->header.length);
	if ((csum = acpi_tb_checksum((uint8_t *) x, x->header.length)) != 0) {
		printf("XSDT has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	hexdump(stdout, r, a-(void *)r);
#endif

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
			printf("%p\n", __procinfo.vcoremap);
			printf("Vcore %d mapped to pcore %d\n", i,
			    	__procinfo.vcoremap[i].pcoreid);
		}
	}

	ret = syscall(33, 1);
	if (ret < 0) {
		perror("vm setup");
		exit(1);
	}
	ret = posix_memalign((void **)&p512, 4096, 3*4096);
	printf("memalign is %p\n", p512);
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
	printf("kernbase for pml4 is 0x%llx and entry is %llx\n", kernbase, entry);
	printf("p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	vmctl.command = REG_RSP_RIP_CR3;
	vmctl.cr3 = (uint64_t) p512;
	vmctl.regs.tf_rip = entry;
	vmctl.regs.tf_rsp = (uint64_t) &stack[1024];
	if (mcp) {
		/* set up virtio bits, which depend on threads being enabled. */
		register_virtio_mmio(&vqdev, virtio_mmio_base);
	}
	printf("threads started\n");
	printf("Writing command :%s:\n", cmd);

	ret = write(fd, &vmctl, sizeof(vmctl));
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
			printf("RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
			showstatus(stdout, &vmctl);
		}
		if (resumeprompt) {
			printf("RESUME?\n");
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
				printf("RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				showstatus(stdout, &vmctl);
				quit = 1;
				break;
			}
			if (debug) printf("%p %p %p %p %p %p\n", gpa, regx, regp, store, size, advance);
			if ((gpa & ~0xfffULL) == virtiobase) {
				if (debug) printf("DO SOME VIRTIO\n");
				// Lucky for us the various virtio ops are well-defined.
				virtio_mmio(&vmctl, gpa, regx, regp, store);
#if 0
			} else if ((gpa & 0xfee00000) == 0xfee00000) {
				// until we fix our include mess, just put the proto here.
				int apic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store);
				apic(&vmctl, gpa, regx, regp, store);
			} else if ((gpa & 0xfec00000) == 0xfec00000) {
				// until we fix our include mess, just put the proto here.
				int ioapic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store);
				ioapic(&vmctl, gpa, regx, regp, store);
#endif
			} else if (gpa < 4096) {
				uint64_t val = 0;
				memmove(&val, &low4k[gpa], size);
				hexdump(stdout, &low4k[gpa], size);
				printf("Low 1m, code %p read @ %p, size %d, val %p\n", vmctl.regs.tf_rip, gpa, size, val);
				memmove(regp, &low4k[gpa], size);
				hexdump(stdout, regp, size);
			} else {
				printf("EPT violation: can't handle %p\n", gpa);
				printf("RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				printf("Returning 0xffffffff\n");
				showstatus(stdout, &vmctl);
				// Just fill the whole register for now.
				*regp = (uint64_t) -1;
			}
			vmctl.regs.tf_rip += advance;
			if (debug) printf("Advance rip by %d bytes to %p\n", advance, vmctl.regs.tf_rip);
			vmctl.shutdown = 0;
			vmctl.gpa = 0;
			vmctl.command = REG_ALL;
		} else if (vmctl.shutdown == SHUTDOWN_UNHANDLED_EXIT_REASON) {
			switch(vmctl.ret_code){
			case  EXIT_REASON_VMCALL:
				byte = vmctl.regs.tf_rdi;
				printf("%c", byte);
				if (byte == '\n') printf("%c", 'V');
				vmctl.regs.tf_rip += 3;
				break;
			case EXIT_REASON_EXTERNAL_INTERRUPT:
				//debug = 1;
				fprintf(stderr, "XINT 0x%x 0x%x\n", vmctl.intrinfo1, vmctl.intrinfo2);
				vmctl.interrupt = 0x80000302; // b0d;
				// That sent an NMI and we got it.

				vmctl.interrupt = 0x80000320; // b0d;
				// This fails on entry
				
				vmctl.interrupt = 0x80000306; // b0d;
				// This succeedd in sending a UD.

				vmctl.interrupt = 0x8000030f; // b0d;
				
				vmctl.command = RESUME;
				break;
			case EXIT_REASON_IO_INSTRUCTION:
				printf("IO @ %p\n", vmctl.regs.tf_rip);
				io(&vmctl);
				vmctl.shutdown = 0;
				vmctl.gpa = 0;
				vmctl.command = REG_ALL;
				break;
			case EXIT_REASON_INTERRUPT_WINDOW:
				vmctl.interrupt = 0x80000220;
				vmctl.command = RESUME;
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
			case EXIT_REASON_HLT:
				printf("\n================== Guest halted. RIP. =======================\n");
				quit = 1;
				break;
			default:
				fprintf(stderr, "Don't know how to handle exit %d\n", vmctl.ret_code);
				fprintf(stderr, "RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
				showstatus(stderr, &vmctl);
				quit = 1;
				break;
			}
		}
		if (debug) printf("at bottom of switch, quit is %d\n", quit);
		if (quit)
			break;
		if (debug) printf("NOW DO A RESUME\n");
		ret = write(fd, &vmctl, sizeof(vmctl));
		if (ret != sizeof(vmctl)) {
			perror(cmd);
		}
	}

	/* later. 
	for (int i = 0; i < nr_threads-1; i++) {
		int ret;
		if (pthread_join(my_threads[i], &my_retvals[i]))
			perror("pth_join failed");
		printf("%d %d\n", i, ret);
	}
 */

	return 0;
}
