/* Copyright (C) 2003 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Jakub Jelinek <jakub@redhat.com>, 2003.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <errno.h>
#include <error.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

volatile int count;

void sighand(int sig)
{
	assert(SIGUSR1 == sig);
	++count;
}

void sigact(int sig, siginfo_t *info, void *null)
{
	assert(SIGUSR1 == sig);
	assert(sig == info->si_signo);
	++count;
}

int main(void)
{
	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	int nr_tests = 0;

	sa.sa_handler = sighand;
	sa.sa_flags = 0;
	nr_tests++;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		printf("first sigaction failed: %m\n");
		exit(1);
	}
	if (raise(SIGUSR1) < 0) {
		printf("first raise failed: %m\n");
		exit(1);
	}

	sa.sa_sigaction = sigact;
	sa.sa_flags = SA_SIGINFO;
	nr_tests++;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		printf("second sigaction failed: %m\n");
		exit(1);
	}
	if (raise(SIGUSR1) < 0) {
		printf("second raise failed: %m\n");
		exit(1);
	}
	if (count != nr_tests) {
		printf("signal handler not called %d times\n", nr_tests);
		exit(1);
	}
	printf("Passed, exiting\n");
	exit(0);
}
