#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <hart.h>
#include <stdio.h>

void* func(void* arg)
{
	int id = (int)arg;

	printf("Hello from pthread %d!\n",id);

	return arg;
}

int main()
{
	int i;
	pthread_t* t = (pthread_t*)malloc(sizeof(pthread_t)*(hart_max_harts()-1));

	for(i = 0; i < hart_max_harts()-1; i++)
		pthread_create(&t[i],NULL,&func,(void*)(i+1));

	for(i = 0; i < hart_max_harts()-1; i++)
	{
		void* x = 0;
		pthread_join(t[i],&x);
		assert((long)x == i+1);
	}

	printf("Pthreads joined!  Bye...\n");
	return 0;
}
