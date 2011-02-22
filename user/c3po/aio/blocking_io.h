

#ifndef BLOCKING_IO_H
#define BLOCKING_IO_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/syscall.h>
//#ifdef HAVE_SYS_SOCKETCALL_H
//#include <sys/socketcall.h>
//#endif


// replacements for standard system calls
extern int open(const char *pathname, int flags, ...);
extern int creat(const char *pathname, mode_t mode);
extern int close(int fd);

extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

extern off_t lseek(int fd, off_t off, int whence);
extern int fcntl(int fd, int cmd, ...);
extern int connect(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen);
extern int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int dup(int oldfd);
extern int dup2(int oldfd, int newfd);


// FIXME: should add disk r/w calls that specify the offset



#endif // BLOCKING_IO_H
