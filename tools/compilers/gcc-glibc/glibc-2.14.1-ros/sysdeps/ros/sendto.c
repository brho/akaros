#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

int
__sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
	return ros_syscall(SYS_sendto, s, buf, len, flags, to, tolen);
}

weak_alias (__sendto, sendto)
