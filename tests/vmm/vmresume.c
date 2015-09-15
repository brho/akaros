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

int *mmap_blob;
unsigned long long stack[1024];
volatile int shared = 0;
int mcp = 1;
#define V(x, t) (*((volatile t*)(x)))

static void *fail(void*arg)
{

	*mmap_blob = 1337;
	if (mcp)
	while (V(&shared, int) < 31) {
		if (! (V(&shared, int) & 1))
			V(&shared, int) = V(&shared, int) + 1;
//	__asm__ __volatile__("vmcall\n");
//		cpu_relax();
	}
	V(&shared, int) = 55;

	__asm__ __volatile__("vmcall\n");
	__asm__ __volatile__("mov $0xdeadbeef, %rbx; mov 5, %rax\n");	
}

unsigned long long *p512, *p1, *p2m;

int main(int argc, char **argv)
{
	int nr_gpcs = 1;
	int fd = open("#cons/sysctl", O_RDWR), ret;
	void * x;
	static char cmd[512];
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

	ret = syscall(33, 1);
	if (ret < 0) {
		perror("vm setup");
		exit(1);
	}
	ret = posix_memalign((void **)&p512, 4096, 3*4096);
	if (ret) {
		perror("ptp alloc");
		exit(1);
	}
	p1 = &p512[512];
	p2m = &p512[1024];
	p512[0] = (unsigned long long)p1 | 7;
	p1[0] = /*0x87; */(unsigned long long)p2m | 7;
	p2m[0] = 0x87;
	p2m[1] = 0x200000 | 0x87;
	p2m[2] = 0x400000 | 0x87;
	p2m[3] = 0x600000 | 0x87;

	printf("p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	sprintf(cmd, "V 0x%x 0x%x 0x%x", (unsigned long long)fail, (unsigned long long) &stack[1024], (unsigned long long) p512);
	printf("Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}
	sprintf(cmd, "V 0 0 0");
	while (V(&shared, int) < 31) {
		printf("RESUME?\n");
		if (V(&shared, int) & 1) {
			printf("shared %d\n", V(&shared, int) );
		}
		getchar();
		ret = write(fd, cmd, strlen(cmd));
		if (ret != strlen(cmd)) {
			perror(cmd);
		}
		if (V(&shared, int) & 1) {
			printf("shared %d\n", V(&shared, int) );
			V(&shared, int) = V(&shared, int) + 1;
		}
	}
	printf("shared is %d, blob is %d\n", shared, *mmap_blob);

	return 0;
}
