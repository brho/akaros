#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

/* OS dependent #incs */
#include <parlib.h>
#include <dtls.h>

#define NR_TEST_THREADS 10
#define NUM_DTLS_KEYS 10
pthread_t my_threads[NR_TEST_THREADS];
void *my_retvals[NR_TEST_THREADS];
dtls_key_t dtls_keys[NUM_DTLS_KEYS];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *thread(void* arg)
{	
  long *dtls_value[NUM_DTLS_KEYS];
  for(int i=0; i<NUM_DTLS_KEYS; i++) {
    dtls_value[i] = malloc(sizeof(long));
    *dtls_value[i] = (long)pthread_self() + i;
    set_dtls(dtls_keys[i], dtls_value[i]);
  }

  pthread_mutex_lock(&mutex);
  long self = (long)pthread_self();
  printf("In pthread %p (%ld)\n", (void*)self, self);
  for(int i=0; i<NUM_DTLS_KEYS; i++) {
    long *value = get_dtls(dtls_keys[i]);
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
  for(int i=0; i<NUM_DTLS_KEYS; i++) {
    dtls_keys[i] = dtls_key_create(dtls_dtor);
  }
  for (int i = 0; i < NR_TEST_THREADS; i++) {
  	assert(!pthread_create(&my_threads[i], NULL, &thread, NULL));
  }
  for (int i = 0; i < NR_TEST_THREADS; i++) {
  	pthread_join(my_threads[i], &my_retvals[i]);
  }
  for(int i=0; i<NUM_DTLS_KEYS; i++) {
    dtls_key_delete(dtls_keys[i]);
  }
  printf("Test complete!\n");
} 
