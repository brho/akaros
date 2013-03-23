#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

ssize_t __recvfrom (int s, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen)
{
	return ros_syscall(SYS_recvfrom, s, buf, len, flags, from, fromlen);
}

weak_alias (__recvfrom, recvfrom)
