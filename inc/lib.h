// Main public header file for our user-land support library,
// whose code lives in the lib directory.
// This library is roughly our OS's version of a standard C library,
// and is intended to be linked into all user-mode applications
// (NOT the kernel or boot loader).

#ifndef ROS_INC_LIB_H
#define ROS_INC_LIB_H 1

#include <inc/types.h>
#include <inc/stdarg.h>
#include <inc/string.h>
#include <inc/error.h>
#include <inc/env.h>
#include <inc/memlayout.h>
#include <inc/syscall.h>
#include <inc/pool.h>
// These two are included below because of dependency issues.
//#include <inc/stdio.h>
//#include <inc/assert.h>

#define USED(x)		(void)(x)

// libos.c or entry.S
extern char *binaryname;
extern volatile env_t *env;
// will need to change these types when we have real structs
// seems like they need to be either arrays [] or functions () for it to work
extern volatile uint8_t (COUNT(PGSIZE * UINFO_PAGES) procinfo)[];
extern volatile uint8_t (COUNT(PGSIZE * UDATA_PAGES) procdata)[];
extern syscall_front_ring_t sysfrontring;
extern volatile page_t pages[];
void	exit(void);

// readline.c
char*	readline(const char *buf);

// syscall.c
void sys_null();
void sys_cputs(const char *string, size_t len);
void sys_cputs_async(const char *string, size_t len, syscall_desc_t* desc);
int	sys_cgetc(void);
envid_t	sys_getenvid(void);
int	sys_env_destroy(envid_t);
error_t waiton_syscall(syscall_desc_t* desc, syscall_rsp_t* rsp);

// async callback
#define MAX_SYSCALLS 100
#define MAX_ASYNCCALLS 10
// This is the high-level object a process waits, with multiple syscalls within.
typedef syscall_desc_list_t async_desc_t;
// This is per-thread, and used when entering a async library call to properly
// group syscall_desc_t used during the processing of that async call
extern async_desc_t* current_async_desc;
// stdio.h needs to be included after async_desc_t.  assert.h includes stdio.h.
#include <inc/stdio.h>
#include <inc/assert.h>


// This pooltype contains syscall_desc_t, which is how you wait on one syscall.
POOL_TYPE_DEFINE(syscall_desc_t, syscall_desc_pool, MAX_SYSCALLS);
POOL_TYPE_DEFINE(async_desc_t, async_desc_pool, MAX_ASYNCCALLS);
// These are declared in libmain.c
extern syscall_desc_pool_t syscall_desc_pool;
extern async_desc_pool_t async_desc_pool;
// Finds a free async_desc_t, on which you can wait for a series of syscalls
async_desc_t* get_async_desc(void);
// Wait on all syscalls within this async call.  TODO - timeout or something?
error_t waiton_async_call(async_desc_t* desc);
// Finds a free sys_desc_t, on which you can wait for a specific syscall, and
// binds it to the group desc.
syscall_desc_t* get_sys_desc(async_desc_t* desc);


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
