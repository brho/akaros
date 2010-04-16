#include <rstdio.h>
#include <pthread.h>

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) \
	pthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	pthread_mutex_unlock(&lock);

int thread_num;
pthread_t t0;
pthread_t t1;
pthread_t t2;

__thread int my_id;
void *thread(void* arg)
{	
	my_id = thread_num++;
	printf_safe("thread %d\n", my_id);
}

int main(int argc, char** argv) 
{
	thread_num = 0;
	printf_safe("About to create thread 0\n");
	pthread_create(&t0, NULL, &thread, NULL);
	printf("returned from creating him, spinning\n");
	while(1);

//	printf_safe("About to create thread 1\n");
//	pthread_create(&t1, NULL, &thread, NULL);
//	printf_safe("About to create thread 2\n");
//	pthread_create(&t2, NULL, &thread, NULL);

	printf_safe("About to join on thread 0\n");
	pthread_join(t0, NULL);
//	printf_safe("About to join on thread 1\n");
//	pthread_join(t1, NULL);
//	printf_safe("About to join on thread 2\n");
//	pthread_join(t2, NULL);
} 
