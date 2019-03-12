/* Copyright Google, Inc. 2017
 * Author: Zach Zimmerman
 * mmap_vmm_test: tests mmap with fd's with access from
 * vmthreads */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <parlib/parlib.h>
#include <parlib/timing.h>
#include <parlib/ros_debug.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vmm/vmm.h>
#include <vmm/vthread.h>

static struct virtual_machine vm = {.mtx = UTH_MUTEX_INIT};

int safe_to_exit;
void *addr;
size_t nr_pgs = 1;
#define STRIDE 1
#define NUM_ITERS 100

static void mmap_testz(void)
{
	assert(addr);
	for (char *i = addr; (void*)i < addr + nr_pgs * PGSIZE; i += STRIDE)
		*i = 'z';
}

static void mmap_testy(void)
{
	assert(addr);
	for (char *i = addr; (void*)i < addr + nr_pgs * PGSIZE; i += STRIDE)
		*i = 'y';
}

void *worker_thread(void *arg)
{
	int i;

	for (i = 0; i < NUM_ITERS; ++i)
		mmap_testy();

	safe_to_exit = true;
	__asm__ __volatile__("hlt\n\t");
	return 0;
}

int main(void)
{
	int fd, pid, ret;
	char inputfile[50];

	pid = getpid();
	sprintf(inputfile, "/tmp/mmap-test-%d", pid);

	fd = open(inputfile, O_RDWR | O_CREAT, 0666);
	if (fd < 0) {
		perror("Unable to open file");
		exit(-1);
	}

	ret = unlink(inputfile);
	if (ret == -1) {
		perror("UNLINK error");
		exit(-1);
	}

	//Increase the file size with ftruncate
	ret = ftruncate(fd, nr_pgs * PGSIZE);
	if (ret == -1) {
		perror("FTRUNCATE error");
		exit(-1);
	}

	nr_pgs = 1;
	void *loc = (void*) 0x1000000;

	addr = mmap(loc, nr_pgs * PGSIZE, PROT_WRITE | PROT_READ | PROT_EXEC,
		    MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap failed");
		exit(-1);
	}

	printf("MMap got addr %p\n", addr);
	printf("Spawning worker vmthread thread, etc...\n");
	vthread_create(&vm, (void*)&worker_thread, NULL);

	while (!safe_to_exit)
		cpu_relax();

	for (char *i = addr; (void*)i < addr + nr_pgs * PGSIZE; i += STRIDE)
		assert(*i == 'y');

	printf("mmap_file_vmm: test finished, doing teardown\n");

	ret = munmap(addr, nr_pgs * PGSIZE);
	if (ret == -1) {
		perror("mmap_file_vmm: problem unmapping memory after test\n");
		exit(-1);
	}

	ret = close(fd);
	if (ret == -1) {
		perror("mmap_file_vmm: problem closing file after test\n");
		exit(-1);
	}

	printf("mmap_file_vmm: PASSED\n");
	return 0;
}
