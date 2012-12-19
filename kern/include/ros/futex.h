/* This isn't really part of the kernel interface, and is just placed here so
 * gcc can find it during its compilation. */
#ifndef ROS_KERNEL

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

#endif /* ifndef ROS_KERNEL */
