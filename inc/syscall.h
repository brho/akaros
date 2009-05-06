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
// syscall number starts at 1 and goes up to NSYSCALLS, without holes.
#define INVALID_SYSCALL(syscallno) ((syscallno) > NSYSCALLS)

#define NUM_SYS_ARGS 6
typedef struct SyscallRequest {
	uint32_t num;
	uint32_t flags;
	uint32_t args[NUM_SYS_ARGS];
} syscall_req_t;

typedef struct SyscallResponse {
	int32_t retval;
} syscall_rsp_t;

// Generic Syscall Ring Buffer
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

typedef struct SyscallRespDesc {
	syscall_front_ring_t* sysfr;
	uint32_t idx;
} syscall_desc_t;

#endif /* !ROS_INC_SYSCALL_H */
