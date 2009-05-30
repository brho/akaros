#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

#include <arch/types.h>
#include <ros/ring_buffer.h>
#include <ros/queue.h>

/* system call numbers */
enum
{
	SYS_begofcalls, //Should always be first

	SYS_null,
	SYS_cache_buster,
	SYS_cache_invalidate,
	SYS_cputs,
	SYS_cgetc,
	SYS_getenvid,
	SYS_getcpuid,
	SYS_env_destroy,
	SYS_serial_write,
	SYS_serial_read,

	SYS_endofcalls //Should always be last
};
#define NSYSCALLS (SYS_endofcalls -1)
// syscall number starts at 1 and goes up to NSYSCALLS, without holes.
#define INVALID_SYSCALL(syscallno) ((syscallno) > NSYSCALLS)

//intreg_t syscall_sysenter(uint16_t num, intreg_t a1,
//                          intreg_t a2, intreg_t a3,
//                          intreg_t a4, intreg_t a5);
int32_t syscall_sysenter(uint16_t num, int32_t a1,
                         int32_t a2, int32_t a3,
                         int32_t a4, int32_t a5);

#define NUM_SYS_ARGS 6
typedef struct syscall_req {
	uint32_t num;
	uint32_t flags;
	uint32_t args[NUM_SYS_ARGS];
} syscall_req_t;

typedef struct syscall_rsp {
	int32_t retval;
} syscall_rsp_t;

// Generic Syscall Ring Buffer
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

typedef struct syscall_desc syscall_desc_t;
struct syscall_desc {
	LIST_ENTRY(syscall_desc_t) next;
	syscall_front_ring_t* sysfr;
	uint32_t idx;
	// cleanup
	void (*cleanup)(void* data);
	void* data;
};
LIST_HEAD(syscall_desc_list_t, syscall_desc_t);

#endif /* !ROS_INCLUDE_SYSCALL_H */
