#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <parlib.h>
#include <unistd.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
//#define printf_safe(...) {}
#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

#define NUM_TEST_THREADS 32
pthread_t my_threads[NUM_TEST_THREADS];
void *my_retvals[NUM_TEST_THREADS];
pthread_barrier_t barrier;

void *thread(void* arg)
{	
	for(int i=0; i<NUM_TEST_THREADS; i++) {
		//printf_safe("[A] pthread %d on vcore %d\n", pthread_self()->id, vcore_id());
		pthread_barrier_wait(&barrier);
	}
	return (void*)(pthread_self()->id);
}

int main(int argc, char** argv) 
{
	pthread_barrier_init(&barrier, NULL, NUM_TEST_THREADS);
	#define NUM_ITERATIONS 5000
	for(int j=0; j<NUM_ITERATIONS; j++) {
//	while (1) {
		for (int i = 1; i <= NUM_TEST_THREADS; i++) {
			pthread_create(&my_threads[i-1], NULL, &thread, NULL);
		}
		for (int i = 1; i <= NUM_TEST_THREADS; i++) {
			pthread_join(my_threads[i-1], &my_retvals[i-1]);
		}
		printf("Iteration %d of %d\n", j, NUM_ITERATIONS);
	}
	pthread_barrier_destroy(&barrier);
	sys_proc_destroy(getpid(), 0);
} 
