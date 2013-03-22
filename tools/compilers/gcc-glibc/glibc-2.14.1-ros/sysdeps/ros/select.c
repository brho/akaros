#include <sysdep.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

int __select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
	return ros_syscall(SYS_select, nfds, readfds, writefds, exceptfds, timeout, 0);
}

libc_hidden_def (__select)
weak_alias (__select, select)
