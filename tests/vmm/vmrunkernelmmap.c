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

static void *mmap_blob;
unsigned long long stack[1024];
volatile int tr, rr, done;
volatile int state;
int debug;

unsigned long long *p512, *p1, *p2m;

void *talk_thread(void *arg)
{
	printf("talk thread ..\n");
	int c;
	
	// This is a a bit odd but getchar() is not echoing characters.
	// That's good for us but makes no sense.
	while (!done && (c = getchar())) {
		int i;
		if (debug) printf("Set rr to 0x%x\n", c | 0x80);
		rr = c | 0x80;
		if (debug) printf("rr 0x%x tr 0x%x\n", rr, tr);
		while (! tr)
			;
		if (debug) printf("tr 0x%x\n", tr);
		putchar(tr & 0x7f);
		tr = 0;
	}
	rr = 0;	
	return NULL;
}

pthread_t *my_threads;
void **my_retvals;
int nr_threads = 2;

int main(int argc, char **argv)
{
	int i, amt;
	int nr_gpcs = 1;
	uint64_t entry;
	int fd = open("#c/sysctl", O_RDWR), ret;
	int kfd = -1;
	bool smallkernel = false;
	void * x;
	static char cmd[512];
	if (fd < 0) {
		perror("#c/sysctl");
		exit(1);
	}
	argc--,argv++;
	if (argc != 2) {
		fprintf(stderr, "Usage: %s vmimage entrypoint\n", argv[0]);
		exit(1);
	}
	entry = strtoull(argv[1], 0, 0);
	kfd = open(argv[0], O_RDONLY);
	if (kfd < 0) {
		perror(argv[0]);
		exit(1);
	}
	if (ros_syscall(SYS_setup_vmm, nr_gpcs, 0, 0, 0, 0, 0) != nr_gpcs) {
		perror("Guest pcore setup failed");
		exit(1);
	}
		my_threads = malloc(sizeof(pthread_t) * nr_threads);
		my_retvals = malloc(sizeof(void*) * nr_threads);
		if (!(my_retvals && my_threads))
			perror("Init threads/malloc");

		pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		pthread_need_tls(FALSE);
		pthread_lib_init();					/* gives us one vcore */
		vcore_request(nr_threads - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_threads; i++) {
			x = __procinfo.vcoremap;
			printf("%p\n", __procinfo.vcoremap);
			printf("Vcore %d mapped to pcore %d\n", i,
			    	__procinfo.vcoremap[i].pcoreid);
		}
		if (pthread_create(&my_threads[0], NULL, &talk_thread, NULL))
			perror("pth_create failed");
//		if (pthread_create(&my_threads[1], NULL, &fail, NULL))
//			perror("pth_create failed");
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
	/* blob that is faulted in from the EPT first.  we need this to be in low
	 * memory (not above the normal mmap_break), so the EPT can look it up.
	 * Note that we won't get 4096.  The min is 1MB now, and ld is there. */

	mmap_blob = mmap((int*)(15*1048576), 16 * 1048576, PROT_EXEC | PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS, -1, 0);
	if (mmap_blob == MAP_FAILED) {
		perror("Unable to mmap");
		exit(1);
	}

	memset(mmap_blob, 0, 16*1048576);
	// read in the kernel.
	x = mmap_blob + 0x1000000;
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
	fprintf(stderr, "Read in %d bytes\n", x-mmap_blob);

	p512 = mmap_blob;
	p1 = &p512[512];
	p2m = &p512[1024];
	
	// We had thought to enter the kernel at the high address. But
	// there's just too much state the kernel has to clean up to
	// make this really work -- consider all the segment
	// descriptors that have to move, etc.  So we will enter the
	// kernel in the low part of the address space, and let it
	// work up its page tables and the other good fun.  Map the
	// kernel address space at low virtual, for 1G.  It's ok to
	// map memory we have no access to.
#define _2MiB 0x200000
	p512[0] = (unsigned long long)p1 | 7;
	// if only we could guarantee 1G pages everywhere!
	p1[0] = /*0x87; */(unsigned long long)p2m | 7;
	for(i = 0; i < 16; i++) {
		p2m[i] = 0x87 | i * _2MiB;
		printf("pwm[%d] = 0x%llx\n", i, p2m[i]);
	}
	printf("p512 %p p512[0] is 0x%lx p1 %p p1[0] is 0x%x\n", p512, p512[0], p1, p1[0]);
	sprintf(cmd, "V 0x%llx 0x%llx 0x%llx", entry, (unsigned long long) &stack[1024], (unsigned long long) p512);
	printf("Writing command :%s:\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret != strlen(cmd)) {
		perror(cmd);
	}

	sprintf(cmd, "V 0 0 0");
	while (! done) {
		char c[1];
		printf("hit return\n"); read(0, c, 1);
		if (debug)
			fprintf(stderr, "RESUME\n");
		ret = write(fd, cmd, strlen(cmd));
		if (ret != strlen(cmd)) {
			perror(cmd);
		}
	}
	return 0;
}
