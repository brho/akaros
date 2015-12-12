/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>

#define PROF_DOM_SHIFT (8 * sizeof(uint64_t) - 4)
#define PROF_INFO_MASK (((uint64_t) 1 << PROF_DOM_SHIFT) - 1)

#define PROF_MKINFO(dom, dinfo)								\
	(((uint64_t) (dom) << PROF_DOM_SHIFT) | ((dinfo) & PROF_INFO_MASK))

#define PROF_INFO_DOM(i) ((uint64_t) (i) >> PROF_DOM_SHIFT)
#define PROF_INFO_DATA(i) ((i) & PROF_INFO_MASK)

#define PROF_DOM_TIMER 1
#define PROF_DOM_PMU 2

#define PROFTYPE_KERN_TRACE64	1

struct proftype_kern_trace64 {
	uint64_t info;
	uint64_t tstamp;
	uint16_t cpu;
	uint16_t num_traces;
	uint64_t trace[0];
} __attribute__((packed));

#define PROFTYPE_USER_TRACE64	2

struct proftype_user_trace64 {
	uint64_t info;
	uint64_t tstamp;
	uint32_t pid;
	uint16_t cpu;
	uint16_t num_traces;
	uint64_t trace[0];
} __attribute__((packed));

#define PROFTYPE_PID_MMAP64		3

struct proftype_pid_mmap64 {
	uint64_t tstamp;
	uint64_t addr;
	uint64_t size;
	uint64_t offset;
	uint32_t pid;
	uint8_t path[0];
} __attribute__((packed));

#define PROFTYPE_NEW_PROCESS	4

struct proftype_new_process {
	uint64_t tstamp;
	uint32_t pid;
	uint8_t path[0];
} __attribute__((packed));
