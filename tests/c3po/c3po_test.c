#include <stdio.h>
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

pthread_t t1;
pthread_t t2;
pthread_t t3;

#define NUM_TEST_THREADS 100

pthread_t my_threads[NUM_TEST_THREADS];
void *my_retvals[NUM_TEST_THREADS];

__thread int my_id;
void *yield_thread(void* arg)
{	
	for (int i = 0; i < 100; i++) {
		printf_safe("[A] pthread %p on vcore %d, itr: %d\n", pthread_self(), vcore_id(), i);
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

//int main(int argc, char** argv) 
//{
//	void *retval1 = 0;
//	printf_safe("[A] About to create thread 1\n");
//	pthread_create(&t1, NULL, &yield_thread, NULL);
//	printf_safe("[A] About to join on thread 1\n");
//	pthread_join(t1, &retval1);
//}

int main(int argc, char** argv) 
{
	void *retval1 = 0;
	void *retval2 = 0;
	void *retval3 = 0;

	/* yield test */
	printf_safe("[A] About to create thread 1\n");
	pthread_create(&t1, NULL, &yield_thread, NULL);
	printf_safe("[A] About to create thread 2\n");
	pthread_create(&t2, NULL, &yield_thread, NULL);
	printf_safe("[A] About to create thread 3\n");
	pthread_create(&t3, NULL, &yield_thread, NULL);
	/* join on them */
	printf_safe("[A] About to join on thread 1\n");
	pthread_join(t1, &retval1);
	printf_safe("[A] Successfully joined on thread 1 (retval: %p)\n", retval1);
	printf_safe("[A] About to join on thread 2\n");
	pthread_join(t2, &retval2);
	printf_safe("[A] Successfully joined on thread 2 (retval: %p)\n", retval2);
	printf_safe("[A] About to join on thread 3\n");
	pthread_join(t3, &retval3);
	printf_safe("[A] Successfully joined on thread 3 (retval: %p)\n", retval3);

	/* create and join on hellos */
	for (int i = 0; i < NUM_TEST_THREADS; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		pthread_create(&my_threads[i], NULL, &yield_thread, NULL);
	}
	for (int i = 0; i < NUM_TEST_THREADS; i++) {
		printf_safe("[A] About to join on thread %d\n", i);
		pthread_join(my_threads[i], &my_retvals[i]);
		printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	printf("Exiting cleanly!\n");
} 
