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
#include <parlib/core_set.h>
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

#define MAX_FQSTR_SZ 128
struct perf_eventsel {
	struct perfmon_event ev;
	bool attr_emitted;
	uint32_t type;
	uint64_t config;
	char fq_str[MAX_FQSTR_SZ];
};

struct perf_event {
	struct core_set cores;
	struct perf_eventsel sel;
	int ped;
};

struct perf_context_config {
	const char *perf_file;
	const char *kpctl_file;
	const char *kpdata_file;
};

struct perf_context {
	struct perf_context_config *cfg;
	int perf_fd;
	int kpctl_fd;
	struct perf_arch_info pai;
	int event_count;
	struct perf_event events[MAX_CPU_EVENTS];
};

void perf_initialize(void);
void perf_finalize(void);
struct perf_eventsel *perf_parse_event(const char *str);
struct perf_context *perf_create_context(struct perf_context_config *cfg);
void perf_free_context(struct perf_context *pctx);
void perf_context_event_submit(struct perf_context *pctx,
			       const struct core_set *cores,
			       const struct perf_eventsel *sel);
void perf_stop_events(struct perf_context *pctx);
void perf_start_sampling(struct perf_context *pctx);
void perf_stop_sampling(struct perf_context *pctx);
uint64_t perf_get_event_count(struct perf_context *pctx, unsigned int idx);
void perf_context_show_events(struct perf_context *pctx, FILE *file);
void perf_show_events(const char *rx, FILE *file);
void perf_convert_trace_data(struct perfconv_context *cctx, const char *input,
			     FILE *outfile);

static inline const struct perf_arch_info *perf_context_get_arch_info(
	const struct perf_context *pctx)
{
	return &pctx->pai;
}
