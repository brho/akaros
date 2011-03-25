#include <rstdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <parlib.h>
#include <unistd.h>
#include <vcore.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) {}
//#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

#define MAX_NR_TEST_THREADS 100000
int nr_yield_threads = 100;
int nr_yield_loops = 100;

pthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

__thread int my_id;
void *yield_thread(void* arg)
{	
	for (int i = 0; i < nr_yield_loops; i++) {
		printf_safe("[A] pthread %d %p on vcore %d, itr: %d\n", pthread_self()->id,
		       pthread_self(), vcore_id(), i);
		pthread_yield();
		printf_safe("[A] pthread %p returned from yield on vcore %d, itr: %d\n",
		            pthread_self(), vcore_id(), i);
	}
	return (void*)(pthread_self());
}

void *hello_thread(void* arg)
{	
	printf_safe("[A] pthread %p on vcore %d\n", pthread_self(), vcore_id());
	return (void*)(pthread_self());
}

int main(int argc, char** argv) 
{
	if (argc > 1)
		nr_yield_threads = strtol(argv[1], 0, 10);
	if (argc > 2)
		nr_yield_loops = strtol(argv[2], 0, 10);
	nr_yield_threads = MIN(nr_yield_threads, MAX_NR_TEST_THREADS);
	printf("Making %d threads of %d loops each\n", nr_yield_threads, nr_yield_loops);
	/* create and join on yield */
	for (int i = 0; i < nr_yield_threads; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		assert(!pthread_create(&my_threads[i], NULL, &yield_thread, NULL));
	}
	for (int i = 0; i < nr_yield_threads; i++) {
		printf_safe("[A] About to join on thread %d(%p)\n", i, my_threads[i]);
		pthread_join(my_threads[i], &my_retvals[i]);
		printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	printf("Exiting cleanly!\n");
} 
