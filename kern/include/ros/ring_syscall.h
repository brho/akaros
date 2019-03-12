#pragma once

#include <ros/common.h>
#include <ros/ring_buffer.h>

#define NUM_SYSCALL_ARGS 6
/* This will need to change to represent sending pointers to syscalls, not the
 * syscalls themselves */
struct syscall;
typedef enum {
	// The response has been digested by the user space, can be reallocated
	RES_free, 
	// Space fo request is allocated
	REQ_alloc,
	// The request is populated by the caller
	REQ_ready,
	// The request is being processed, or a kernel thread is going to pick
	// up the stack to process this later.
	REQ_processing,
	// The response is ready to be picked up
	RES_ready
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
