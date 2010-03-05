/* Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.  */

#ifndef ROS_PROCDATA_H
#define ROS_PROCDATA_H

#include <ros/memlayout.h>
#include <ros/ring_syscall.h>
#include <ros/arch/arch.h>
#include <ros/common.h>
#include <ros/procinfo.h>
#include <ros/notification.h>
#include <arch/mmu.h>

typedef struct procdata {
	syscall_sring_t			syscallring;
	char					pad1[SYSCALLRINGSIZE - sizeof(syscall_sring_t)];
	sysevent_sring_t		syseventring;
	char					pad2[SYSEVENTRINGSIZE - sizeof(sysevent_sring_t)];
#ifdef __i386__
	segdesc_t				*ldt; // TODO: bug with this.  needs to go
#endif
	// TODO: will replace these in a later commit
	uintptr_t stack_pointers[MAX_NUM_CPUS];
	struct notif_method		notif_methods[MAX_NR_NOTIF];
	/* Long range, would like these to be mapped in lazily, as the vcores are
	 * requested.  Sharing MAX_NUM_CPUS is a bit weird too. */
	struct preempt_data		vcore_preempt_data[MAX_NUM_CPUS];
} procdata_t;

#define PROCDATA_NUM_PAGES  ((sizeof(procdata_t)-1)/PGSIZE + 1)

// this is how user programs access the procdata page
#ifndef ROS_KERNEL
# define __procdata (*(procdata_t*)UDATA)
#endif

#endif // !ROS_PROCDATA_H
