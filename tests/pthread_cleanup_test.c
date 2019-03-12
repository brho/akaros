#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define handle_error_en(en, msg) \
		  do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static volatile int done = 0;
static volatile int cleanup_pop_arg = 0;
static volatile int cnt = 0;

static void cleanup_handler(void *arg)
{
	printf("Running Cleanup Handler\n");
}

static void *thread_start(void *arg)
{
	time_t start, curr;

	printf("Pushing Cleanup Handler\n");
	pthread_cleanup_push(cleanup_handler, NULL);
	while (!done)
		cnt++;
	printf("Popping Cleanup Handler: %d\n", cleanup_pop_arg);
	pthread_cleanup_pop(cleanup_pop_arg);
	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t thr;
	int s;
	void *res;

	if (argc == 2) {
		cleanup_pop_arg = atoi(argv[1]);
	} else {
		printf("You must supply either 0 or 1 as an argument to "
		       "run the pop handler or not.\n");
		exit(EXIT_FAILURE);
	}

	/* Start a new thread. */
	s = pthread_create(&thr, NULL, thread_start, NULL);
	if (s != 0)
		handle_error_en(s, "pthread_create");

	/* Allow new thread to run a while, then signal done. */
	uthread_sleep(2);
	done = 1;

	s = pthread_join(thr, &res);
	if (s != 0)
		handle_error_en(s, "pthread_join");

	printf("Thread terminated normally; cnt = %d\n", cnt);
	exit(EXIT_SUCCESS);
}
