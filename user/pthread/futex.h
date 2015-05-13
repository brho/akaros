#ifndef PTHREAD_FUTEX_H
#define PTHREAD_FUTEX_H

#include <sys/time.h>

__BEGIN_DECLS

enum {
	FUTEX_WAIT,
	FUTEX_WAKE
};

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, int val3);

__END_DECLS

#endif	/* PTHREAD_FUTEX_H */
