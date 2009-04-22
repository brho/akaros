#ifndef ROS_INC_SYSCALL_H
#define ROS_INC_SYSCALL_H

/* system call numbers */
enum
{
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
	NSYSCALLS
};

#endif /* !ROS_INC_SYSCALL_H */
