#include <futex.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "misc-compat.h"

#define NUM_THREADS 10
pthread_t thandlers[NUM_THREADS];

void *handler(void *arg) {
	int id = pthread_id();
	int var = 0;
    struct timespec timeout = {
		.tv_sec = id,
		.tv_nsec = 0
	};
	printf("Begin thread: %d\n", id);
    futex(&var, FUTEX_WAIT, 0, &timeout, NULL, 0);
	printf("End thread: %d\n", id);
}

int main(int argc, char **argv)
{
	for (int i=0; i<NUM_THREADS; i++) {
		pthread_create(&thandlers[i], NULL, &handler, NULL);
	}
	for (int i=0; i<NUM_THREADS; i++) {
		pthread_join(thandlers[i], NULL);
	}
	return 0;
}
