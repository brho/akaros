#ifdef __CONFIG_ARSC_SERVER__
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <vcore.h>
#include <parlib.h>
#include <ros/syscall.h>
#include <arc.h>
#include <stdio.h>

#define NUM_THREADS 4

pthread_t t1;

async_desc_t desc1;

syscall_desc_t* sys_cputs_async(const char *s, size_t len,                                             
                     void (*cleanup_handler)(void*), void* cleanup_data)
{                                                                                                                     
	return arc_call(SYS_cputs, s, len);
}

void *syscall_thread(void* arg)
{
	char testme ='a';
	char buf[20] = {0};
	sprintf(buf, "%d", (pthread_self()->id % 10) );
	char tid = buf[0];
	syscall_desc_t* sysdesc;
	sysdesc = sys_cputs_async(&tid, 1, NULL, NULL);
	assert (-1 != waiton_syscall(sysdesc));
}

int main(int argc, char** argv){
	int pid = sys_getpid();
	pthread_t *my_threads = malloc(sizeof(pthread_t) * NUM_THREADS);
	char testme = 't';
	printf ("multi thread - init arsc \n");
	init_arc(&SYS_CHANNEL);
	for (int i = 0; i < NUM_THREADS ; i++)
		pthread_create(&my_threads[i], NULL, &syscall_thread, NULL);
	
	for (int i = 0; i < NUM_THREADS; i++){
		pthread_join(my_threads[i], NULL);
	}

	printf("multi thread - end\n");
}
#else
int main(){};
#endif
