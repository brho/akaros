/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>

#define PROFTYPE_KERN_TRACE64	1

struct proftype_kern_trace64 {
	uint64_t info;
	uint64_t tstamp;
	uint32_t pid;
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

#define PROFTYPE_PID_MMAP64	3

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
