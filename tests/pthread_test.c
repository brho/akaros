#include <stdio.h>
#include <pthread.h>

int thread_num;
pthread_t t0;
pthread_t t1;

__thread int my_id;
void *thread(void* arg)
{	
	my_id = thread_num++;
	printf("thread %d\n", my_id);
}

int main(int argc, char** argv) 
{
	thread_num = 0;
	pthread_create(&t0, NULL, &thread, NULL);
//	pthread_create(&t1, NULL, &thread, NULL);

	pthread_join(t0, NULL);
//	pthread_join(t1, NULL);
} 
