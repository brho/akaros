#ifndef ROS_ARCH_FRONTEND_H
#define ROS_ARCH_FRONTEND_H

#include <ros/common.h>

int32_t frontend_syscall(int32_t syscall_num, uint32_t arg0, uint32_t arg1, uint32_t arg2);

#define RAMP_SYSCALL_exit		1
#define RAMP_SYSCALL_read		3
#define RAMP_SYSCALL_write		4
#define RAMP_SYSCALL_open		5
#define RAMP_SYSCALL_close		6
#define RAMP_SYSCALL_unlink		10
#define RAMP_SYSCALL_chdir		12
#define RAMP_SYSCALL_brk		17
#define RAMP_SYSCALL_stat		18
#define RAMP_SYSCALL_lseek		19
#define RAMP_SYSCALL_fstat		28
#define RAMP_SYSCALL_lstat		88
#define RAMP_SYSCALL_getch		98
#define RAMP_SYSCALL_gettimeofday	156

#endif /* !ROS_ARCH_FRONTEND_H */
