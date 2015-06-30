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

/* this test will run the "kernel" in the negative address space. We hope. */
int *mmap_blob;
unsigned long long stack[1024];
volatile int shared = 0;
int mcp = 1;
#define V(x, t) (*((volatile t*)(x)))

uint8_t _kernel[64*1048576];

unsigned long long *p512, *p1, *p2m;

void *talk_thread(void *arg)
{
	printf("talk thread ..\n");
	for(; V(&shared, int) < 32; ){
		if (V(&shared, int) & 1) {
			printf("shared %d\n", V(&shared, int) );
			V(&shared, int) = V(&shared, int) + 1;
		}
		cpu_relax();
	}
	printf("All done, read %d\n", *mmap_blob);
	return NULL;
}

pthread_t *my_threads;
void **my_retvals;
int nr_threads = 2;

int main(int argc, char **argv)
{
	int amt;
	int vmmflags = VMM_VMCALL_PRINTF;
	uint64_t entry = 0x1000000, kerneladdress = 0x1000000;
	int nr_gpcs = 1;
	int fd = open("#cons/sysctl", O_RDWR), ret;
	void * x;
	int kfd = -1;
	static char cmd[512];
	void *coreboot_tables = (void *) 0x1165000;
	/* kernel has to be in the range 16M to 64M for now. */
	// mmap is not working for us at present.
	if ((uint64_t)_kernel > 16*1048576) {
		printf("kernel array is above 16M, sucks\n");
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
		case 'n':
			vmmflags &= ~VMM_VMCALL_PRINTF;
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

	mcp = 0; //argc - 1;
	if (mcp) {
		my_threads = malloc(sizeof(pthread_t) * nr_threads);
		my_retvals = malloc(sizeof(void*) * nr_threads);
		if (!(my_retvals && my_threads))
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

	if (mcp) {
		if (pthread_create(&my_threads[0], NULL, &talk_thread, NULL))
			perror("pth_create failed");
//		if (pthread_create(&my_threads[1], NULL, &fail, NULL))
//			perror("pth_create failed");
	}
	printf("threads started\n");

	if (0) for (int i = 0; i < nr_threads-1; i++) {
		int ret;
		if (pthread_join(my_threads[i], &my_retvals[i]))
			perror("pth_join failed");
		printf("%d %d\n", i, ret);
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
	uint8_t *kernel = (void *)(16*1048576);
	write_coreboot_table(coreboot_tables, kernel, 16*1048576);
	hexdump(stdout, coreboot_tables, 128);
	printf("kernbase for pml4 is 0x%llx and entry is %llx\n", kernbase, entry);
	printf("p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	sprintf(cmd, "V 0x%llx 0x%llx 0x%llx", entry, (unsigned long long) &stack[1024], (unsigned long long) p512);
	printf("Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}
	sprintf(cmd, "V 0 0 0");
	while (1) {
		int c;
		printf("RESUME?\n");
		c = getchar();
		if (c == 'q')
			break;
		ret = write(fd, cmd, strlen(cmd));
		if (ret != strlen(cmd)) {
			perror(cmd);
		}
	}
	printf("shared is %d, blob is %d\n", shared, *mmap_blob);

	return 0;
}
