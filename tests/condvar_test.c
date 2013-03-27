#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

/* OS dependent #incs */
#include <parlib.h>
#include <vcore.h>
#include <timing.h>

#define MAX_NR_TEST_THREADS 1000

pthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];


/* Funcs and global vars for test_cv() */
pthread_cond_t local_cv;
pthread_cond_t *cv = &local_cv;
pthread_mutex_t local_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t *pth_m = &local_mutex;

atomic_t counter;
volatile bool state = FALSE;		/* for test 3 */

void *__test_pthread_cond_signal(void *arg)
{
	if (atomic_read(&counter) % 4)
		pthread_cond_signal(cv);
	else
		pthread_cond_broadcast(cv);
	atomic_dec(&counter);
}

void *__test_pthread_cond_waiter(void *arg)
{
	pthread_mutex_lock(pth_m);
	/* check state, etc */
	pthread_cond_wait(cv, pth_m);
	pthread_mutex_unlock(pth_m);
	atomic_dec(&counter);
}

void *__test_pthread_cond_waiter_t3(void *arg)
{
	udelay((long)arg);
	/* if state == false, we haven't seen the signal yet */
	pthread_mutex_lock(pth_m);
	printd("Came in, saw state %d\n", state);
	while (!state) {
		cpu_relax();
		pthread_cond_wait(cv, pth_m);	/* unlocks and relocks */
	}
	pthread_mutex_unlock(pth_m);
	/* Make sure we are done, tell the controller we are done */
	cmb();
	assert(state);
	atomic_dec(&counter);
}

int main(void)
{
	int nr_msgs;
	pthread_lib_init();
	pthread_cond_init(cv, 0);
	pthread_mutex_init(pth_m, 0);

	/* Test 0: signal without waiting */
	pthread_cond_broadcast(cv);
	pthread_cond_signal(cv);
	printf("test_cv: signal without waiting complete\n");

	/* Test 1: single / minimal shit */
	nr_msgs = max_vcores() - 1;
	atomic_init(&counter, nr_msgs);
	for (int i = 0; i < nr_msgs; i++) {
		if (pthread_create(&my_threads[i], NULL, &__test_pthread_cond_waiter,
		    NULL))
			perror("pth_create failed");
	}
	udelay(1000000);
	pthread_cond_signal(cv);
	/* wait for one to make it */
	while (atomic_read(&counter) != nr_msgs - 1)
		pthread_yield();
	printf("test_cv: single signal complete\n");
	pthread_cond_broadcast(cv);
	for (int i = 0; i < nr_msgs; i++)
		pthread_join(my_threads[i], &my_retvals[i]);
	printf("test_cv: broadcast signal complete\n");

	/* Test 2: shitloads of waiters and signalers */
	nr_msgs = MAX_NR_TEST_THREADS;
	atomic_init(&counter, nr_msgs);
	for (int i = 0; i < nr_msgs; i++) {
		if (i % 5) {
			if (pthread_create(&my_threads[i], NULL,
			    &__test_pthread_cond_waiter, NULL))
				perror("pth_create failed");
		} else {
			if (pthread_create(&my_threads[i], NULL,
			    &__test_pthread_cond_signal, NULL))
				perror("pth_create failed");
		}
	}
	pthread_yield();
	while (atomic_read(&counter)) {
		cpu_relax();
		pthread_cond_broadcast(cv);
		pthread_yield();
	}
	for (int i = 0; i < nr_msgs; i++)
		pthread_join(my_threads[i], &my_retvals[i]);
	printf("test_cv: massive message storm complete\n");

	/* Test 3: basic one signaller, one receiver.  we want to vary the amount of
	 * time the sender and receiver delays, starting with (1ms, 0ms) and ending
	 * with (0ms, 1ms).  At each extreme, such as with the sender waiting 1ms,
	 * the receiver/waiter should hit the "check and wait" point well before the
	 * sender/signaller hits the "change state and signal" point.
	 *
	 * Need to make sure we are running in parallel here.  Temp turned off the
	 * 2LSs VC management and got up to 2 VC.  Assuming no preemption. */
	pthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
	while (num_vcores() < 2)
		vcore_request(1);
	for (long i = 0; i < 1000; i++) {
		for (int j = 0; j < 10; j++) {	/* some extra chances at each point */
			state = FALSE;
			/* client waits for i usec */
			if (pthread_create(&my_threads[0], NULL,
			    &__test_pthread_cond_waiter_t3, (void*)i))
				perror("pth_create failed");
			cmb();
			udelay(1000 - i);	/* senders wait time: 1000..0 */
			/* Need to lock the mutex when touching state and signalling about
			 * that state (atomically touch and signal).  Thanks pthreads, for
			 * mandating a cond_signal that doesn't require locking. */
			pthread_mutex_lock(pth_m);
			state = TRUE;
			pthread_cond_signal(cv);
			pthread_mutex_unlock(pth_m);
			/* they might not have run at all yet (in which case they lost the
			 * race and don't need the signal).  but we need to wait til they're
			 * done */
			pthread_join(my_threads[0], my_retvals[0]);
		}
	}
	pthread_can_vcore_request(TRUE);	/* 2LS controls VCs again */
	printf("test_cv: single sender/receiver complete\n");
}
