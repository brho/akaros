/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <ros/common.h>
#include <ros/arch/perfmon.h>
#include <arch/x86.h>
#include <atomic.h>
#include <core_set.h>
#include <stdint.h>
#include <kthread.h>

#define MAX_VAR_COUNTERS 32
#define MAX_FIX_COUNTERS 16
#define MAX_PERFMON_COUNTERS (MAX_VAR_COUNTERS + MAX_FIX_COUNTERS)
#define INVALID_COUNTER INT32_MIN

struct hw_trapframe;

typedef int32_t counter_t;

struct perfmon_cpu_caps {
	uint32_t perfmon_version;
	uint32_t proc_arch_events;
	uint32_t bits_x_counter;
	uint32_t counters_x_proc;
	uint32_t bits_x_fix_counter;
	uint32_t fix_counters_x_proc;
};

struct perfmon_alloc {
	struct perfmon_event ev;
	counter_t cores_counters[0];
};

struct perfmon_session {
	qlock_t qlock;
	struct perfmon_alloc *allocs[MAX_PERFMON_COUNTERS];
};

struct perfmon_status {
	struct perfmon_event ev;
	uint64_t cores_values[0];
};

bool perfmon_supported(void);
void perfmon_global_init(void);
void perfmon_pcpu_init(void);
void perfmon_snapshot_hwtf(struct hw_trapframe *hw_tf);
void perfmon_snapshot_vmtf(struct vm_trapframe *vm_tf);
void perfmon_interrupt(struct hw_trapframe *hw_tf, void *data);
void perfmon_get_cpu_caps(struct perfmon_cpu_caps *pcc);
int perfmon_open_event(const struct core_set *cset, struct perfmon_session *ps,
		       const struct perfmon_event *pev);
void perfmon_close_event(struct perfmon_session *ps, int ped);
struct perfmon_status *perfmon_get_event_status(struct perfmon_session *ps,
						int ped);
void perfmon_free_event_status(struct perfmon_status *pef);
struct perfmon_session *perfmon_create_session(void);
void perfmon_close_session(struct perfmon_session *ps);

static inline uint64_t read_pmc(uint32_t index)
{
	uint32_t edx, eax;

	asm volatile("rdpmc" : "=d"(edx), "=a"(eax) : "c"(index));
	return ((uint64_t) edx << 32) | eax;
}
