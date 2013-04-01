#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <parlib.h>
#include <unistd.h>
#include <sys/time.h>

pthread_barrier_t barrier;

#define MAX_NR_TEST_THREADS 100000
int nr_threads = 100;
int nr_loops = 10000;
int nr_vcores = 0;

pthread_t *my_threads;
void **my_retvals;
bool run_barriertest = FALSE;

void *thread(void *arg)
{	
	while (!run_barriertest)
		cpu_relax();
	for(int i = 0; i < nr_loops; i++) {
		pthread_barrier_wait(&barrier);
	}
	return (void*)(long)pthread_self()->id;
}

int main(int argc, char** argv) 
{
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	long usec_diff;
	if (argc > 1)
		nr_threads = strtol(argv[1], 0, 10);
	if (argc > 2)
		nr_loops = strtol(argv[2], 0, 10);
	if (argc > 3)
		nr_vcores = strtol(argv[3], 0, 10);
	printf("Running %d threads for %d iterations on %d vcores\n",
	       nr_threads, nr_loops, nr_vcores);
	nr_threads = MIN(nr_threads, MAX_NR_TEST_THREADS);
	my_threads = malloc(sizeof(pthread_t) * nr_threads);
	my_retvals = malloc(sizeof(void*) * nr_threads);
	if (!(my_retvals && my_threads))
		perror("Init threads/malloc");
	if (nr_vcores) {
		/* Only do the vcore trickery if requested */
		pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		pthread_lib_init();					/* gives us one vcore */
		vcore_request(nr_vcores - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_vcores; i++) {
			printd("Vcore %d mapped to pcore %d\n", i,
				   __procinfo.vcoremap[i].pcoreid);
		}
	}
	pthread_barrier_init(&barrier, NULL, nr_threads);
	for (int i = 0; i < nr_threads; i++) {
		pthread_create(&my_threads[i], NULL, &thread, NULL);
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	run_barriertest = TRUE;
	for (int i = 0; i < nr_threads; i++) {
		pthread_join(my_threads[i], &my_retvals[i]);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	pthread_barrier_destroy(&barrier);
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("Done: %d threads, %d loops, %d vcores\n",
	       nr_threads, nr_loops, nr_vcores);
	printf("Time to run: %d usec, %f usec per barrier\n", usec_diff,
	       (float)usec_diff / nr_loops);
	pthread_barrier_destroy(&barrier);
} 
