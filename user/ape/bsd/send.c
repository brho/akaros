/* posix */
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include<arpa/inet.h>

#include "priv.h"

ssize_t send (int fd, __const void *a, size_t n, int flags)
{
	if(flags & MSG_OOB){
		errno = EOPNOTSUPP;
		return -1;
	}
	return write(fd, a, n);
}

ssize_t
recv(int fd, void *a, size_t n, int flags)
{
	if(flags & MSG_OOB){
		errno = EOPNOTSUPP;
		return -1;
	}
	return read(fd, a, n);
}
