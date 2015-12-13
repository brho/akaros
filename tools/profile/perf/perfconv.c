/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Converts kprof profiler files into Linux perf ones. The Linux Perf file
 * format has bee illustrated here:
 *
 *	 https://lwn.net/Articles/644919/
 *	 https://openlab-mu-internal.web.cern.ch/openlab-mu-internal/03_Documents/
 *			 3_Technical_Documents/Technical_Reports/2011/Urs_Fassler_report.pdf
 *
 */

#include <ros/common.h>
#include <ros/memops.h>
#include <ros/profiler_records.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include "perf_format.h"
#include "xlib.h"
#include "perf_core.h"
#include "perfconv.h"

#define MAX_PERF_RECORD_SIZE (32 * 1024 * 1024)
#define PERF_RECORD_BUFFER_SIZE 1024
#define OFFSET_NORELOC ((uint64_t) -1)
#define MEMFILE_BLOCK_SIZE (64 * 1024)

#define MBWR_SOLID (1 << 0)

struct perf_record {
	uint64_t type;
	uint64_t size;
	char *data;
	char buffer[PERF_RECORD_BUFFER_SIZE];
};

static void dbg_print(struct perfconv_context *cctx, int level, FILE *file,
					  const char *fmt, ...)
{
	if (cctx->debug_level >= level) {
		va_list args;

		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
	}
}

void perfconv_set_dbglevel(int level, struct perfconv_context *cctx)
{
	cctx->debug_level = level;
}

static void free_record(struct perf_record *pr)
{
	if (pr->data != pr->buffer)
		free(pr->data);
	pr->data = NULL;
}

static int read_record(FILE *file, struct perf_record *pr)
{
	if (vb_fdecode_uint64(file, &pr->type) == EOF ||
		vb_fdecode_uint64(file, &pr->size) == EOF)
		return EOF;
	if (pr->size > MAX_PERF_RECORD_SIZE) {
		fprintf(stderr, "Invalid record size: type=%lu size=%lu\n", pr->type,
				pr->size);
		exit(1);
	}
	if (pr->size > sizeof(pr->buffer))
		pr->data = xmalloc((size_t) pr->size);
	else
		pr->data = pr->buffer;
	if (fread(pr->data, 1, (size_t) pr->size, file) != (size_t) pr->size) {
		fprintf(stderr, "Unable to read record memory: size=%lu\n",
				pr->size);
		return EOF;
	}

	return 0;
}

static struct mem_block *mem_block_alloc(struct mem_file *mf, size_t size)
{
	struct mem_block *mb = xmem_arena_alloc(mf->ma,
											sizeof(struct mem_block) + size);

	mb->next = NULL;
	mb->base = mb->wptr = (char *) mb + sizeof(struct mem_block);
	mb->top = mb->base + size;

	return mb;
}

static char *mem_block_write(struct mem_block *mb, const void *data,
							 size_t size)
{
	char *wrbase = mb->wptr;

	always_assert(size <= mb->top - mb->wptr);

	memcpy(mb->wptr, data, size);
	mb->wptr += size;

	return wrbase;
}

static void mem_file_init(struct mem_file *mf, struct mem_arena *ma)
{
	ZERO_DATA(*mf);
	mf->ma = ma;
}

static int mem_block_can_write(struct mem_block *mb, size_t size, int flags)
{
	size_t space = mb->top - mb->wptr;

	return (flags & MBWR_SOLID) ? (space >= size) : (space > 0);
}

static void *mem_file_write(struct mem_file *mf, const void *data, size_t size,
							int flags)
{
	void *wrbase = NULL;

	while (size > 0) {
		size_t space, csize;
		struct mem_block *mb = mf->tail;

		if (!mb || !mem_block_can_write(mb, size, flags)) {
			mb = mem_block_alloc(mf, max(MEMFILE_BLOCK_SIZE, size));
			if (!mf->tail)
				mf->head = mb;
			else
				mf->tail->next = mb;
			mf->tail = mb;
		}
		space = mb->top - mb->wptr;
		csize = min(size, space);

		wrbase = mem_block_write(mb, data, csize);
		mf->size += csize;

		size -= csize;
		data = (const char *) data + csize;
	}

	return wrbase;
}

static void mem_file_sync(struct mem_file *mf, FILE *file, uint64_t rel_offset)
{
	struct mem_block *mb;

	if (rel_offset != 0) {
		struct mem_file_reloc *rel;

		always_assert(!mf->relocs || rel_offset != OFFSET_NORELOC);

		for (rel = mf->relocs; rel; rel = rel->next)
			*rel->ptr += rel_offset;
	}

	for (mb = mf->head; mb; mb = mb->next)
		xfwrite(mb->base, mb->wptr - mb->base, file);
}

static struct mem_file_reloc *mem_file_add_reloc(struct mem_file *mf,
												 uint64_t *ptr)
{
	struct mem_file_reloc *rel =
		xmem_arena_zalloc(mf->ma, sizeof(struct mem_file_reloc));

	rel->ptr = ptr;
	rel->next = mf->relocs;
	mf->relocs = rel;

	return rel;
}

static uint64_t perfconv_make_config_id(bool is_raw, uint64_t type,
										uint64_t event_id)
{
	uint64_t config = is_raw ? (uint64_t) 1 << 63 : 0;

	return config | (type << 56) | (event_id & (((uint64_t) 1 << 56) - 1));
}

static uint64_t perfconv_get_config_type(uint64_t config)
{
	return (config >> 56) & 0x7f;
}

static void add_static_mmap(const char *path, uint64_t addr, uint64_t offset,
							uint64_t size, uint32_t pid,
							struct perfconv_context *cctx)
{
	struct static_mmap64 *mm;
	struct stat stb;

	if (size == 0) {
		if (stat(path, &stb)) {
			fprintf(stderr, "Unable to stat mmapped file '%s': %s\n",
					path, strerror(errno));
			exit(1);
		}
		size = (uint64_t) stb.st_size;
	}

	mm = xmem_arena_zalloc(&cctx->ma, sizeof(struct static_mmap64));
	mm->pid = pid;
	mm->addr = addr;
	mm->size = size;
	mm->offset = offset;
	mm->path = xmem_arena_strdup(&cctx->ma, path);

	mm->next = cctx->static_mmaps;
	cctx->static_mmaps = mm;
}

void perfconv_add_kernel_mmap(const char *path, size_t ksize,
							  struct perfconv_context *cctx)
{
	add_static_mmap(path, cctx->kernel_load_address, cctx->kernel_load_address,
					(uint64_t) ksize, 0, cctx);
}

static void headers_init(struct perf_headers *hdrs)
{
	ZERO_DATA(*hdrs);
}

static void headers_add_header(struct perf_headers *hdrs, size_t nhdr,
							   struct mem_block *mb)
{
	always_assert(nhdr < HEADER_FEAT_BITS);

	hdrs->headers[nhdr] = mb;
}

static void headers_write(struct perf_headers *hdrs, struct perf_header *ph,
						  struct mem_file *mf)
{
	size_t i;

	for (i = 0; i < HEADER_FEAT_BITS; i++) {
		struct mem_block *mb = hdrs->headers[i];

		if (mb) {
			mem_file_write(mf, mb->base, mb->wptr - mb->base, 0);
			set_bitno(ph->adds_features, i);
		}
	}
}

static void perf_header_init(struct perf_header *ph)
{
	ZERO_DATA(*ph);
	ph->magic = PERF_MAGIC2;
	ph->size = sizeof(*ph);
	ph->attr_size = sizeof(struct perf_event_attr);
}

static void add_attribute(struct mem_file *amf, struct mem_file *mmf,
						  const struct perf_event_attr *attr,
						  const uint64_t *ids, size_t nids)
{
	struct perf_file_section *psids;
	struct perf_file_section sids;

	mem_file_write(amf, attr, sizeof(*attr), 0);

	sids.offset = mmf->size;
	sids.size = nids * sizeof(uint64_t);

	mem_file_write(mmf, ids, nids * sizeof(uint64_t), 0);

	psids = mem_file_write(amf, &sids, sizeof(sids), MBWR_SOLID);

	mem_file_add_reloc(amf, &psids->offset);
}

static void add_default_attribute(struct mem_file *amf, struct mem_file *mmf,
								  uint64_t config, uint64_t id)
{
	struct perf_event_attr attr;

	ZERO_DATA(attr);
	attr.type = (uint32_t) perfconv_get_config_type(config);
	attr.size = sizeof(attr);
	attr.config = config;
	attr.mmap = 1;
	attr.comm = 1;
	attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
		PERF_SAMPLE_ADDR | PERF_SAMPLE_ID | PERF_SAMPLE_CPU |
		PERF_SAMPLE_CALLCHAIN;

	add_attribute(amf, mmf, &attr, &id, 1);
}

static uint64_t perfconv_get_event_id(struct perfconv_context *cctx,
									  uint64_t info)
{
	size_t i, j, n, ipos;
	uint64_t id, config;
	struct perf_event_id *nevents;

	for (;;) {
		ipos = (size_t) (info % cctx->alloced_events);
		for (i = cctx->alloced_events; (i > 0) &&
				 (cctx->events[ipos].event != 0);
			 i--, ipos = (ipos + 1) % cctx->alloced_events) {
			if (cctx->events[ipos].event == info)
				return cctx->events[ipos].id;
		}
		if (i != 0)
			break;
		/* Need to resize the hash ...
		 */
		n = 2 * cctx->alloced_events;
		nevents = xmem_arena_zalloc(&cctx->ma, n * sizeof(*cctx->events));
		for (i = 0; i < cctx->alloced_events; i++) {
			if (cctx->events[i].event == 0)
				continue;
			j = cctx->events[i].event % n;
			for (; nevents[j].event; j = (j + 1) % n)
				;
			nevents[j].event = cctx->events[i].event;
			nevents[j].id = cctx->events[i].id;
		}
		cctx->alloced_events = n;
		cctx->events = nevents;
	}

	cctx->events[ipos].event = info;
	cctx->events[ipos].id = id = cctx->sqnr_id;
	cctx->sqnr_id++;

	switch ((int) PROF_INFO_DOM(info)) {
	case PROF_DOM_PMU:
		config = perfconv_make_config_id(TRUE, PERF_TYPE_RAW,
										 PROF_INFO_DATA(info));
		break;
	case PROF_DOM_TIMER:
	default:
		config = perfconv_make_config_id(FALSE, PERF_TYPE_HARDWARE,
										 PERF_COUNT_HW_CPU_CYCLES);
	}

	add_default_attribute(&cctx->attrs, &cctx->misc, config, id);

	return id;
}

static void emit_static_mmaps(struct perfconv_context *cctx)
{
	struct static_mmap64 *mm;

	for (mm = cctx->static_mmaps; mm; mm = mm->next) {
		size_t size = sizeof(struct perf_record_mmap) + strlen(mm->path) + 1;
		struct perf_record_mmap *xrec = xzmalloc(size);

		xrec->header.type = PERF_RECORD_MMAP;
		xrec->header.misc = PERF_RECORD_MISC_USER;
		xrec->header.size = size;
		xrec->pid = xrec->tid = mm->pid;
		xrec->addr = mm->addr;
		xrec->len = mm->size;
		xrec->pgoff = mm->offset;
		strcpy(xrec->filename, mm->path);

		mem_file_write(&cctx->data, xrec, size, 0);

		free(xrec);
	}
}

static void emit_comm(uint32_t pid, const char *comm,
					  struct perfconv_context *cctx)
{
	size_t size = sizeof(struct perf_record_comm) + strlen(comm) + 1;
	struct perf_record_comm *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_COMM;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->pid = xrec->tid = pid;
	strcpy(xrec->comm, comm);

	mem_file_write(&cctx->data, xrec, size, 0);

	free(xrec);
}

static void emit_pid_mmap64(struct perf_record *pr,
							struct perfconv_context *cctx)
{
	struct proftype_pid_mmap64 *rec = (struct proftype_pid_mmap64 *) pr->data;
	size_t size = sizeof(struct perf_record_mmap) + strlen(rec->path) + 1;
	struct perf_record_mmap *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_MMAP;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->pid = xrec->tid = rec->pid;
	xrec->addr = rec->addr;
	xrec->len = rec->size;
	xrec->pgoff = rec->offset;
	strcpy(xrec->filename, rec->path);

	mem_file_write(&cctx->data, xrec, size, 0);

	free(xrec);
}

static void emit_kernel_trace64(struct perf_record *pr,
								struct perfconv_context *cctx)
{
	struct proftype_kern_trace64 *rec = (struct proftype_kern_trace64 *)
		pr->data;
	size_t size = sizeof(struct perf_record_sample) +
		(rec->num_traces - 1) * sizeof(uint64_t);
	struct perf_record_sample *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_SAMPLE;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->ip = rec->trace[0];
	xrec->time = rec->tstamp;
	xrec->addr = rec->trace[0];
	xrec->id = perfconv_get_event_id(cctx, rec->info);
	xrec->cpu = rec->cpu;
	xrec->nr = rec->num_traces - 1;
	memcpy(xrec->ips, rec->trace + 1, (rec->num_traces - 1) * sizeof(uint64_t));

	mem_file_write(&cctx->data, xrec, size, 0);

	free(xrec);
}

static void emit_user_trace64(struct perf_record *pr,
							  struct perfconv_context *cctx)
{
	struct proftype_user_trace64 *rec = (struct proftype_user_trace64 *)
		pr->data;
	size_t size = sizeof(struct perf_record_sample) +
		(rec->num_traces - 1) * sizeof(uint64_t);
	struct perf_record_sample *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_SAMPLE;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->ip = rec->trace[0];
	xrec->pid = xrec->tid = rec->pid;
	xrec->time = rec->tstamp;
	xrec->addr = rec->trace[0];
	xrec->id = perfconv_get_event_id(cctx, rec->info);
	xrec->cpu = rec->cpu;
	xrec->nr = rec->num_traces - 1;
	memcpy(xrec->ips, rec->trace + 1, (rec->num_traces - 1) * sizeof(uint64_t));

	mem_file_write(&cctx->data, xrec, size, 0);

	free(xrec);
}

static void emit_new_process(struct perf_record *pr,
							 struct perfconv_context *cctx)
{
	struct proftype_new_process *rec = (struct proftype_new_process *) pr->data;
	const char *comm = strrchr(rec->path, '/');

	if (!comm)
		comm = rec->path;
	else
		comm++;
	emit_comm(rec->pid, comm, cctx);
}

static void add_event_type(struct mem_file *mf, uint64_t id, const char *name)
{
	struct perf_trace_event_type evt;

	ZERO_DATA(evt);
	evt.event_id = id;
	strncpy(evt.name, name, sizeof(evt.name));
	evt.name[sizeof(evt.name) - 1] = 0;

	mem_file_write(mf, &evt, sizeof(evt), 0);
}

struct perfconv_context *perfconv_create_context(void)
{
	struct perfconv_context *cctx = xzmalloc(sizeof(struct perfconv_context));

	xmem_arena_init(&cctx->ma, 0);
	cctx->kernel_load_address = 0xffffffffc0000000;
	cctx->alloced_events = 128;
	cctx->events = xmem_arena_zalloc(
		&cctx->ma, cctx->alloced_events * sizeof(*cctx->events));
	perf_header_init(&cctx->ph);
	headers_init(&cctx->hdrs);
	mem_file_init(&cctx->fhdrs, &cctx->ma);
	mem_file_init(&cctx->misc, &cctx->ma);
	mem_file_init(&cctx->attrs, &cctx->ma);
	mem_file_init(&cctx->data, &cctx->ma);
	mem_file_init(&cctx->event_types, &cctx->ma);

	emit_comm(0, "[kernel]", cctx);

	return cctx;
}

void perfconv_free_context(struct perfconv_context *cctx)
{
	if (cctx) {
		xmem_arena_destroy(&cctx->ma);
		free(cctx);
	}
}

void perfconv_process_input(struct perfconv_context *cctx, FILE *input,
							FILE *output)
{
	size_t processed_records = 0;
	uint64_t offset, rel_offset;
	struct perf_record pr;

	emit_static_mmaps(cctx);

	while (read_record(input, &pr) == 0) {
		dbg_print(cctx, 8, stderr, "Valid record: type=%lu size=%lu\n",
				  pr.type, pr.size);

		processed_records++;

		switch (pr.type) {
		case PROFTYPE_KERN_TRACE64:
			emit_kernel_trace64(&pr, cctx);
			break;
		case PROFTYPE_USER_TRACE64:
			emit_user_trace64(&pr, cctx);
			break;
		case PROFTYPE_PID_MMAP64:
			emit_pid_mmap64(&pr, cctx);
			break;
		case PROFTYPE_NEW_PROCESS:
			emit_new_process(&pr, cctx);
			break;
		default:
			fprintf(stderr, "Unknown record: type=%lu size=%lu\n", pr.type,
					pr.size);
			processed_records--;
		}

		free_record(&pr);
	}

	headers_write(&cctx->hdrs, &cctx->ph, &cctx->fhdrs);
	offset = sizeof(cctx->ph) + cctx->fhdrs.size + cctx->misc.size;

	if (cctx->event_types.size > 0) {
		cctx->ph.event_types.offset = offset;
		cctx->ph.event_types.size = cctx->event_types.size;
		offset += cctx->event_types.size;
	}
	if (cctx->attrs.size > 0) {
		cctx->ph.attrs.offset = offset;
		cctx->ph.attrs.size = cctx->attrs.size;
		offset += cctx->attrs.size;
	}
	if (cctx->data.size > 0) {
		cctx->ph.data.offset = offset;
		cctx->ph.data.size = cctx->data.size;
		offset += cctx->data.size;
	}

	xfwrite(&cctx->ph, sizeof(cctx->ph), output);
	mem_file_sync(&cctx->fhdrs, output, OFFSET_NORELOC);

	rel_offset = (uint64_t) ftell(output);
	mem_file_sync(&cctx->misc, output, rel_offset);

	mem_file_sync(&cctx->event_types, output, rel_offset);
	mem_file_sync(&cctx->attrs, output, rel_offset);
	mem_file_sync(&cctx->data, output, rel_offset);

	dbg_print(cctx, 2, stderr, "Conversion succeeded: %lu records converted\n",
			  processed_records);
}
