/* See COPYRIGHT for copyright information. */

#ifndef ROS_PROCDATA_H
#define ROS_PROCDATA_H

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/ring_syscall.h>
#include <ros/common.h>
#include <ros/procinfo.h>
#include <arch/mmu.h>

typedef struct procdata {
	// The actual ring buffers for communicating with user space
	syscall_sring_t  syscallring;  // Per-process ring buffer for async syscalls
	char padding1[SYSCALLRINGSIZE - sizeof(syscall_sring_t)];
	sysevent_sring_t syseventring; // Per-process ring buffer for async sysevents
	char padding2[SYSEVENTRINGSIZE - sizeof(sysevent_sring_t)];
#ifdef __i386__
	segdesc_t *ldt;
#endif
} procdata_t;
#define PROCDATA_NUM_PAGES  ((sizeof(procdata_t)-1)/PGSIZE + 1)

#endif // !ROS_PROCDATA_H
