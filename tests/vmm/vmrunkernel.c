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
#include <ros/vmm.h>
#include <ros/arch/mmu.h>
#include <ros/vmx.h>
#include <parlib/uthread.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

/* this test will run the "kernel" in the negative address space. We hope. */
int *mmap_blob;
unsigned long long stack[1024];
volatile int shared = 0;
volatile int quit = 0;
int mcp = 1;

#define MiB 0x100000u
#define GiB (1u<<30)
#define GKERNBASE (16*MiB)
#define KERNSIZE (128*MiB+GKERNBASE)
uint8_t _kernel[KERNSIZE];

unsigned long long *p512, *p1, *p2m;

void **my_retvals;
int nr_threads = 3;
int debug = 0;
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

	for(num = 0;;num++) {
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

int main(int argc, char **argv)
{
	uint64_t virtiobase = 0x100000000ULL;
	struct vmctl vmctl;
	int amt;
	int vmmflags = 0; // Disabled probably forever. VMM_VMCALL_PRINTF;
	uint64_t entry = 0x1000000, kerneladdress = 0x1000000;
	int nr_gpcs = 1;
	int fd = open("#c/vmctl", O_RDWR), ret;
	void * x;
	int kfd = -1;
	static char cmd[512];
	void *coreboot_tables = (void *) 0x1165000;

	// mmap is not working for us at present.
	if ((uint64_t)_kernel > GKERNBASE) {
		printf("kernel array @%p is above , GKERNBASE@%p sucks\n", _kernel, GKERNBASE);
		exit(1);
	}
	memset(_kernel, 0, sizeof(_kernel));

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
		case 'v':
			vmmflags |= VMM_VMCALL_PRINTF;
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
	x = (void *)kerneladdress;
	for(;;) {
		amt = read(kfd, x, 1048576);
		if (amt < 0) {
			perror("read");
			exit(1);
		}
		if (amt == 0) {
			break;
		}
		x += amt;
	}
	fprintf(stderr, "Read in %d bytes\n", x-kerneladdress);

	fprintf(stderr, "Run with %d cores and vmmflags 0x%x\n", nr_gpcs, vmmflags);
	if (ros_syscall(SYS_setup_vmm, nr_gpcs, vmmflags, 0, 0, 0, 0) != nr_gpcs) {
		perror("Guest pcore setup failed");
		exit(1);
	}
	/* blob that is faulted in from the EPT first.  we need this to be in low
	 * memory (not above the normal mmap_break), so the EPT can look it up.
	 * Note that we won't get 4096.  The min is 1MB now, and ld is there. */
	mmap_blob = mmap((int*)4096, PGSIZE, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (mmap_blob == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}

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
			x = __procinfo.vcoremap;
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
	int i;
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
		if (debug) printf("RESUME?\n");
		//c = getchar();
		//if (c == 'q')
			//break;
		if (debug) printf("RIP %p, shutdown 0x%x\n", vmctl.regs.tf_rip, vmctl.shutdown);
		//showstatus(stdout, &vmctl);
		// this will be in a function, someday.
		// A rough check: is the GPA 
		if ((vmctl.shutdown == SHUTDOWN_EPT_VIOLATION) && ((vmctl.gpa & ~0xfffULL) == virtiobase)) {
			if (debug) printf("DO SOME VIRTIO\n");
			virtio_mmio(&vmctl);
			vmctl.shutdown = 0;
			vmctl.gpa = 0;
			vmctl.command = REG_ALL;
		}
		if (vmctl.shutdown == SHUTDOWN_UNHANDLED_EXIT_REASON) {
			switch(vmctl.ret_code){
			case  EXIT_REASON_VMCALL:
				byte = vmctl.regs.tf_rdi;
				printf("%c", byte);
				if (byte == '\n') printf("%c", 'V');
				vmctl.regs.tf_rip += 3;
				break;
			default:
				fprintf(stderr, "Don't know how to handle exit %d\n", vmctl.ret_code);
				quit = 1;
				break;
			}
		}
		if (quit)
			break;
		if (debug) printf("NOW DO A RESUME\n");
		ret = write(fd, &vmctl, sizeof(vmctl));
		if (ret != sizeof(vmctl)) {
			perror(cmd);
		}
	}

	printf("shared is %d, blob is %d\n", shared, *mmap_blob);

	quit = 1;
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
