#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>
#undef __connect
int __connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	return ros_syscall(SYS_connect, sockfd, addr, addrlen, 0, 0, 0);
}

INTDEF(__connect)
weak_alias (__connect, connect)
