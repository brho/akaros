/* See COPYRIGHT for copyright information. */

#ifndef ROS_PROCDATA_H
#define ROS_PROCDATA_H

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/sysevent.h>
#include <ros/error.h>
#include <ros/common.h>

typedef struct procinfo {
	pid_t id;
} procinfo_t;
#define PROCINFO_NUM_PAGES  ((sizeof(procinfo_t)-1)/PGSIZE + 1)	

typedef struct procdata {
	// The actual ring buffers for communicating with user space
	syscall_sring_t  syscallring;  // Per-process ring buffer for async syscalls
	sysevent_sring_t syseventring; // Per-process ring buffer for async sysevents
} procdata_t;
#define PROCDATA_NUM_PAGES  ((sizeof(procdata_t)-1)/PGSIZE + 1)

#endif // !ROS_PROCDATA_H
