#ifndef _ROS_RING_SYSCALL_H
#define _ROS_RING_SYSCALL_H

#include <ros/common.h>
#include <ros/ring_buffer.h>

#define NUM_SYSCALL_ARGS 6
/* This will need to change to represent sending pointers to syscalls, not the
 * syscalls themselves */
struct syscall;
typedef enum {
	RES_free,  // The response has been digested by the user space, can be reallocated
	REQ_alloc, // Space fo request is allocated
	REQ_ready, // The request is populated by the caller
	REQ_processing, // The request is being processed, 
					// or a kernel thread is going to pick up the stack to process this later.

	RES_ready // The response is ready to be picked up
} syscall_status_t;

typedef struct syscall_req {
    syscall_status_t status; // TODO:rethink this
	void (*cleanup)(void* data);
	void *data;
	struct syscall* sc;
} syscall_req_t, syscall_rsp_t;

#define RSP_ERRNO(rsp) (rsp->sc->err)
#define RSP_RESULT(rsp) (rsp->sc->retval)

// Generic Syscall Ring Buffer
#define SYSCALLRINGSIZE    PGSIZE
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

#endif
