/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <parlib.h>
#include <pthread.h>
#include <futex.h>
#include <signal.h>

// Signal handler run in vcore context which redirects signals to userspace by
// waking a thread waiting on a futex. Note, this does not guarantee that every
// signal that comes through this handler will be noticed or processed by the
// thread.
int __sigpending = 0;
void sig_handler(int signr) {
	__sigpending = 1;
	futex(&__sigpending, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
}

// User level thread waiting on a futex to count signals
int count = 0;
void *count_signals(void *arg) {
	while(1) {
		futex(&__sigpending, FUTEX_WAIT, 0, NULL, NULL, 0);
		__sync_fetch_and_add(&count, 1);
		__sigpending = 0;
	}
}

// Thread spamming us with signals
void *sig_thread(void *arg) {
	int *done = (int*)arg;
	struct sigaction sigact = {.sa_handler = sig_handler, 0};
	sigaction(SIGUSR1, &sigact, 0);
	while(1) {
		kill(getpid(), SIGUSR1);
		cmb();
		if (*done) return NULL;
	}
}

int main(int argc, char **argv)
{
	int done = false;
	pthread_t pth_handle, pth_handle2;
	// Spawn off a thread to spam us with signals
	pthread_create(&pth_handle, NULL, &sig_thread, &done);
	// Spawn off a thread to process those signals
	pthread_create(&pth_handle2, NULL, &count_signals, NULL);
	// Sleep for 3 seconds by timing out on a futex
	int dummy = 0;
	struct timespec timeout = {.tv_sec = 3, 0};
	futex(&dummy, FUTEX_WAIT, 0, &timeout, NULL, 0);
	// Force the signal tread to exit
	cmb();
	done = true;
	pthread_join(pth_handle, NULL);
	printf("count: %d\n", count);
	return 0;
}
