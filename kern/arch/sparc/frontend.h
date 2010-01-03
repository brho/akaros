#ifndef ROS_ARCH_FRONTEND_H
#define ROS_ARCH_FRONTEND_H

#include <ros/common.h>

#ifdef ROS_KERNEL

#include <env.h>
int32_t frontend_syscall_from_user(env_t* p, int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t translate_args);
int32_t frontend_syscall(pid_t pid, int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2, int32_t* errno);

int32_t sys_nbgetch();
int32_t sys_nbputch(char ch);

#endif

#define RAMP_MAXPATH			1024

#define RAMP_SYSCALL_exit		1
#define RAMP_SYSCALL_read		3
#define RAMP_SYSCALL_write		4
#define RAMP_SYSCALL_open		5
#define RAMP_SYSCALL_close		6
#define RAMP_SYSCALL_link		9
#define RAMP_SYSCALL_unlink		10
#define RAMP_SYSCALL_chdir		12
#define RAMP_SYSCALL_chmod		15
#define RAMP_SYSCALL_brk		17
#define RAMP_SYSCALL_stat		18
#define RAMP_SYSCALL_lseek		19
#define RAMP_SYSCALL_fstat		28
#define RAMP_SYSCALL_utime		30
#define RAMP_SYSCALL_access		33
#define RAMP_SYSCALL_dup		41
#define RAMP_SYSCALL_umask		60
#define RAMP_SYSCALL_fcntl		62
#define RAMP_SYSCALL_lstat		88
#define RAMP_SYSCALL_closedir		91
#define RAMP_SYSCALL_rewinddir		92
#define RAMP_SYSCALL_readdir		93
#define RAMP_SYSCALL_opendir		94
#define RAMP_SYSCALL_dup2		95
#define RAMP_SYSCALL_proc_free		96
#define RAMP_SYSCALL_proc_init		97
#define RAMP_SYSCALL_gettimeofday	156
#define RAMP_SYSCALL_getcwd		229

#endif /* !ROS_ARCH_FRONTEND_H */
