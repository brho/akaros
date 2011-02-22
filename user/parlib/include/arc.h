// Header for Asynch Remote Call, currently only support remote syscalls.
#ifndef _ARC_H
#define _ARC_H

#ifdef __cplusplus
  extern "C" {
#endif

#include <parlib.h>
#include <error.h>
#include <pool.h>
#include <assert.h>
#include <sys/queue.h>
#include <ros/ring_syscall.h>

extern syscall_front_ring_t syscallfrontring;
extern sysevent_back_ring_t syseventbackring;

/*
 * Syscall Descriptor: This helps userspace track a specific syscall.  Includes
 * a cleanup function to be run when this syscall is complete.  Linked list of
 * these for now. (Tail Queue)
 */
typedef struct syscall_desc syscall_desc_t;
struct syscall_desc {
	TAILQ_ENTRY(syscall_desc) next;
	syscall_front_ring_t* sysfr;
	uint32_t idx;
	// cleanup
	void (*cleanup)(void* data);
	void* data;
};
TAILQ_HEAD(syscall_desc_list, syscall_desc);
typedef struct syscall_desc_list syscall_desc_list_t;


// TODO: where to declare async syscalls?

// async callback
#define MAX_SYSCALLS 100
#define MAX_ASYNCCALLS 100

// The high-level object a process waits on, with multiple syscalls within.
typedef struct async_desc {
	syscall_desc_list_t syslist;
	void (*cleanup)(void* data);
	void* data;
} async_desc_t;

// Response to an async call.  Should be some sort of aggregation of the
// syscall responses.
typedef struct async_rsp_t {
	int32_t retval;
} async_rsp_t;

// This is per-thread, and used when entering a async library call to properly
// group syscall_desc_t used during the processing of that async call
extern async_desc_t* current_async_desc;

// This pooltype contains syscall_desc_t, which is how you wait on one syscall.
POOL_TYPE_DEFINE(syscall_desc_t, syscall_desc_pool, MAX_SYSCALLS);
POOL_TYPE_DEFINE(async_desc_t, async_desc_pool, MAX_ASYNCCALLS);

// These are declared in asynccall.c
extern syscall_desc_pool_t syscall_desc_pool;
extern async_desc_pool_t async_desc_pool;

/* Initialize front and back rings of syscall/event ring */
void init_arc();

/* Generic Async Call */
int waiton_syscall(syscall_desc_t* desc, syscall_rsp_t* rsp);

/* Async group call */
int waiton_async_call(async_desc_t* desc, async_rsp_t* rsp);

int async_syscall(syscall_req_t* req, syscall_desc_t* desc);

async_desc_t* get_async_desc(void);
syscall_desc_t* get_sys_desc(async_desc_t* desc);
int get_all_desc(async_desc_t** a_desc, syscall_desc_t** s_desc);

#ifdef __cplusplus
  }
#endif

#endif
