#pragma once

#include <ros/bits/syscall.h>
#include <ros/arch/syscall.h>
#include <ros/event.h>
#include <ros/atomic.h>

/* Flags for an individual syscall. */
#define SC_DONE					0x0001		/* SC is done */
#define SC_PROGRESS				0x0002		/* SC made progress */
#define SC_UEVENT				0x0004		/* user has an ev_q */
#define SC_K_LOCK				0x0008		/* kernel locked sysc */
#define SC_ABORT				0x0010		/* syscall abort attempted */

#define MAX_ERRSTR_LEN			128
#define SYSTR_BUF_SZ			PGSIZE

struct syscall {
	unsigned int				num;
	int							err;			/* errno */
	long						retval;
	atomic_t					flags;
	struct event_queue			*ev_q;
	void						*u_data;
	long						arg0;
	long						arg1;
	long						arg2;
	long						arg3;
	long						arg4;
	long						arg5;
	char						errstr[MAX_ERRSTR_LEN];
};

struct childfdmap {
	unsigned int				parentfd;
	unsigned int				childfd;
	int							ok;
};

struct argenv {
	size_t argc;
	size_t envc;
	char buf[];
	/* The buf array is laid out as follows:
	 * buf {
	 *   char *argv[argc]; // Offset of arg relative to &argbuf[0]
	 *   char *envp[envc]; // Offset of envvar relative to &argbuf[0]
	 *   char argbuf[sum(map(strlen + 1, argv + envp))];
	 * }
	 */
};

#ifndef ROS_KERNEL

/* Temp hack, til the rest of glibc/userspace uses sys/syscall.h */
#include <sys/syscall.h>
#endif /* ifndef ROS_KERNEL */
