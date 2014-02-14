/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * mmap_test: dumping ground for various tests, such as PFs on files. */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <parlib.h>
#include <timing.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
	
void *addr = 0;
size_t nr_pgs = 1;
#define STRIDE 256

static void mmap_test(void)
{
	assert(addr);
	for (int *i = addr; (void*)i < addr + nr_pgs * PGSIZE; i += STRIDE) {
		*i += 1;
	}
}

void *worker_thread(void* arg)
{	
	while (1) {
		mmap_test();
		uthread_sleep(1);
	}
	return 0;
}

int main(int argc, char** argv) 
{
	pthread_t child;
	void *child_ret;
	int fd;
	struct stat statbuf;

	if (argc < 2) {
		printf("Usage: %s FILENAME [NR_PGS]\n", argv[0]);
		exit(-1);
	}
	/* if you're going to create, you'll need to seek too */
	//fd = open(argv[1], O_RDWR | O_CREAT, 0666);
	fd = open(argv[1], O_RDWR, 0666);
	if (fd < 0) {
		perror("Unable to open file");
		exit(-1);
	}
	if (argc < 3)
		nr_pgs = 1;
	else
		nr_pgs = atoi(argv[2]);
	if (fstat(fd, &statbuf)) {
		perror("Stat failed");
		exit(-1);
	}
	nr_pgs = MIN(nr_pgs, (ROUNDUP(statbuf.st_size, PGSIZE) >> PGSHIFT));
	addr = mmap(0, nr_pgs * PGSIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap failed");
		exit(-1);
	}
	printf("Running as an SCP\n");
	mmap_test();
	printf("Spawning worker thread, etc...\n");
	pthread_create(&child, NULL, &worker_thread, NULL);
	pthread_join(child, &child_ret);
} 
