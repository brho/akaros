#ifndef ROS_INC_SYSCALL_H
#define ROS_INC_SYSCALL_H

#include <inc/types.h>

/* system call numbers */
enum
{
	SYS_cputs = 0,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
	NSYSCALLS
};

#define NUM_SYS_ARGS 6
typedef struct Syscall {
	uint32_t num;
	uint32_t flags;
	uint32_t args[NUM_SYS_ARGS];
} syscall_t;

#endif /* !ROS_INC_SYSCALL_H */
