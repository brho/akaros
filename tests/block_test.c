#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <parlib.h>
#include <unistd.h>
#include <sys/time.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) {}
//#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

#define NUM_TEST_THREADS 500
#define NUM_TEST_LOOPS 1000

pthread_t my_threads[NUM_TEST_THREADS];
void *my_retvals[NUM_TEST_THREADS];

__thread int my_id;
void *block_thread(void* arg)
{	
	assert(!in_vcore_context());
	for (int i = 0; i < NUM_TEST_LOOPS; i++) {
		printf_safe("[A] pthread %d on vcore %d\n", pthread_self()->id, vcore_id());
		sys_block(5000 + pthread_self()->id);
	}
	return (void*)(long)pthread_self()->id;
}

int main(int argc, char** argv) 
{
	struct timeval tv = {0};
	if (gettimeofday(&tv, 0))
		perror("Time error...");
	printf("Start time: %dsec %dusec\n", tv.tv_sec, tv.tv_usec);
	for (int i = 0; i < NUM_TEST_THREADS; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		pthread_create(&my_threads[i], NULL, &block_thread, NULL);
	}
	for (int i = 0; i < NUM_TEST_THREADS; i++) {
		printf_safe("[A] About to join on thread %d\n", i);
		pthread_join(my_threads[i], &my_retvals[i]);
		printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	if (gettimeofday(&tv, 0))
		perror("Time error...");
	printf("End time  : %dsec %dusec\n", tv.tv_sec, tv.tv_usec);
	printf("All done, exiting cleanishly\n");
} 
