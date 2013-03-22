#include <sysdep.h>
#include <stdint.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <ros/syscall.h>

ssize_t
__send(int s, const void *buf, size_t len, int flags) {
	return ros_syscall(SYS_send, s, buf, len, flags, 0, 0);
}

libc_hidden_def (__send)
weak_alias (__send, send)
