#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	return ros_syscall(SYS_accept, sockfd, addr, addrlen, 0, 0, 0);
}

libc_hidden_def (accept)
