/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

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
#include <perfmon/err.h>
#include <perfmon/pfmlib.h>
#include "xlib.h"
#include "perfconv.h"
#include "akaros.h"
#include "perf_core.h"

struct event_coords {
	char *buffer;
	const char *event;
	const char *umask;
};

static const uint32_t invalid_mask = (uint32_t) -1;

static void perf_parse_event_coords(const char *name, struct event_coords *ec)
{
	const char *cptr = strchr(name, ':');

	if (cptr == NULL) {
		ec->buffer = NULL;
		ec->event = name;
		ec->umask = NULL;
	} else {
		size_t cpos = cptr - name;

		ec->buffer = xstrdup(name);
		ec->event = ec->buffer;
		ec->umask = ec->buffer + cpos + 1;
		ec->buffer[cpos] = 0;
	}
}

static void perf_free_event_coords(struct event_coords *ec)
{
	free(ec->buffer);
}

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

static int perf_resolve_event_name(const char *name, uint32_t *event,
								   uint32_t *mask, uint32_t mask_hint)
{
	int idx;
	struct event_coords ec;

	perf_parse_event_coords(name, &ec);

	idx = pfm_find_event(ec.event);
	if (idx >= 0) {
		int i;
		pfm_err_t err;
		pfm_event_info_t einfo;
		pfm_event_attr_info_t ainfo;

		ZERO_DATA(einfo);
		einfo.size = sizeof(einfo);
		err = pfm_get_event_info(idx, PFM_OS_NONE, &einfo);
		if (err != PFM_SUCCESS) {
			fprintf(stderr, "Unable to retrieve event (%s) info: %s\n",
					name, pfm_strerror(err));
			exit(1);
		}

		*event = (uint32_t) einfo.code;
		*mask = invalid_mask;

		ZERO_DATA(ainfo);
		ainfo.size = sizeof(ainfo);
		pfm_for_each_event_attr(i, &einfo) {
			err = pfm_get_event_attr_info(idx, i, PFM_OS_NONE, &ainfo);
			if (err != PFM_SUCCESS) {
				fprintf(stderr, "Failed to get attribute info: %s\n",
						pfm_strerror(err));
				exit(1);
			}
			if (ainfo.type == PFM_ATTR_UMASK) {
				if (mask_hint != invalid_mask) {
					if (mask_hint == (uint32_t) ainfo.code) {
						*mask = (uint32_t) ainfo.code;
						break;
					}
				} else if (!ec.umask) {
					*mask = (uint32_t) ainfo.code;
					if (ainfo.is_dfl)
						break;
				} else if (!strcmp(ec.umask, ainfo.name)) {
					*mask = (uint32_t) ainfo.code;
					break;
				}
			}
		}
	}
	perf_free_event_coords(&ec);

	return idx;
}

static int perf_find_event_by_id(uint32_t event, uint32_t mask)
{
	int pmu;
	pfm_pmu_info_t pinfo;
	pfm_event_info_t info;

    ZERO_DATA(pinfo);
    pinfo.size = sizeof(pinfo);
    ZERO_DATA(info);
    info.size = sizeof(info);

	pfm_for_all_pmus(pmu) {
		pfm_err_t err = pfm_get_pmu_info(pmu, &pinfo);

		if (err != PFM_SUCCESS || !pinfo.is_present)
			continue;

		for (int i = pinfo.first_event; i != -1; i = pfm_get_event_next(i)) {
			uint32_t cevent, cmask;

			err = pfm_get_event_info(i, PFM_OS_NONE, &info);
			if (err != PFM_SUCCESS) {
				fprintf(stderr, "Failed to get event info: %s\n",
						pfm_strerror(err));
				exit(1);
			}
			if (perf_resolve_event_name(info.name, &cevent, &cmask, mask) != i)
				continue;
			if ((cevent == event) && (cmask == mask))
				return i;
		}
	}

	return -1;
}

void perf_initialize(int argc, char *argv[])
{
	pfm_err_t err = pfm_initialize();

	if (err != PFM_SUCCESS) {
		fprintf(stderr, "Unable to initialize perfmon library: %s\n",
				pfm_strerror(err));
		exit(1);
	}
}

void perf_finalize(void)
{
	pfm_terminate();
}

void perf_parse_event(const char *str, struct perf_eventsel *sel)
{
	static const char *const event_spec =
		"{EVENT_ID:MASK,EVENT_NAME:MASK_NAME}[,os[={0,1}]][,usr[={0,1}]]"
		"[,int[={0,1}]][,invcmsk[={0,1}]][,cmask=MASK][,icount=COUNT]";
	char *dstr = xstrdup(str), *sptr, *tok, *ev;

	tok = strtok_r(dstr, ",", &sptr);
	if (tok == NULL) {
		fprintf(stderr, "Invalid event spec string: '%s'\n\t%s\n", str,
				event_spec);
		exit(1);
	}
	ZERO_DATA(*sel);
	sel->eidx = -1;
	sel->ev.flags = 0;
	sel->ev.event = 0;
	PMEV_SET_OS(sel->ev.event, 1);
	PMEV_SET_USR(sel->ev.event, 1);
	PMEV_SET_EN(sel->ev.event, 1);
	if (isdigit(*tok)) {
		ev = strchr(tok, ':');
		if (ev == NULL) {
			fprintf(stderr, "Invalid event spec string: '%s'\n"
					"\tShould be: %s\n", str, event_spec);
			exit(1);
		}
		*ev++ = 0;
		PMEV_SET_EVENT(sel->ev.event, (uint8_t) strtoul(tok, NULL, 0));
		PMEV_SET_MASK(sel->ev.event, (uint8_t) strtoul(ev, NULL, 0));
	} else {
		uint32_t event, mask;

		sel->eidx = perf_resolve_event_name(tok, &event, &mask, invalid_mask);
		if (sel->eidx < 0) {
			fprintf(stderr, "Unable to find event: %s\n", tok);
			exit(1);
		}
		PMEV_SET_EVENT(sel->ev.event, (uint8_t) event);
		PMEV_SET_MASK(sel->ev.event, (uint8_t) mask);
	}
	while ((tok = strtok_r(NULL, ",", &sptr)) != NULL) {
		ev = strchr(tok, '=');
		if (ev)
			*ev++ = 0;
		if (!strcmp(tok, "os")) {
			PMEV_SET_OS(sel->ev.event, (ev == NULL || atoi(ev) != 0) ? 1 : 0);
		} else if (!strcmp(tok, "usr")) {
			PMEV_SET_USR(sel->ev.event, (ev == NULL || atoi(ev) != 0) ? 1 : 0);
		} else if (!strcmp(tok, "int")) {
			PMEV_SET_INTEN(sel->ev.event,
						   (ev == NULL || atoi(ev) != 0) ? 1 : 0);
		} else if (!strcmp(tok, "invcmsk")) {
			PMEV_SET_INVCMSK(sel->ev.event,
							 (ev == NULL || atoi(ev) != 0) ? 1 : 0);
		} else if (!strcmp(tok, "cmask")) {
			if (ev == NULL) {
				fprintf(stderr, "Invalid event spec string: '%s'\n"
						"\tShould be: %s\n", str, event_spec);
				exit(1);
			}
			PMEV_SET_CMASK(sel->ev.event, (uint32_t) strtoul(ev, NULL, 0));
		} else if (!strcmp(tok, "icount")) {
			if (ev == NULL) {
				fprintf(stderr, "Invalid event spec string: '%s'\n"
						"\tShould be: %s\n", str, event_spec);
				exit(1);
			}
			sel->ev.trigger_count = (uint64_t) strtoul(ev, NULL, 0);
		}
	}
	if (PMEV_GET_INTEN(sel->ev.event) && !sel->ev.trigger_count) {
		fprintf(stderr,
				"Counter trigger count for interrupt is too small: %lu\n",
				sel->ev.trigger_count);
		exit(1);
	}
	free(dstr);
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

static uint64_t *perf_get_event_values(int perf_fd, int ped,
									   struct perf_eventsel *sel,
									   size_t *pnvalues)
{
	ssize_t rsize;
	uint32_t i, n;
	uint64_t *values;
	size_t bufsize = 3 * sizeof(uint64_t) + sizeof(uint32_t) +
		MAX_NUM_CORES * sizeof(uint64_t);
	uint8_t *cmdbuf = xmalloc(bufsize);
	uint8_t *wptr = cmdbuf;
	const uint8_t *rptr = cmdbuf;

	*wptr++ = PERFMON_CMD_COUNTER_STATUS;
	wptr = put_le_u32(wptr, ped);

	xpwrite(perf_fd, cmdbuf, wptr - cmdbuf, 0);
	rsize = pread(perf_fd, cmdbuf, bufsize, 0);

	if (rsize < (3 * sizeof(uint64_t) + sizeof(uint32_t))) {
		fprintf(stderr, "Invalid read size while fetching event status: %ld\n",
				rsize);
		exit(1);
	}

	rptr = get_le_u64(rptr, &sel->ev.event);
	rptr = get_le_u64(rptr, &sel->ev.flags);
	rptr = get_le_u64(rptr, &sel->ev.trigger_count);
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

static void perf_close_event(int perf_fd, int ped)
{
	uint8_t cmdbuf[1 + sizeof(uint32_t)];
	uint8_t *wptr = cmdbuf;

	*wptr++ = PERFMON_CMD_COUNTER_CLOSE;
	wptr = put_le_u32(wptr, ped);

	xpwrite(perf_fd, cmdbuf, wptr - cmdbuf, 0);
}

static void perf_enable_sampling(int kpctl_fd)
{
	static const char * const enable_str = "start";

	xwrite(kpctl_fd, enable_str, strlen(enable_str));
}

static void perf_disable_sampling(int kpctl_fd)
{
	static const char * const disable_str = "stop";

	xwrite(kpctl_fd, disable_str, strlen(disable_str));
}

static void perf_flush_sampling(int kpctl_fd)
{
	static const char * const flush_str = "flush";

	xwrite(kpctl_fd, flush_str, strlen(flush_str));
}

struct perf_context *perf_create_context(const struct perf_context_config *cfg)
{
	struct perf_context *pctx = xzmalloc(sizeof(struct perf_context));

	pctx->perf_fd = xopen(cfg->perf_file, O_RDWR, 0);
	pctx->kpctl_fd = xopen(cfg->kpctl_file, O_RDWR, 0);
	perf_get_arch_info(pctx->perf_fd, &pctx->pai);
	perf_enable_sampling(pctx->kpctl_fd);

	return pctx;
}

void perf_free_context(struct perf_context *pctx)
{
	for (int i = 0; i < pctx->event_count; i++)
		perf_close_event(pctx->perf_fd, pctx->events[i].ped);
	perf_disable_sampling(pctx->kpctl_fd);
	close(pctx->kpctl_fd);
	close(pctx->perf_fd);
	free(pctx);
}

void perf_flush_context_traces(struct perf_context *pctx)
{
	perf_flush_sampling(pctx->kpctl_fd);
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
}

void perf_context_show_values(struct perf_context *pctx, FILE *file)
{
	for (int i = 0; i < pctx->event_count; i++) {
		size_t nvalues;
		struct perf_eventsel sel;
		uint64_t *values = perf_get_event_values(pctx->perf_fd,
												 pctx->events[i].ped, &sel,
												 &nvalues);
		char ename[256];

		perf_get_event_string(&pctx->events[i].sel, ename, sizeof(ename));
		fprintf(file, "Event: %s\n\t", ename);
		for (size_t j = 0; j < nvalues; j++)
			fprintf(file, "%lu ", values[j]);
		fprintf(file, "\n");

		free(values);
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

void perf_get_event_string(const struct perf_eventsel *sel, char *sbuf,
						   size_t size)
{
	pfm_event_info_t einfo;

    ZERO_DATA(einfo);
    einfo.size = sizeof(einfo);
	if ((sel->eidx >= 0) &&
		(pfm_get_event_info(sel->eidx, PFM_OS_NONE, &einfo) == PFM_SUCCESS)) {
		const char *mask_name =
			perf_get_event_mask_name(&einfo, PMEV_GET_MASK(sel->ev.event));

		if (mask_name)
			snprintf(sbuf, size, "%s:%s", einfo.name, mask_name);
		else
			snprintf(sbuf, size, "%s", einfo.name);
	} else {
		snprintf(sbuf, size, "0x%02x:0x%02x",
				 (int) PMEV_GET_EVENT(sel->ev.event),
				 (int) PMEV_GET_MASK(sel->ev.event));
	}
}

void perf_make_eventsel_from_event_mask(struct perf_eventsel *sel,
										uint32_t event, uint32_t mask)
{
	ZERO_DATA(*sel);
	PMEV_SET_EVENT(sel->ev.event, (uint8_t) event);
	PMEV_SET_MASK(sel->ev.event, (uint8_t) mask);
	sel->eidx = perf_find_event_by_id(event, mask);
}

static bool perf_get_kernel_elf_path(char *path, size_t psize, size_t *ksize)
{
	int fd;
	ssize_t rsize = -1;

	fd = open("#version/kernel_path", O_RDONLY);
	if (fd >= 0) {
		rsize = read(fd, path, psize);
		while ((rsize > 0) && (path[rsize - 1] == '\n'))
			rsize--;
		close(fd);

		/* We do not export the real kernel size from the #versions device,
		 * because of cyclic dependency issues. The only reason the size is
		 * needed, is because we generate an MMAP record, which Linux perf
		 * uses to find which ELF should be used to resolve addresses to
		 * symbols. Above the Akaros kernel, hardly other ELF will be loaded,
		 * so the worst it can happen if something above the kernel ELF
		 * proper address gets a hit, is that Linux perf will ask the kernel
		 * ELF to resolve an address, and that will fail.
		 * So here we use a large enough size to cover kernel size expansions
		 * for the next 10 years.
		 */
		*ksize = 128 * 1024 * 1024;
	}

	return rsize > 0;
}

void perf_convert_trace_data(struct perfconv_context *cctx, const char *input,
							 const char *output)
{
	FILE *infile, *outfile;
	size_t ksize;
	char kpath[1024];

	infile = xfopen(input, "rb");
	if (xfsize(infile) > 0) {
		outfile = xfopen(output, "wb");

		if (perf_get_kernel_elf_path(kpath, sizeof(kpath), &ksize))
			perfconv_add_kernel_mmap(kpath, ksize, cctx);
		else
			fprintf(stderr, "Unable to fetch kernel build information!\n"
					"Kernel traces will be missing symbol information.\n");

		perfconv_process_input(cctx, infile, outfile);

		fclose(outfile);
	}
	fclose(infile);
}
