#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

int __bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	return ros_syscall(SYS_bind, sockfd, addr, addrlen, 0, 0, 0);
}

weak_alias (__bind, bind)
