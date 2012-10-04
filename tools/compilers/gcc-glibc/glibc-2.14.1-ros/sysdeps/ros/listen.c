#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

int __listen(int sockfd, int backlog) {
	return ros_syscall(SYS_listen, sockfd, backlog, 0, 0, 0, 0);
}
weak_alias (__listen, listen)
