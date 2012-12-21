#ifndef _FUTEX_H
#define _FUTEX_H

#include <sys/time.h>

enum {
	FUTEX_WAIT,
	FUTEX_WAKE
};

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, int val3);

#endif	/* _FUTEX_H */
