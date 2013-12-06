#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

/* OS dependent #incs */
#include <parlib.h>

#define NR_TEST_THREADS 10
#define NUM_PTHREAD_KEYS 10
pthread_t my_threads[NR_TEST_THREADS];
void *my_retvals[NR_TEST_THREADS];
pthread_key_t pthread_keys[NUM_PTHREAD_KEYS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *thread(void* arg)
{	
  long *dtls_value[NUM_PTHREAD_KEYS];
  for(int i=0; i<NUM_PTHREAD_KEYS; i++) {
    dtls_value[i] = malloc(sizeof(long));
    *dtls_value[i] = (long)pthread_self() + i;
    pthread_setspecific(pthread_keys[i], dtls_value[i]);
  }

  pthread_mutex_lock(&mutex);
  long self = (long)pthread_self();
  printf("In pthread %p (%ld)\n", (void*)self, self);
  for(int i=0; i<NUM_PTHREAD_KEYS; i++) {
    long *value = pthread_getspecific(pthread_keys[i]);
    printf("  dtls_value[%d] = %ld\n", i, *value);
  }
  pthread_mutex_unlock(&mutex);

  return (void*)(self);
}

static void dtls_dtor(void *dtls)
{
  pthread_mutex_lock(&mutex);
  printf("Phread %p freeing dtls %p.\n", pthread_self(), dtls);
  free(dtls);
  pthread_mutex_unlock(&mutex);
}

int main(int argc, char** argv) 
{
  printf("Starting dtls test.\n");
  for(int i=0; i<NUM_PTHREAD_KEYS; i++) {
    pthread_key_create(&pthread_keys[i], dtls_dtor);
  }
  for (int i = 0; i < NR_TEST_THREADS; i++) {
  	if (pthread_create(&my_threads[i], NULL, &thread, NULL))
		perror("pth_create failed");
  }
  for (int i = 0; i < NR_TEST_THREADS; i++) {
  	pthread_join(my_threads[i], &my_retvals[i]);
  }
  for(int i=0; i<NUM_PTHREAD_KEYS; i++) {
    pthread_key_delete(pthread_keys[i]);
  }
  printf("Test complete!\n");
} 
