/* Copyright (c) 2014 The Regents of the University of California
 * Kevin KLues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * xmm_test: test the reading/writing of the xmm registers */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <parlib/pvcalarm.h>

static void nothing() {}

static void read_xmm(int id)
{
	char array[16*16] __attribute__((aligned(128))) = {0};
    asm volatile (
		"movdqa %%xmm0,  %0;"
		"movdqa %%xmm1,  %1;"
		"movdqa %%xmm2,  %2;"
		"movdqa %%xmm3,  %3;"
		"movdqa %%xmm4,  %4;"
		"movdqa %%xmm5,  %5;"
		"movdqa %%xmm6,  %6;"
		"movdqa %%xmm7,  %7;"
		"movdqa %%xmm8,  %8;"
		"movdqa %%xmm9,  %9;"
		"movdqa %%xmm10, %10;"
		"movdqa %%xmm11, %11;"
		"movdqa %%xmm12, %12;"
		"movdqa %%xmm13, %13;"
		"movdqa %%xmm14, %14;"
		"movdqa %%xmm15, %15;"
		:"=m"(array[0*16]), 
		 "=m"(array[1*16]), 
		 "=m"(array[2*16]), 
		 "=m"(array[3*16]), 
		 "=m"(array[4*16]), 
		 "=m"(array[5*16]), 
		 "=m"(array[6*16]), 
		 "=m"(array[7*16]), 
		 "=m"(array[8*16]), 
		 "=m"(array[9*16]), 
		 "=m"(array[10*16]), 
		 "=m"(array[11*16]), 
		 "=m"(array[12*16]), 
		 "=m"(array[13*16]), 
		 "=m"(array[14*16]), 
		 "=m"(array[15*16])
		:
        :
    );
	for (int i=0; i<16; i++) {
		int *addr = (int*)(array + i*16);
		if (*addr != id) {
			printf("ERROR: xmm%d, id: %d, *addr: %d\n", i, id, *addr);
			abort();
		}
	}
}

static void write_xmm(int __id)
{
	char id[16] __attribute__((aligned(128)));
	*((int*)id) = __id;
	asm volatile (
		"movdqa %0, %%xmm0;"
		"movdqa %0, %%xmm1;"
		"movdqa %0, %%xmm2;"
		"movdqa %0, %%xmm3;"
		"movdqa %0, %%xmm4;"
		"movdqa %0, %%xmm5;"
		"movdqa %0, %%xmm6;"
		"movdqa %0, %%xmm7;"
		"movdqa %0, %%xmm8;"
		"movdqa %0, %%xmm9;"
		"movdqa %0, %%xmm10;"
		"movdqa %0, %%xmm11;"
		"movdqa %0, %%xmm12;"
		"movdqa %0, %%xmm13;"
		"movdqa %0, %%xmm14;"
		"movdqa %0, %%xmm15;"
		:
		:"m"(id[0])
	    :"%xmm0","%xmm1","%xmm2","%xmm3",
		 "%xmm4","%xmm5","%xmm6","%xmm7",
		 "%xmm8","%xmm9","%xmm10","%xmm11",
		 "%xmm12","%xmm13","%xmm14","%xmm15"
	);
}

void *worker_thread(void* arg)
{	
	write_xmm((int)(long)arg);
	while (1)
		read_xmm((int)(long)arg);
	return 0;
}

int main(int argc, char** argv) 
{
	#define NUM_THREADS	1000
	pthread_t children[NUM_THREADS];

	enable_pvcalarms(PVCALARM_REAL, 10000, nothing);
	for (int i=0; i<NUM_THREADS; i++)
		pthread_create(&children[i], NULL, &worker_thread, (void*)(long)i+2);
	worker_thread((void*)(long)1);
	return 0;
} 

