#ifndef ROS_FRONTEND_H
#define ROS_FRONTEND_H

#include <ros/common.h>

#ifdef ROS_KERNEL

#include <env.h>
#include <process.h>

// Default APPSERVER_ETH_TYPE if not defined externally
#ifndef APPSERVER_ETH_TYPE
#define APPSERVER_ETH_TYPE 0x8888
#endif

int32_t frontend_syscall_from_user(env_t* p, int32_t syscall_num, 
                                   uint32_t arg0, uint32_t arg1, 
                                   uint32_t arg2, uint32_t translate_args);

int32_t frontend_syscall(pid_t pid, int32_t syscall_num, 
                         uint32_t arg0, uint32_t arg1, uint32_t arg2, 
                         uint32_t arg3, int32_t* errno);

int user_frontend_syscall(struct proc* p, int n, int a0, 
                          int a1, int a2, int a3);

int32_t frontend_nbputch(char ch);
int32_t frontend_nbgetch();

void* user_memdup(struct proc* p, const void* va, int len);
void* user_memdup_errno(struct proc* p, const void* va, int len);
void user_memdup_free(struct proc* p, void* va);
char* user_strdup(struct proc* p, const char* va0, int max);
char* user_strdup_errno(struct proc* p, const char* va, int max);
int memcpy_to_user_errno(struct proc* p, void* dst, const void* src, int len);
void* kmalloc_errno(int len);

error_t read_page(struct proc* p, int fd, physaddr_t pa, int pgoff);
error_t open_file(struct proc* p, const char* path, int oflag, int mode);
error_t close_file(struct proc* p, int fd);

#endif

#define APPSERVER_MAXPATH            1024

#define APPSERVER_SYSCALL_exit          1
#define APPSERVER_SYSCALL_read          3
#define APPSERVER_SYSCALL_write         4
#define APPSERVER_SYSCALL_open          5
#define APPSERVER_SYSCALL_close         6
#define APPSERVER_SYSCALL_link          9
#define APPSERVER_SYSCALL_unlink       10
#define APPSERVER_SYSCALL_chdir        12
#define APPSERVER_SYSCALL_chmod        15
#define APPSERVER_SYSCALL_brk          17
#define APPSERVER_SYSCALL_stat         18
#define APPSERVER_SYSCALL_lseek        19
#define APPSERVER_SYSCALL_fstat        28
#define APPSERVER_SYSCALL_utime        30
#define APPSERVER_SYSCALL_access       33
#define APPSERVER_SYSCALL_dup          41
#define APPSERVER_SYSCALL_umask        60
#define APPSERVER_SYSCALL_fcntl        62
#define APPSERVER_SYSCALL_lstat        88
#define APPSERVER_SYSCALL_tcgetattr    89
#define APPSERVER_SYSCALL_tcsetattr    90
#define APPSERVER_SYSCALL_closedir     91
#define APPSERVER_SYSCALL_rewinddir    92
#define APPSERVER_SYSCALL_readdir      93
#define APPSERVER_SYSCALL_opendir      94
#define APPSERVER_SYSCALL_dup2         95
#define APPSERVER_SYSCALL_proc_free    96
#define APPSERVER_SYSCALL_proc_init    97
#define APPSERVER_SYSCALL_time         98
#define APPSERVER_SYSCALL_pread       173
#define APPSERVER_SYSCALL_pwrite      174
#define APPSERVER_SYSCALL_getcwd      229

#endif /* !ROS_FRONTEND_H */
