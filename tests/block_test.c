#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <parlib.h>
#include <unistd.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) {}
//#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

pthread_t t1;
pthread_t t2;
pthread_t t3;

#define NUM_TEST_THREADS 1000

pthread_t my_threads[NUM_TEST_THREADS];
void *my_retvals[NUM_TEST_THREADS];

__thread int my_id;
void *block_thread(void* arg)
{	
	assert(!in_vcore_context());
	for (int i = 0; i < 1000; i++) {
		printf_safe("[A] pthread %d on vcore %d\n", pthread_self()->id, vcore_id());
		ros_syscall(SYS_block, 0, 0, 0, 0, 0, 0);
	}
	return (void*)(pthread_self()->id);
}

void *hello_thread(void* arg)
{	
	printf_safe("[A] pthread %d on vcore %d\n", pthread_self()->id, vcore_id());
	return (void*)(pthread_self()->id);
}

int main(int argc, char** argv) 
{
	void *retval1 = 0;
	void *retval2 = 0;
	void *retval3 = 0;

	/* yield test */
	printf_safe("[A] About to create thread 1\n");
	pthread_create(&t1, NULL, &block_thread, NULL);
	#if 0
	/* sys_block can't handle more than one at a time yet */
	printf_safe("[A] About to create thread 2\n");
	pthread_create(&t2, NULL, &block_thread, NULL);
	printf_safe("[A] About to create thread 3\n");
	pthread_create(&t3, NULL, &block_thread, NULL);
	#endif
	/* join on them */
	printf_safe("[A] About to join on thread 1\n");
	pthread_join(t1, &retval1);
	printf_safe("[A] Successfully joined on thread 1 (retval: %p)\n", retval1);
	#if 0
	printf_safe("[A] About to join on thread 2\n");
	pthread_join(t2, &retval2);
	printf_safe("[A] Successfully joined on thread 2 (retval: %p)\n", retval2);
	printf_safe("[A] About to join on thread 3\n");
	pthread_join(t3, NULL);
	printf_safe("[A] Successfully joined on thread 3 (retval: %p)\n", retval3);
	#endif

	#if 0
	/* create and join on hellos */
	while (1) {
		for (int i = 1; i < NUM_TEST_THREADS; i++) {
			printf_safe("[A] About to create thread %d\n", i);
			pthread_create(&my_threads[i], NULL, &hello_thread, NULL);
		}
		for (int i = 1; i < NUM_TEST_THREADS; i++) {
			printf_safe("[A] About to join on thread %d\n", i);
			pthread_join(my_threads[i], &my_retvals[i]);
			printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
			            my_retvals[i]);
		}
		break;
	}
	#endif
} 
