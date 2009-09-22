#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

/* system call numbers.  need to #def them for use in assembly. */
#define SYS_null					 1
#define SYS_cache_buster			 2
#define SYS_cache_invalidate		 3
#define SYS_cputs					 4
#define SYS_cgetc					 5
#define SYS_getcpuid				 6
#define SYS_getpid					 7
#define SYS_proc_destroy			 8
#define SYS_shared_page_alloc		 9
#define SYS_shared_page_free		10
#define SYS_yield					11
#define SYS_proc_create				12
#define SYS_proc_run				13
#define SYS_mmap					14
#define SYS_brk						15
/*
#define SYS_mprotect
#define SYS_mremap
#define SYS_mincore // can read page tables instead
#define SYS_madvise
#define SYS_mlock
#define SYS_msync
*/
#define SYS_resource_req			16
/* Read and write buffers over the serial port */
#define SYS_serial_write			17
#define SYS_serial_read				18
/* The next 3 syscalls go with the experimental network driver. These syscalls
 * are used by newlib_backend / our remote binary loader to pull data from /
 * put data into a buffer managed by the network driver.  These should go away
 * as things mature. */
#define SYS_eth_read				19
#define SYS_eth_write				20
#define SYS_run_binary				21
// forward a syscall to front-end machine
#define SYS_frontend				22
// Keep this in sync with the last syscall number
#define NSYSCALLS 					22
// syscall number starts at 1 and goes up to NSYSCALLS, without holes.
#define INVALID_SYSCALL(syscallno) ((syscallno) > NSYSCALLS)

/* For Buster Measurement Flags */
#define BUSTER_SHARED			0x0001
#define BUSTER_STRIDED			0x0002
#define BUSTER_LOCKED			0x0004
#define BUSTER_PRINT_TICKS		0x0008
#define BUSTER_JUST_LOCKS		0x0010 // unimplemented

#ifndef __ASSEMBLER__

#include <ros/common.h>
#include <ros/ring_buffer.h>

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
#define SYSCALLRINGSIZE    PGSIZE
//DEFINE_RING_TYPES_WITH_SIZE(syscall, syscall_req_t, syscall_rsp_t, SYSCALLRINGSIZE);
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

#endif /* __ASSEMBLER__ */
#endif /* !ROS_INCLUDE_SYSCALL_H */
