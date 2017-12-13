/* Copyright (c) 2015-2016 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Stephane Eranian <eranian@gmail.com> (perf_show_event_info() from libpfm4)
 *
 * See LICENSE for details. */

#include <ros/arch/msr-index.h>
#include <ros/arch/perfmon.h>
#include <ros/common.h>
#include <ros/memops.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <regex.h>
#include <parlib/parlib.h>
#include <parlib/core_set.h>
#include <perfmon/err.h>
#include <perfmon/pfmlib.h>
#include "xlib.h"
#include "perfconv.h"
#include "perf_core.h"
#include "elf.h"

struct perf_generic_event {
	char						*name;
	uint32_t					type;
	uint32_t					config;
};

struct perf_generic_event generic_events[] = {
	{ .name = "cycles",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_CPU_CYCLES,
	},
	{ .name = "cpu-cycles",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_CPU_CYCLES,
	},
	{ .name = "instructions",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_INSTRUCTIONS,
	},
	{ .name = "cache-references",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_CACHE_REFERENCES,
	},
	{ .name = "cache-misses",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_CACHE_MISSES,
	},
	{ .name = "branches",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
	},
	{ .name = "branch-instructions",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
	},
	{ .name = "branch-misses",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_BRANCH_MISSES,
	},
	{ .name = "bus-cycles",
	  .type = PERF_TYPE_HARDWARE,
	  .config = PERF_COUNT_HW_BUS_CYCLES,
	},
};

static const char *perf_get_event_mask_name(const pfm_event_info_t *einfo,
											uint32_t mask)
{
	int i;
	pfm_event_attr_info_t ainfo;

	ZERO_DATA(ainfo);
	ainfo.size = sizeof(ainfo);
	pfm_for_each_event_attr(i, einfo) {
		pfm_err_t err = pfm_get_event_attr_info(einfo->idx, i, PFM_OS_NONE,
												&ainfo);

		if (err != PFM_SUCCESS) {
			fprintf(stderr, "Failed to get attribute info: %s\n",
					pfm_strerror(err));
			exit(1);
		}
		if (ainfo.type == PFM_ATTR_UMASK) {
			if (mask == (uint32_t) ainfo.code)
				return ainfo.name;
		}
	}

	return NULL;
}

void perf_initialize(void)
{
	pfm_err_t err = pfm_initialize();

	if (err != PFM_SUCCESS) {
		fprintf(stderr, "Unable to initialize perfmon library: %s\n",
				pfm_strerror(err));
		exit(1);
	}
	symbol__elf_init();
}

void perf_finalize(void)
{
	pfm_terminate();
}

/* This is arch-specific and maybe model specific in the future.  For some
 * events, pfm4 gives us a pseudo encoding.  Those codes don't map to real
 * hardware events and are meant to be interpreted by Linux for *other* HW
 * events, e.g. in arch/x86/events/intel/core.c.
 *
 * While we're here, we can also take *real* encodings and treat them like
 * pseudo encodings.  For instance, the arch event 0x3c (unhalted_core_cycles)
 * can also be done with fixed counter 1.  This all assumes we have version 2 or
 * later of Intel's perfmon. */
static void x86_handle_pseudo_encoding(struct perf_eventsel *sel)
{
	uint8_t lower_byte;

	switch (sel->ev.event & 0xffff) {
	case 0xc0:	/* arch inst_retired */
		sel->ev.flags |= PERFMON_FIXED_EVENT;
		PMEV_SET_MASK(sel->ev.event, 0);
		PMEV_SET_EVENT(sel->ev.event, 0);
		return;
	case 0x3c:	/* arch unhalted_core_cycles */
		sel->ev.flags |= PERFMON_FIXED_EVENT;
		PMEV_SET_MASK(sel->ev.event, 0);
		PMEV_SET_EVENT(sel->ev.event, 1);
		return;
	case 0x13c:	/* arch unhalted_reference_cycles */
	case 0x300:	/* pseudo encode: unhalted_reference_cycles */
		sel->ev.flags |= PERFMON_FIXED_EVENT;
		PMEV_SET_MASK(sel->ev.event, 0);
		PMEV_SET_EVENT(sel->ev.event, 2);
		return;
	};
	lower_byte = sel->ev.event & 0xff;
	if ((lower_byte == 0x00) || (lower_byte == 0xff))
		fprintf(stderr, "Unhandled pseudo encoding %d\n", lower_byte);
}

/* Parse the string using pfm's lookup functions.  Returns TRUE on success and
 * fills in parts of sel. */
static bool parse_pfm_encoding(const char *str, struct perf_eventsel *sel)
{
	pfm_pmu_encode_arg_t encode;
	int err;
	char *ptr;

	memset(&encode, 0, sizeof(encode));
	encode.size = sizeof(encode);
	encode.fstr = &ptr;
	err = pfm_get_os_event_encoding(str, PFM_PLM3 | PFM_PLM0, PFM_OS_NONE,
	                                &encode);
	if (err)
		return FALSE;
	strlcpy(sel->fq_str, ptr, MAX_FQSTR_SZ);
	free(ptr);
	if (encode.count == 0) {
		fprintf(stderr, "Found event %s, but it had no codes!\n", sel->fq_str);
		return FALSE;
	}
	sel->ev.event = encode.codes[0];
	x86_handle_pseudo_encoding(sel);
	sel->type = PERF_TYPE_RAW;
	sel->config = (PMEV_GET_MASK(sel->ev.event) << 8) |
	              PMEV_GET_EVENT(sel->ev.event);
	return TRUE;
}

static bool is_end_of_raw(char c)
{
	return (c == ':') || (c == '\0');
}

/* Helper: given a string, if the event is a raw hex code, return its numeric
 * value.  Returns -1 if it does not match a raw code.
 *
 * rNN[N][N][:,\0].  Begins with r, has at least two hexdigits, up to 4, and
 * ends with : , or \0. */
static int extract_raw_code(const char *event)
{
	int i;
	char copy[5] = {0};

	if (event[0] != 'r')
		return -1;
	event++;
	for (i = 0; i < 4; i++) {
		if (isxdigit(event[i]))
			continue;
		if (is_end_of_raw(event[i]))
			break;
		return -1;
	}
	if (!is_end_of_raw(event[i]))
		return -1;
	/* 'i' tracks how many we found (i.e. every 'continue') */
	if (i < 2)
		return -1;
	/* need a null-terminated raw code for strtol. */
	for (int j = 0; j < i; j++)
		copy[j] = event[j];
	return strtol(copy, NULL, 16);
}

/* Takes any modifiers, e.g. u:k:etc, and sets the respective values in sel. */
static void parse_modifiers(const char *str, struct perf_eventsel *sel)
{
	char *dup_str, *tok, *tok_save = 0;

	dup_str = xstrdup(str);
	for (tok = strtok_r(dup_str, ":", &tok_save);
	     tok;
	     tok = strtok_r(NULL, ":", &tok_save)) {

		switch (tok[0]) {
		case 'u':
			PMEV_SET_USR(sel->ev.event, 1);
			break;
		case 'k':
			PMEV_SET_OS(sel->ev.event, 1);
			break;
		case 'e':
			PMEV_SET_EDGE(sel->ev.event, 1);
			break;
		case 'p':
			PMEV_SET_PC(sel->ev.event, 1);
			break;
		case 't':
			PMEV_SET_ANYTH(sel->ev.event, 1);
			break;
		case 'i':
			PMEV_SET_INVCMSK(sel->ev.event, 1);
			break;
		case 'c':
			if (tok[1] != '=') {
				fprintf(stderr, "Bad cmask tok %s, ignoring\n", tok);
				break;
			}
			errno = 0;
			PMEV_SET_CMASK(sel->ev.event, strtoul(&tok[2], NULL, 0));
			if (errno)
				fprintf(stderr, "Bad cmask tok %s, trying anyway\n", tok);
			break;
		}
	}
	free(dup_str);
}

/* Parse the string for a raw encoding.  Returns TRUE on success and fills in
 * parts of sel.  It has basic modifiers, like pfm4, for setting bits in the
 * event code.  This is arch specific, and is all x86 (intel) for now. */
static bool parse_raw_encoding(const char *str, struct perf_eventsel *sel)
{
	int code = extract_raw_code(str);
	char *colon;

	if (code == -1)
		return FALSE;
	sel->ev.event = code;
	strlcpy(sel->fq_str, str, MAX_FQSTR_SZ);
	colon = strchr(str, ':');
	if (colon)
		parse_modifiers(colon + 1, sel);
	/* Note that we do not call x86_handle_pseudo_encoding here.  We'll submit
	 * exactly what the user asked us for - which also means no fixed counters
	 * for them (unless we want a :f: token or something). */
	sel->type = PERF_TYPE_RAW;
	sel->config = (PMEV_GET_MASK(sel->ev.event) << 8) |
	              PMEV_GET_EVENT(sel->ev.event);
	return TRUE;
}

/* Helper, returns true is str is a generic event string, and fills in sel with
 * the type and config. */
static bool generic_str_get_code(const char *str, struct perf_eventsel *sel)
{
	char *colon = strchr(str, ':');
	/* if there was no :, we compare as far as we can.  generic_events.name is a
	 * string literal, so strcmp() is fine. */
	size_t len = colon ? colon - str : SIZE_MAX;

	for (int i = 0; i < COUNT_OF(generic_events); i++) {
		if (!strncmp(generic_events[i].name, str, len)) {
			sel->type = generic_events[i].type;
			sel->config = generic_events[i].config;
			return TRUE;
		}
	}
	return FALSE;
}

/* TODO: this is arch-specific and possibly machine-specific. (intel for now).
 * Basically a lot of our perf is arch-dependent. (e.g. PMEV_*). */
static bool arch_translate_generic(struct perf_eventsel *sel)
{
	switch (sel->type) {
	case PERF_TYPE_HARDWARE:
		/* These are the intel/x86 architectural perf events */
		switch (sel->config) {
		case PERF_COUNT_HW_CPU_CYCLES:
			PMEV_SET_MASK(sel->ev.event, 0x00);
			PMEV_SET_EVENT(sel->ev.event, 0x3c);
			break;
		case PERF_COUNT_HW_INSTRUCTIONS:
			PMEV_SET_MASK(sel->ev.event, 0x00);
			PMEV_SET_EVENT(sel->ev.event, 0xc0);
			break;
		case PERF_COUNT_HW_CACHE_REFERENCES:
			PMEV_SET_MASK(sel->ev.event, 0x4f);
			PMEV_SET_EVENT(sel->ev.event, 0x2e);
			break;
		case PERF_COUNT_HW_CACHE_MISSES:
			PMEV_SET_MASK(sel->ev.event, 0x41);
			PMEV_SET_EVENT(sel->ev.event, 0x2e);
			break;
		case PERF_COUNT_HW_BRANCH_INSTRUCTIONS:
			PMEV_SET_MASK(sel->ev.event, 0x00);
			PMEV_SET_EVENT(sel->ev.event, 0xc4);
			break;
		case PERF_COUNT_HW_BRANCH_MISSES:
			PMEV_SET_MASK(sel->ev.event, 0x00);
			PMEV_SET_EVENT(sel->ev.event, 0xc5);
			break;
		case PERF_COUNT_HW_BUS_CYCLES:
			/* Unhalted reference cycles */
			PMEV_SET_MASK(sel->ev.event, 0x01);
			PMEV_SET_EVENT(sel->ev.event, 0x3c);
			break;
		default:
			return FALSE;
		};
		break;
	default:
		return FALSE;
	};
	/* This will make sure we use fixed counters if available */
	x86_handle_pseudo_encoding(sel);
	return TRUE;
}

/* Parse the string for a built-in encoding.  These are the perf defaults such
 * as 'cycles' or 'cache-references.' Returns TRUE on success and fills in parts
 * of sel. */
static bool parse_generic_encoding(const char *str, struct perf_eventsel *sel)
{
	bool ret = FALSE;
	char *colon;

	if (!generic_str_get_code(str, sel))
		return FALSE;
	switch (sel->type) {
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		ret = arch_translate_generic(sel);
		break;
	};
	if (!ret) {
		fprintf(stderr, "Unsupported built-in event %s\n", str);
		return FALSE;
	}
	strlcpy(sel->fq_str, str, MAX_FQSTR_SZ);
	colon = strchr(str, ':');
	if (colon)
		parse_modifiers(colon + 1, sel);
	return TRUE;
}

/* Given an event description string, fills out sel with the info from the
 * string such that it can be submitted to the OS.
 *
 * The caller can set more bits if they like, such as whether or not to
 * interrupt on overflow, the sample_period, etc.  None of those settings are
 * part of the event string.
 *
 * Kills the program on failure. */
struct perf_eventsel *perf_parse_event(const char *str)
{
	struct perf_eventsel *sel = xzmalloc(sizeof(struct perf_eventsel));

	sel->ev.user_data = (uint64_t)sel;
	if (parse_generic_encoding(str, sel))
		goto success;
	if (parse_pfm_encoding(str, sel))
		goto success;
	if (parse_raw_encoding(str, sel))
		goto success;
	free(sel);
	fprintf(stderr, "Failed to parse event string %s\n", str);
	exit(-1);
success:
	if (!PMEV_GET_OS(sel->ev.event) && !PMEV_GET_USR(sel->ev.event)) {
		PMEV_SET_OS(sel->ev.event, 1);
		PMEV_SET_USR(sel->ev.event, 1);
	}
	PMEV_SET_EN(sel->ev.event, 1);
	return sel;
}

static void perf_get_arch_info(int perf_fd, struct perf_arch_info *pai)
{
	uint8_t cmdbuf[6 * sizeof(uint32_t)];
	const uint8_t *rptr = cmdbuf;

	cmdbuf[0] = PERFMON_CMD_CPU_CAPS;

	xpwrite(perf_fd, cmdbuf, 1, 0);
	xpread(perf_fd, cmdbuf, 6 * sizeof(uint32_t), 0);

	rptr = get_le_u32(rptr, &pai->perfmon_version);
	rptr = get_le_u32(rptr, &pai->proc_arch_events);
	rptr = get_le_u32(rptr, &pai->bits_x_counter);
	rptr = get_le_u32(rptr, &pai->counters_x_proc);
	rptr = get_le_u32(rptr, &pai->bits_x_fix_counter);
	rptr = get_le_u32(rptr, &pai->fix_counters_x_proc);
}

static int perf_open_event(int perf_fd, const struct core_set *cores,
						   const struct perf_eventsel *sel)
{
	uint8_t cmdbuf[1 + 3 * sizeof(uint64_t) + sizeof(uint32_t) +
				   CORE_SET_SIZE];
	uint8_t *wptr = cmdbuf;
	const uint8_t *rptr = cmdbuf;
	uint32_t ped;
	int i, j;

	*wptr++ = PERFMON_CMD_COUNTER_OPEN;
	wptr = put_le_u64(wptr, sel->ev.event);
	wptr = put_le_u64(wptr, sel->ev.flags);
	wptr = put_le_u64(wptr, sel->ev.trigger_count);
	wptr = put_le_u64(wptr, sel->ev.user_data);

	for (i = CORE_SET_SIZE - 1; (i >= 0) && !cores->core_set[i]; i--)
		;
	if (i < 0) {
		fprintf(stderr, "Performance event CPU set must not be empty\n");
		exit(1);
	}
	wptr = put_le_u32(wptr, i + 1);
	for (j = 0; j <= i; j++)
		*wptr++ = cores->core_set[j];

	xpwrite(perf_fd, cmdbuf, wptr - cmdbuf, 0);
	xpread(perf_fd, cmdbuf, sizeof(uint32_t), 0);

	rptr = get_le_u32(rptr, &ped);

	return (int) ped;
}

static uint64_t *perf_get_event_values(int perf_fd, int ped, size_t *pnvalues)
{
	ssize_t rsize;
	uint32_t i, n;
	uint64_t *values;
	uint64_t temp;
	size_t bufsize = sizeof(uint32_t) + MAX_NUM_CORES * sizeof(uint64_t);
	uint8_t *cmdbuf = xmalloc(bufsize);
	uint8_t *wptr = cmdbuf;
	const uint8_t *rptr = cmdbuf;

	*wptr++ = PERFMON_CMD_COUNTER_STATUS;
	wptr = put_le_u32(wptr, ped);

	xpwrite(perf_fd, cmdbuf, wptr - cmdbuf, 0);
	rsize = pread(perf_fd, cmdbuf, bufsize, 0);

	if (rsize < (sizeof(uint32_t))) {
		fprintf(stderr, "Invalid read size while fetching event status: %ld\n",
				rsize);
		exit(1);
	}
	rptr = get_le_u32(rptr, &n);
	if (((rptr - cmdbuf) + n * sizeof(uint64_t)) > rsize) {
		fprintf(stderr, "Invalid read size while fetching event status: %ld\n",
				rsize);
		exit(1);
	}
	values = xmalloc(n * sizeof(uint64_t));
	for (i = 0; i < n; i++)
		rptr = get_le_u64(rptr, values + i);
	free(cmdbuf);

	*pnvalues = n;

	return values;
}

/* Helper, returns the total count (across all cores) of the event @idx */
uint64_t perf_get_event_count(struct perf_context *pctx, unsigned int idx)
{
	uint64_t total = 0;
	size_t nvalues;
	uint64_t *values;

	values = perf_get_event_values(pctx->perf_fd, pctx->events[idx].ped,
	                               &nvalues);
	for (int i = 0; i < nvalues; i++)
		total += values[i];
	free(values);
	return total;
}

static void perf_close_event(int perf_fd, int ped)
{
	uint8_t cmdbuf[1 + sizeof(uint32_t)];
	uint8_t *wptr = cmdbuf;

	*wptr++ = PERFMON_CMD_COUNTER_CLOSE;
	wptr = put_le_u32(wptr, ped);

	xpwrite(perf_fd, cmdbuf, wptr - cmdbuf, 0);
}

struct perf_context *perf_create_context(struct perf_context_config *cfg)
{
	struct perf_context *pctx = xzmalloc(sizeof(struct perf_context));

	pctx->cfg = cfg;
	pctx->perf_fd = xopen(cfg->perf_file, O_RDWR, 0);
	/* perf record needs kpctl_fd, but other perf subcommands might not.  We'll
	 * delay the opening of kpctl until we need it, since kprof is picky about
	 * multiple users of kpctl. */
	pctx->kpctl_fd = -1;
	perf_get_arch_info(pctx->perf_fd, &pctx->pai);

	return pctx;
}

void perf_free_context(struct perf_context *pctx)
{
	if (pctx->kpctl_fd != -1)
		close(pctx->kpctl_fd);	/* disabled sampling */
	close(pctx->perf_fd);	/* closes all events */
	free(pctx);
}

void perf_context_event_submit(struct perf_context *pctx,
							   const struct core_set *cores,
							   const struct perf_eventsel *sel)
{
	struct perf_event *pevt = pctx->events + pctx->event_count;

	if (pctx->event_count >= COUNT_OF(pctx->events)) {
		fprintf(stderr, "Too many open events: %d\n", pctx->event_count);
		exit(1);
	}
	pctx->event_count++;
	pevt->cores = *cores;
	pevt->sel = *sel;
	pevt->ped = perf_open_event(pctx->perf_fd, cores, sel);
	if (pevt->ped < 0) {
		fprintf(stderr, "Unable to submit event \"%s\": %s\n", sel->fq_str,
		        errstr());
		exit(1);
	}
}

void perf_stop_events(struct perf_context *pctx)
{
	for (int i = 0; i < pctx->event_count; i++)
		perf_close_event(pctx->perf_fd, pctx->events[i].ped);
}

static void ensure_kpctl_is_open(struct perf_context *pctx)
{
	if (pctx->kpctl_fd == -1)
		pctx->kpctl_fd = xopen(pctx->cfg->kpctl_file, O_RDWR, 0);
}

void perf_start_sampling(struct perf_context *pctx)
{
	static const char * const enable_str = "start";

	ensure_kpctl_is_open(pctx);
	xwrite(pctx->kpctl_fd, enable_str, strlen(enable_str));
}

void perf_stop_sampling(struct perf_context *pctx)
{
	static const char * const disable_str = "stop";

	ensure_kpctl_is_open(pctx);
	xwrite(pctx->kpctl_fd, disable_str, strlen(disable_str));
}

void perf_context_show_events(struct perf_context *pctx, FILE *file)
{
	struct perf_eventsel *sel;

	for (int i = 0; i < pctx->event_count; i++) {
		sel = &pctx->events[i].sel;
		fprintf(file, "Event: %s, final code %p%s, trigger count %d\n",
		        sel->fq_str, sel->ev.event,
		        perfmon_is_fixed_event(&sel->ev) ? " (fixed)" : "",
		        sel->ev.trigger_count);
	}
}

static void perf_print_event_flags(const pfm_event_info_t *info, FILE *file)
{
	int n = 0;

	if (info->is_precise) {
		fputs("[precise] ", file);
		n++;
	}
	if (!n)
		fputs("None", file);
}

static void perf_print_attr_flags(const pfm_event_attr_info_t *info, FILE *file)
{
	int n = 0;

	if (info->is_dfl) {
		fputs("[default] ", file);
		n++;
	}
	if (info->is_precise) {
		fputs("[precise] ", file);
		n++;
	}
	if (!n)
		fputs("None ", file);
}

/* Ported from libpfm4 */
static void perf_show_event_info(const pfm_event_info_t *info,
								 const pfm_pmu_info_t *pinfo, FILE *file)
{
	static const char * const srcs[PFM_ATTR_CTRL_MAX] = {
		[PFM_ATTR_CTRL_UNKNOWN] = "???",
		[PFM_ATTR_CTRL_PMU] = "PMU",
		[PFM_ATTR_CTRL_PERF_EVENT] = "perf_event",
	};
	pfm_event_attr_info_t ainfo;
	int i, mod = 0, um = 0;

	fprintf(file, "#-----------------------------\n"
			"IDX      : %d\n"
			"PMU name : %s (%s)\n"
			"Name     : %s\n"
			"Equiv    : %s\n",
			info->idx, pinfo->name, pinfo->desc,
			info->name, info->equiv ? info->equiv : "None");

	fprintf(file, "Flags    : ");
	perf_print_event_flags(info, file);
	fputc('\n', file);

	fprintf(file, "Desc     : %s\n", info->desc ? info->desc :
			"no description available");
	fprintf(file, "Code     : 0x%"PRIx64"\n", info->code);

	ZERO_DATA(ainfo);
	ainfo.size = sizeof(ainfo);

	pfm_for_each_event_attr(i, info) {
		const char *src;
		pfm_err_t err = pfm_get_event_attr_info(info->idx, i, PFM_OS_NONE,
												&ainfo);

		if (err != PFM_SUCCESS) {
			fprintf(stderr, "Failed to get attribute info: %s\n",
					pfm_strerror(err));
			exit(1);
		}

		if (ainfo.ctrl >= PFM_ATTR_CTRL_MAX) {
			fprintf(stderr, "event: %s has unsupported attribute source %d",
					info->name, ainfo.ctrl);
			ainfo.ctrl = PFM_ATTR_CTRL_UNKNOWN;
		}
		src = srcs[ainfo.ctrl];
		switch (ainfo.type) {
			case PFM_ATTR_UMASK:
				fprintf(file, "Umask-%02u : 0x%02"PRIx64" : %s : [%s] : ",
						um, ainfo.code, src, ainfo.name);
				perf_print_attr_flags(&ainfo, file);
				fputc(':', file);
				if (ainfo.equiv)
					fprintf(file, " Alias to %s", ainfo.equiv);
				else
					fprintf(file, " %s", ainfo.desc);
				fputc('\n', file);
				um++;
				break;
			case PFM_ATTR_MOD_BOOL:
				fprintf(file, "Modif-%02u : 0x%02"PRIx64" : %s : [%s] : "
						"%s (boolean)\n", mod, ainfo.code, src, ainfo.name,
						ainfo.desc);
				mod++;
				break;
			case PFM_ATTR_MOD_INTEGER:
				fprintf(file, "Modif-%02u : 0x%02"PRIx64" : %s : [%s] : "
						"%s (integer)\n", mod, ainfo.code, src, ainfo.name,
						ainfo.desc);
				mod++;
				break;
			default:
				fprintf(file, "Attr-%02u  : 0x%02"PRIx64" : %s : [%s] : %s\n",
						i, ainfo.code, ainfo.name, src, ainfo.desc);
		}
	}
}

void perf_show_events(const char *rx, FILE *file)
{
	int pmu;
	regex_t crx;
	pfm_pmu_info_t pinfo;
	pfm_event_info_t info;
	char fullname[256];

	if (rx && regcomp(&crx, rx, REG_ICASE)) {
		fprintf(stderr, "Failed to compile event regex: '%s'\n", rx);
		exit(1);
	}

    ZERO_DATA(pinfo);
    pinfo.size = sizeof(pinfo);
    ZERO_DATA(info);
    info.size = sizeof(info);

	pfm_for_all_pmus(pmu) {
		pfm_err_t err = pfm_get_pmu_info(pmu, &pinfo);

		if (err != PFM_SUCCESS || !pinfo.is_present)
			continue;

		for (int i = pinfo.first_event; i != -1; i = pfm_get_event_next(i)) {
			err = pfm_get_event_info(i, PFM_OS_NONE, &info);
			if (err != PFM_SUCCESS) {
				fprintf(stderr, "Failed to get event info: %s\n",
						pfm_strerror(err));
				exit(1);
			}
			snprintf(fullname, sizeof(fullname), "%s::%s", pinfo.name,
					 info.name);
			if (!rx || regexec(&crx, fullname, 0, NULL, 0) == 0)
				perf_show_event_info(&info, &pinfo, file);
		}
	}
	if (rx)
		regfree(&crx);
}

void perf_convert_trace_data(struct perfconv_context *cctx, const char *input,
							 FILE *outfile)
{
	FILE *infile;
	size_t ksize;

	infile = xfopen(input, "rb");
	if (xfsize(infile) > 0) {
		perfconv_add_kernel_mmap(cctx);
		perfconv_add_kernel_buildid(cctx);
		perfconv_process_input(cctx, infile, outfile);
	}
	fclose(infile);
}
