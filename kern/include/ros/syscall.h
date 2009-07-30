#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

#include <arch/types.h>
#include <ros/ring_buffer.h>

/* system call numbers */
enum
{
	SYS_begofcalls, //Should always be first

	SYS_null,
	SYS_cache_buster,
	SYS_cache_invalidate,
	SYS_cputs,
	SYS_cgetc,
	SYS_getcpuid,
	SYS_serial_write,
	SYS_serial_read,
	SYS_shared_page_alloc,
	SYS_shared_page_free,
	SYS_getenvid,
	SYS_env_destroy,
	SYS_yield,
	SYS_proc_create,
	SYS_proc_run,

	SYS_endofcalls //Should always be last
};
#define NSYSCALLS (SYS_endofcalls -1)
// syscall number starts at 1 and goes up to NSYSCALLS, without holes.
#define INVALID_SYSCALL(syscallno) ((syscallno) > NSYSCALLS)

/* For Buster Measurement Flags */
#define BUSTER_SHARED			0x0001
#define BUSTER_STRIDED			0x0002
#define BUSTER_LOCKED			0x0004
#define BUSTER_PRINT_TICKS		0x0008
#define BUSTER_JUST_LOCKS		0x0010 // unimplemented

#define NUM_SYSCALL_ARGS 6
typedef struct syscall_req {
	uint32_t num;
	uint32_t flags;
	uint32_t args[NUM_SYSCALL_ARGS];
} syscall_req_t;

typedef struct syscall_rsp {
	int32_t retval;
} syscall_rsp_t;

// Generic Syscall Ring Buffer
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

#endif /* !ROS_INCLUDE_SYSCALL_H */
