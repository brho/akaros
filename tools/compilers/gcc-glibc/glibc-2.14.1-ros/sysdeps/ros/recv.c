#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>
int __recv(int s, void *buf, size_t len, int flags) {
	return ros_syscall(SYS_recv, s, buf, len, flags, 0, 0);
}

//libc_hidden_def (__recv)
weak_alias (__recv, recv)
