#include <stdio.h> 
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>

/* this test will run the "kernel" in the negative address space. We hope. */
int *mmap_blob;
unsigned long long stack[1024];
volatile int shared = 0;
int mcp = 1;
#define V(x, t) (*((volatile t*)(x)))

uint8_t _kernel[64*1048576];

void *fail(void*arg)
{

	__asm__ __volatile__(
			"MOVl $0xc0000082, %ecx\nrdmsr\naddl $1, %eax\nwrmsr\n"
			"MOVl $0xc0000100, %ecx\nrdmsr\naddl $1, %eax\nwrmsr\n"
			"MOVl $0xc0000101, %ecx\nrdmsr\naddl $1, %eax\nwrmsr\n"
			"MOVl $0xc0000102, %ecx\nrdmsr\naddl $1, %eax\nwrmsr\n"
			"mov $0x30, %rdi\nvmcall\nhlt\n");
	*mmap_blob = 1337;
	if (mcp)
	while (V(&shared, int) < 31) {
		if (! (V(&shared, int) & 1))
			V(&shared, int) = V(&shared, int) + 1;
//		cpu_relax();
	}
	V(&shared, int) = 55;

	__asm__ __volatile__("vmcall\n");
	__asm__ __volatile__("mov $0xdeadbeef, %rbx; mov 5, %rax\n");	
}

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
	int nr_gpcs = 1;
	int fd = open("#cons/sysctl", O_RDWR), ret;
	void * x;
	static char cmd[512];
	/* kernel has to be in the range 16M to 64M for now. */
	// mmap is not working for us at present.
	if ((uint64_t)_kernel > 16*1048576) {
		printf("kernel array is above 16M, sucks\n");
		exit(1);
	}

	if (fd < 0) {
		perror("#cons/sysctl");
		exit(1);
	}
	if (ros_syscall(SYS_setup_vmm, nr_gpcs, 0, 0, 0, 0, 0) != nr_gpcs) {
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

	mcp = 1; //argc - 1;
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
	p512[PML4(kernbase)] = (unsigned long long)p1 | 7;
	p1[PML3(kernbase)] = /*0x87; */(unsigned long long)p2m | 7;
#define _2MiB (0x200000)
	int i;
	for (i = 0; i < 512; i++) {
		p2m[PML2(kernbase + i * _2MiB)] = 0x87 | i * _2MiB;
	}

	kernbase >>= (0+12);
	kernbase <<= (0 + 12);
	uint64_t entry = kernbase + (uint64_t) fail;
	uint8_t *kernel = (void *)(16*1048576);
	uint8_t program[] = {0x0f, 0x1, 0xc1, 0xeb, 0xfe};
	printf("memmove(%p, %p, %d\n", kernel, fail, 4096);
	memmove(kernel, fail, 4096);
	entry = (uint64_t)kernel;
	printf("kernbase for pml4 is 0x%llx and entry is %llx\n", kernbase, entry);
	printf("p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	sprintf(cmd, "V 0x%llx 0x%llx 0x%llx", entry, (unsigned long long) &stack[1024], (unsigned long long) p512);
	printf("Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}
	printf("shared is %d, blob is %d\n", shared, *mmap_blob);
	printf("Hit return to end, ...\n");
	read(0, p512, 1);

	return 0;
}
