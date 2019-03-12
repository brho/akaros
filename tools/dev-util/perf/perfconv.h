/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Converts kprof profiler files into Linux perf ones. The Linux Perf file
 * format has bee illustrated here:
 *
 * https://lwn.net/Articles/644919/
 * https://openlab-mu-internal.web.cern.ch/openlab-mu-internal/03_Documents/
 *	 3_Technical_Documents/Technical_Reports/2011/Urs_Fassler_report.pdf
 *
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include "xlib.h"
#include "perf_format.h"

struct mem_file_reloc {
	struct mem_file_reloc *next;
	uint64_t *ptr;
};

struct mem_block {
	struct mem_block *next;
	char *base;
	char *top;
	char *wptr;
};

struct mem_file {
	size_t size;
	struct mem_block *head;
	struct mem_block *tail;
	struct mem_file_reloc *relocs;
};

struct static_mmap64 {
	struct static_mmap64 *next;
	uint64_t addr;
	uint64_t size;
	uint64_t offset;
	uint32_t pid;
	uint32_t tid;
	uint16_t header_misc;
	char *path;
};

struct perf_headers {
	struct mem_block *headers[HEADER_FEAT_BITS];
};

struct perf_event_id {
	uint64_t event;
	uint64_t id;
};

struct perfconv_context {
	struct perf_context *pctx;
	int debug_level;
	struct static_mmap64 *static_mmaps;
	struct perf_header ph;
	struct perf_headers hdrs;
	struct mem_file fhdrs, attr_ids, attrs, data, event_types;
};

extern char *cmd_line_save;

struct perfconv_context *perfconv_create_context(struct perf_context *pctx);
void perfconv_free_context(struct perfconv_context *cctx);
void perfconv_set_dbglevel(int level, struct perfconv_context *cctx);
void perfconv_add_kernel_mmap(struct perfconv_context *cctx);
void perfconv_add_kernel_buildid(struct perfconv_context *cctx);
void perfconv_process_input(struct perfconv_context *cctx, FILE *input,
			    FILE *output);
