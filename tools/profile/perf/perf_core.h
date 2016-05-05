/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <ros/arch/arch.h>
#include <ros/arch/perfmon.h>
#include <ros/common.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include "akaros.h"
#include "perfconv.h"

#define MAX_CPU_EVENTS 256

struct perf_arch_info {
	uint32_t perfmon_version;
	uint32_t proc_arch_events;
	uint32_t bits_x_counter;
	uint32_t counters_x_proc;
	uint32_t bits_x_fix_counter;
	uint32_t fix_counters_x_proc;
};

struct perf_eventsel {
	struct perfmon_event ev;
	int eidx;
};

struct perf_event {
	struct core_set cores;
	struct perf_eventsel sel;
	int ped;
};

struct perf_context {
	int perf_fd;
	int kpctl_fd;
	struct perf_arch_info pai;
	int event_count;
	struct perf_event events[MAX_CPU_EVENTS];
};

struct perf_context_config {
	const char *perf_file;
	const char *kpctl_file;
};

void perf_initialize(int argc, char *argv[]);
void perf_finalize(void);
void perf_parse_event(const char *str, struct perf_eventsel *sel);
struct perf_context *perf_create_context(const struct perf_context_config *cfg);
void perf_free_context(struct perf_context *pctx);
void perf_flush_context_traces(struct perf_context *pctx);
void perf_context_event_submit(struct perf_context *pctx,
							   const struct core_set *cores,
							   const struct perf_eventsel *sel);
void perf_context_show_values(struct perf_context *pctx, FILE *file);
void perf_show_events(const char *rx, FILE *file);
void perf_get_event_string(const struct perf_eventsel *sel, char *sbuf,
						   size_t size);
void perf_make_eventsel_from_event_mask(struct perf_eventsel *sel,
										uint32_t event, uint32_t mask);
void perf_convert_trace_data(struct perfconv_context *cctx, const char *input,
							 const char *output);

static inline const struct perf_arch_info *perf_context_get_arch_info(
	const struct perf_context *pctx)
{
	return &pctx->pai;
}
