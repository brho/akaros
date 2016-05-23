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

void perfconv_add_kernel_mmap(struct perfconv_context *cctx)
{
	char path[] = "[kernel.kallsyms]";
	struct static_mmap64 *mm;

	mm = xmem_arena_zalloc(&cctx->ma, sizeof(struct static_mmap64));
	mm->pid = -1;				/* Linux HOST_KERNEL_ID == -1 */
	mm->tid = 0;				/* Default thread: swapper */
	mm->header_misc = PERF_RECORD_MISC_KERNEL;
	/* Linux sets addr = 0, size = 0xffffffff9fffffff, off = 0xffffffff81000000
	 * Their mmap record is also called [kernel.kallsyms]_text (I think).  They
	 * also have a _text symbol in kallsyms at ffffffff81000000 (equiv to our
	 * KERN_LOAD_ADDR (which is 0xffffffffc0000000)).  Either way, this seems to
	 * work for us; we'll see.  It's also arch-independent (for now). */
	mm->addr = 0;
	mm->size = 0xffffffffffffffff;
	mm->offset = 0x0;
	mm->path = xmem_arena_strdup(&cctx->ma, path);

	mm->next = cctx->static_mmaps;
	cctx->static_mmaps = mm;
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

static void emit_attr(struct mem_file *amf, struct mem_file *mmf,
                      const struct perf_event_attr *attr, uint64_t id)
{
	struct perf_file_section *psids;
	struct perf_file_section sids;

	mem_file_write(amf, attr, sizeof(*attr), 0);

	sids.offset = mmf->size;
	sids.size = sizeof(uint64_t);

	mem_file_write(mmf, &id, sizeof(uint64_t), 0);

	psids = mem_file_write(amf, &sids, sizeof(sids), MBWR_SOLID);

	mem_file_add_reloc(amf, &psids->offset);
}

/* Given raw_info, which is what the kernel sends as user_data for a particular
 * sample, look up the 'id' for the event/sample.  The 'id' identifies the event
 * stream that the sample is a part of.  There are many samples per event
 * stream, all identified by 'id.'  It doesn't matter what 'id', so long as it
 * is unique.  We happen to use the pointer to the sample's eventsel.
 *
 * If this is the first time we've seen 'raw_info', we'll also add an attribute
 * to the perf ctx.  There is one attr per 'id' / event stream. */
static uint64_t perfconv_get_event_id(struct perfconv_context *cctx,
									  uint64_t raw_info)
{
	struct perf_eventsel *sel = (struct perf_eventsel*)raw_info;
	struct perf_event_attr attr;
	uint64_t raw_event;

	assert(sel);
	if (sel->attr_emitted)
		return raw_info;
	raw_event = sel->ev.event;
	ZERO_DATA(attr);
	attr.size = sizeof(attr);
	attr.mmap = 1;
	attr.comm = 1;
	attr.sample_period = sel->ev.trigger_count;
	/* Closely coupled with struct perf_record_sample */
	attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
	                   PERF_SAMPLE_ADDR | PERF_SAMPLE_ID | PERF_SAMPLE_CPU |
	                   PERF_SAMPLE_CALLCHAIN;
	attr.exclude_guest = 1;	/* we can't trace VMs yet */
	attr.exclude_hv = 1;	/* we aren't tracing our hypervisor, AFAIK */
	attr.exclude_user = !PMEV_GET_USR(raw_event);
	attr.exclude_kernel = !PMEV_GET_OS(raw_event);
	attr.type = sel->type;
	attr.config = sel->config;
	emit_attr(&cctx->attrs, &cctx->misc, &attr, raw_info);
	sel->attr_emitted = TRUE;
	return raw_info;
}

static void emit_static_mmaps(struct perfconv_context *cctx)
{
	struct static_mmap64 *mm;

	for (mm = cctx->static_mmaps; mm; mm = mm->next) {
		size_t size = sizeof(struct perf_record_mmap) + strlen(mm->path) + 1;
		struct perf_record_mmap *xrec = xzmalloc(size);

		xrec->header.type = PERF_RECORD_MMAP;
		xrec->header.misc = mm->header_misc;
		xrec->header.size = size;
		xrec->pid = mm->pid;
		xrec->tid = mm->tid;
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
	size_t size = sizeof(struct perf_record_mmap) +
	              strlen((char*)rec->path) + 1;
	struct perf_record_mmap *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_MMAP;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->pid = xrec->tid = rec->pid;
	xrec->addr = rec->addr;
	xrec->len = rec->size;
	xrec->pgoff = rec->offset;
	strcpy(xrec->filename, (char*)rec->path);

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
	xrec->header.misc = PERF_RECORD_MISC_KERNEL;
	xrec->header.size = size;
	xrec->ip = rec->trace[0];
	/* TODO: We could have pid/tid for kernel tasks during their lifetime.
	 * During syscalls, we could use the pid of the process.  For the kernel
	 * itself, -1 seems to be generic kernel stuff, and tid == 0 is 'swapper'.
	 *
	 * Right now, the kernel doesn't even tell us the pid, so we have no way of
	 * knowing from userspace. */
	xrec->pid = -1;
	xrec->tid = 0;
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
	const char *comm = strrchr((char*)rec->path, '/');

	if (!comm)
		comm = (char*)rec->path;
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

struct perfconv_context *perfconv_create_context(struct perf_context *pctx)
{
	struct perfconv_context *cctx = xzmalloc(sizeof(struct perfconv_context));

	cctx->pctx = pctx;
	xmem_arena_init(&cctx->ma, 0);
	perf_header_init(&cctx->ph);
	headers_init(&cctx->hdrs);
	mem_file_init(&cctx->fhdrs, &cctx->ma);
	mem_file_init(&cctx->misc, &cctx->ma);
	mem_file_init(&cctx->attrs, &cctx->ma);
	mem_file_init(&cctx->data, &cctx->ma);
	mem_file_init(&cctx->event_types, &cctx->ma);

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
