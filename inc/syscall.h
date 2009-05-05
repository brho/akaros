#ifndef ROS_INC_SYSCALL_H
#define ROS_INC_SYSCALL_H

#include <inc/types.h>
#include <inc/ring_buffer.h>

/* system call numbers */
enum
{
	SYS_null = 1,
	SYS_cputs,
	SYS_cgetc,
	SYS_getenvid,
	SYS_env_destroy,
};
#define NSYSCALLS (SYS_env_destroy)

#define NUM_SYS_ARGS 6
typedef struct SyscallRequest {
	uint32_t num;
	uint32_t flags;
	uint32_t args[NUM_SYS_ARGS];
} syscall_req_t;

typedef struct SyscallResponse {
	uint32_t retval;
} syscall_resp_t;


// Generic Syscall Ring Buffer
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_resp_t);

#endif /* !ROS_INC_SYSCALL_H */
