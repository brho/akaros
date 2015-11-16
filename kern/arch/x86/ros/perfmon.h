/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <ros/common.h>
#include <ros/memops.h>
#include <ros/bitfield.h>
#include <stdint.h>

/* The request format for the #arch/perf device is as follow (all the integers
 * listed here are little endian):
 *
 * U8 CMD;
 * [CMD dependent payload]
 *
 * The following command are supported, with their own structure:
 *
 * PERFMON_CMD_COUNTER_OPEN request
 *   U8 CMD; (= PERFMON_CMD_COUNTER_OPEN)
 *   U64 EVENT_DESCRIPTOR;
 *   U64 EVENT_FLAGS;
 *   U64 EVENT_TRIGGER_COUNT;
 *   U32 NUM_CPUMASK_BYTES;
 *   U8 CPUMASK_BYTES[NUM_CPUMASK_BYTES];
 * PERFMON_CMD_COUNTER_OPEN response
 *   U32 EVENT_DESCRIPTOR;
 *
 * PERFMON_CMD_COUNTER_STATUS request
 *   U8 CMD; (= PERFMON_CMD_COUNTER_STATUS)
 *   U32 EVENT_DESCRIPTOR;
 * PERFMON_CMD_COUNTER_STATUS response
 *   U64 EVENT_DESCRIPTOR;
 *   U64 EVENT_FLAGS;
 *   U64 EVENT_TRIGGER_COUNT;
 *   U32 NUM_VALUES; (always num_cores)
 *   U64 VALUES[NUM_VALUES]; (one value per core - zero if the counter was not
 *                            active in that core)
 *
 * PERFMON_CMD_COUNTER_CLOSE request
 *   U8 CMD; (= PERFMON_CMD_COUNTER_CLOSE)
 *   U32 EVENT_DESCRIPTOR;
 * PERFMON_CMD_COUNTER_CLOSE response
 *   NONE
 *
 * PERFMON_CMD_CPU_CAPS request
 *   U8 CMD; (= PERFMON_CMD_CPU_CAPS)
 * PERFMON_CMD_CPU_CAPS response
 *   U32 PERFMON_VERSION;
 *   U32 ARCH_EVENTS;
 *   U32 BITS_X_COUNTER;
 *   U32 COUNTERS_X_PROC;
 *   U32 BITS_X_FIX_COUNTER;
 *   U32 FIX_COUNTERS_X_PROC;
 */

#define PERFMON_CMD_COUNTER_OPEN 1
#define PERFMON_CMD_COUNTER_STATUS 2
#define PERFMON_CMD_COUNTER_CLOSE 3
#define PERFMON_CMD_CPU_CAPS 4

#define PERFMON_FIXED_EVENT (1 << 0)

#define PMEV_EVENT MKBITFIELD(0, 8)
#define PMEV_MASK MKBITFIELD(8, 8)
#define PMEV_USR MKBITFIELD(16, 1)
#define PMEV_OS MKBITFIELD(17, 1)
#define PMEV_EDGE MKBITFIELD(18, 1)
#define PMEV_PC MKBITFIELD(19, 1)
#define PMEV_INTEN MKBITFIELD(20, 1)
#define PMEV_ANYTH MKBITFIELD(21, 1)
#define PMEV_EN MKBITFIELD(22, 1)
#define PMEV_INVCMSK MKBITFIELD(23, 1)
#define PMEV_CMASK MKBITFIELD(24, 8)

#define PMEV_GET_EVENT(v) BF_GETFIELD(v, PMEV_EVENT)
#define PMEV_SET_EVENT(v, x) BF_SETFIELD(v, x, PMEV_EVENT)
#define PMEV_GET_MASK(v) BF_GETFIELD(v, PMEV_MASK)
#define PMEV_SET_MASK(v, x) BF_SETFIELD(v, x, PMEV_MASK)
#define PMEV_GET_USR(v) BF_GETFIELD(v, PMEV_USR)
#define PMEV_SET_USR(v, x) BF_SETFIELD(v, x, PMEV_USR)
#define PMEV_GET_OS(v) BF_GETFIELD(v, PMEV_OS)
#define PMEV_SET_OS(v, x) BF_SETFIELD(v, x, PMEV_OS)
#define PMEV_GET_EDGE(v) BF_GETFIELD(v, PMEV_EDGE)
#define PMEV_SET_EDGE(v, x) BF_SETFIELD(v, x, PMEV_EDGE)
#define PMEV_GET_PC(v) BF_GETFIELD(v, PMEV_PC)
#define PMEV_SET_PC(v, x) BF_SETFIELD(v, x, PMEV_PC)
#define PMEV_GET_INTEN(v) BF_GETFIELD(v, PMEV_INTEN)
#define PMEV_SET_INTEN(v, x) BF_SETFIELD(v, x, PMEV_INTEN)
#define PMEV_GET_ANYTH(v) BF_GETFIELD(v, PMEV_ANYTH)
#define PMEV_SET_ANYTH(v, x) BF_SETFIELD(v, x, PMEV_ANYTH)
#define PMEV_GET_EN(v) BF_GETFIELD(v, PMEV_EN)
#define PMEV_SET_EN(v, x) BF_SETFIELD(v, x, PMEV_EN)
#define PMEV_GET_INVCMSK(v) BF_GETFIELD(v, PMEV_INVCMSK)
#define PMEV_SET_INVCMSK(v, x) BF_SETFIELD(v, x, PMEV_INVCMSK)
#define PMEV_GET_CMASK(v) BF_GETFIELD(v, PMEV_CMASK)
#define PMEV_SET_CMASK(v, x) BF_SETFIELD(v, x, PMEV_CMASK)

struct perfmon_event {
	uint64_t event;
	uint64_t flags;
	uint64_t trigger_count;
};

static inline void perfmon_init_event(struct perfmon_event *pev)
{
	ZERO_DATA(*pev);
}

static inline bool perfmon_is_fixed_event(const struct perfmon_event *pev)
{
	return (pev->flags & PERFMON_FIXED_EVENT) != 0;
}
