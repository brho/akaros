#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <ros/syscall.h>

int __socket(int socket_family, int socket_type, int protocol) {
	return ros_syscall(SYS_socket, socket_family, socket_type, protocol, 0,0,0);
}

//libc_hidden_def (__socket)
weak_alias (__socket, socket)
