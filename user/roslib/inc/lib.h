// Main public header file for our user-land support library,
// whose code lives in the lib directory.
// This library is roughly our OS's version of a standard C library,
// and is intended to be linked into all user-mode applications
// (NOT the kernel or boot loader).

#ifndef ROS_INC_LIB_H
#define ROS_INC_LIB_H 1

#include <arch/types.h>
#include <arch/timer.h>
#include <ros/error.h>
#include <stdarg.h>
#include <string.h>
#include <pool.h>

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/env.h>

#define USED(x)		(void)(x)

// libos.c or entry.S
extern char *binaryname;
extern volatile env_t *env;
// will need to change these types when we have real structs
// seems like they need to be either arrays [] or functions () for it to work
extern volatile uint8_t (COUNT(PGSIZE * UINFO_PAGES) procinfo)[];
extern volatile uint8_t (COUNT(PGSIZE * UDATA_PAGES) procdata)[];
extern syscall_front_ring_t sysfrontring;
void exit(void) __attribute__((noreturn));

// syscall.c
void        sys_null();
error_t     sys_null_async(syscall_desc_t* desc);
void        sys_cache_invalidate();
void        sys_cache_buster(uint32_t num_writes, uint32_t num_pages,
                             uint32_t flags);
error_t     sys_cache_buster_async(syscall_desc_t* desc, uint32_t num_writes,
                                   uint32_t num_pages, uint32_t flags);
ssize_t     sys_cputs(const char *string, size_t len);
error_t     sys_cputs_async(const char *s, size_t len, syscall_desc_t* desc,
                            void (*cleanup_handler)(void*), void* cleanup_data);
uint16_t    sys_cgetc(void);
envid_t     sys_getenvid(void);
envid_t     sys_getcpuid(void);
error_t     sys_env_destroy(envid_t);
error_t     waiton_syscall(syscall_desc_t* desc, syscall_rsp_t* rsp);

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
// stdio.h needs to be included after async_desc_t.  assert.h includes stdio.h.
#include <stdio.h>
#include <assert.h>

// This pooltype contains syscall_desc_t, which is how you wait on one syscall.
POOL_TYPE_DEFINE(syscall_desc_t, syscall_desc_pool, MAX_SYSCALLS);
POOL_TYPE_DEFINE(async_desc_t, async_desc_pool, MAX_ASYNCCALLS);

// This pooltype contains all the timers used in user level time tracking
POOL_TYPE_DEFINE(timer_t, timer_pool, MAX_TIMERS);

// These are declared in libmain.c
extern syscall_desc_pool_t syscall_desc_pool;
extern async_desc_pool_t async_desc_pool;
extern timer_pool_t timer_pool;

error_t waiton_async_call(async_desc_t* desc, async_rsp_t* rsp);
async_desc_t* get_async_desc(void);
syscall_desc_t* get_sys_desc(async_desc_t* desc);
error_t get_all_desc(async_desc_t** a_desc, syscall_desc_t** s_desc);


/* File open modes */
#define	O_RDONLY	0x0000		/* open for reading only */
#define	O_WRONLY	0x0001		/* open for writing only */
#define	O_RDWR		0x0002		/* open for reading and writing */
#define	O_ACCMODE	0x0003		/* mask for above modes */

#define	O_CREAT		0x0100		/* create if nonexistent */
#define	O_TRUNC		0x0200		/* truncate to zero length */
#define	O_EXCL		0x0400		/* error if already exists */
#define O_MKDIR		0x0800		/* create directory, not regular file */

#endif	// !ROS_INC_LIB_H
