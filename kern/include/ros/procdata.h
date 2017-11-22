/* Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.  */

#pragma once

#include <ros/memlayout.h>
#include <ros/ring_syscall.h>
#include <ros/sysevent.h>
#include <ros/arch/arch.h>
#include <ros/common.h>
#include <ros/procinfo.h>
#include <ros/event.h>

typedef struct procdata {
	/*
	syscall_sring_t			syscallring;
	char					pad1[SYSCALLRINGSIZE - sizeof(syscall_sring_t)];
	*/
	syscall_sring_t			*syscallring;
	sysevent_sring_t		syseventring;
	char					pad2[SYSEVENTRINGSIZE - sizeof(sysevent_sring_t)];
	bool					padb;
	uint8_t					pad8;
	uint16_t				pad16;
	uint32_t				pad32;
	struct resource_req		res_req[MAX_NUM_RESOURCES];
	struct event_queue		*kernel_evts[MAX_NR_EVENT];
	/* Long range, would like these to be mapped in lazily, as the vcores are
	 * requested.  Sharing MAX_NUM_CORES is a bit weird too. */
	struct preempt_data		vcore_preempt_data[MAX_NUM_CORES];
} procdata_t;

#define PROCDATA_NUM_PAGES  ((sizeof(procdata_t)-1)/PGSIZE + 1)

/* TODO: I dislike having this not be a pointer (for kernel programming) */
#define __procdata (*(procdata_t*)UDATA)
