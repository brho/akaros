#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sys/types.h>
#include <sys/socket.h>

int accept(int fd, __SOCKADDR_ARG addr, socklen_t * __restrict alen)
{
	return accept4(fd, addr, alen, 0);
}
libc_hidden_def(accept)
