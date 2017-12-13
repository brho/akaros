/* Copyright (c) 2015-2016 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
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
#include <assert.h>
#include "perf_format.h"
#include "xlib.h"
#include "perf_core.h"
#include "perfconv.h"
#include "elf.h"

#define MAX_PERF_RECORD_SIZE (32 * 1024 * 1024)
#define PERF_RECORD_BUFFER_SIZE 1024
#define OFFSET_NORELOC ((uint64_t) -1)
#define MEMFILE_BLOCK_SIZE (64 * 1024)

#define MBWR_SOLID (1 << 0)

char *cmd_line_save;

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

static struct mem_block *mem_block_alloc(size_t size)
{
	struct mem_block *mb = xmalloc(sizeof(struct mem_block) + size);

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

static void mem_file_init(struct mem_file *mf)
{
	ZERO_DATA(*mf);
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
			mb = mem_block_alloc(max(MEMFILE_BLOCK_SIZE, size));
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
	struct mem_file_reloc *rel = xzmalloc(sizeof(struct mem_file_reloc));

	rel->ptr = ptr;
	rel->next = mf->relocs;
	mf->relocs = rel;

	return rel;
}

void perfconv_add_kernel_mmap(struct perfconv_context *cctx)
{
	char path[] = "[kernel.kallsyms]";
	struct static_mmap64 *mm;

	mm = xzmalloc(sizeof(struct static_mmap64));
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
	mm->path = xstrdup(path);

	mm->next = cctx->static_mmaps;
	cctx->static_mmaps = mm;
}

static void headers_init(struct perf_headers *hdrs)
{
	ZERO_DATA(*hdrs);
}

/* Prepends a header (the mem_block) to the list of memblocks for the given
 * HEADER type.  They will all be concatted together during finalization. */
static void headers_add_header(struct perf_header *ph,
                               struct perf_headers *hdrs, size_t nhdr,
							   struct mem_block *mb)
{
	always_assert(nhdr < HEADER_FEAT_BITS);

	mb->next = hdrs->headers[nhdr];
	hdrs->headers[nhdr] = mb;
	set_bitno(ph->adds_features, nhdr);
}

/* Emits the headers contents to the mem_file */
static void headers_finalize(struct perf_headers *hdrs, struct mem_file *mf)
{
	struct perf_file_section *header_file_secs, *file_sec;
	struct perf_file_section *file_sec_reloc;
	size_t nr_hdrs = 0;
	size_t hdr_off;
	struct mem_block *mb;
	size_t mb_sz;

	/* For each header, we need a perf_file_section.  These header file sections
	 * are right after the main perf header, and they point to actual header. */
	for (int i = 0; i < HEADER_FEAT_BITS; i++)
		if (hdrs->headers[i])
			nr_hdrs++;
	if (!nr_hdrs)
		return;
	header_file_secs = xmalloc(sizeof(struct perf_file_section) * nr_hdrs);

	hdr_off = sizeof(struct perf_file_section) * nr_hdrs;
	file_sec = header_file_secs;

	/* Spit out the perf_file_sections first and track relocations for all of
	 * the offsets. */
	for (int i = 0; i < HEADER_FEAT_BITS; i++) {
		mb = hdrs->headers[i];
		if (!mb)
			continue;
		mb_sz = mb->wptr - mb->base;
		/* headers[i] is a chain of memblocks */
		while (mb->next) {
			mb = mb->next;
			mb_sz += mb->wptr - mb->base;
		}
		file_sec->size = mb_sz;
		file_sec->offset = hdr_off;		/* offset rel to this memfile */
		/* When we sync the memfile, we'll need to relocate each of the offsets
		 * so that they are relative to the final file.   mem_file_write()
		 * should be returning the location of where it wrote our file section
		 * within the memfile.  that's the offset that we'll reloc later. */
		file_sec_reloc = mem_file_write(mf, file_sec,
		                                sizeof(struct perf_file_section), 0);
		assert(file_sec->size == file_sec_reloc->size);
		assert(file_sec->offset == file_sec_reloc->offset);
		mem_file_add_reloc(mf, &file_sec_reloc->offset);

		hdr_off += mb_sz;
		file_sec++;
	}
	free(header_file_secs);

	/* Spit out the actual headers */
	for (int i = 0; i < HEADER_FEAT_BITS; i++) {
		mb = hdrs->headers[i];
		while (mb) {
			mem_file_write(mf, mb->base, mb->wptr - mb->base, 0);
			mb = mb->next;
		}
	}
}

/* Builds a struct perf_header_string from str and returns it in a mem_block */
static struct mem_block *header_make_string(const char *str)
{
	struct perf_header_string *hdr;
	struct mem_block *mb;
	size_t str_sz = strlen(str) + 1;
	size_t hdr_sz = ROUNDUP(str_sz + sizeof(struct perf_header_string),
	                        PERF_STRING_ALIGN);

	mb = mem_block_alloc(hdr_sz);
	/* Manually writing to the block to avoid another alloc.  I guess I could do
	 * two writes (len and string) and try to not screw it up, but that'd be a
	 * mess. */
	hdr = (struct perf_header_string*)mb->wptr;
	mb->wptr += hdr_sz;
	hdr->len = str_sz;
	memcpy(hdr->string, str, str_sz);
	return mb;
}

/* Opens and reads filename, returning the contents.  Free the ret. */
static char *get_str_from_os(const char *filename)
{
	int fd, ret;
	struct stat fd_stat;
	char *buf;
	size_t buf_sz;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return 0;
	ret = fstat(fd, &fd_stat);
	if (ret) {
		close(fd);
		return 0;
	}
	buf_sz = fd_stat.st_size + 1;
	buf = xmalloc(buf_sz);
	ret = read(fd, buf, buf_sz - 1);
	if (ret <= 0) {
		free(buf);
		close(fd);
		return 0;
	}
	close(fd);
	/* OS strings should be null terminated, but let's be defensive */
	buf[ret] = 0;
	return buf;
}

static void hdr_do_osrelease(struct perf_header *ph, struct perf_headers *hdrs)
{
	char *str;

	str = get_str_from_os("#version/version_name");
	if (!str)
		return;
	headers_add_header(ph, hdrs, HEADER_OSRELEASE, header_make_string(str));
	free(str);
}

static void hdr_do_nrcpus(struct perf_header *ph, struct perf_headers *hdrs)
{
	char *str;
	uint32_t nr_cores;
	struct mem_block *mb;
	struct nr_cpus *hdr;

	str = get_str_from_os("#vars/num_cores!dw");
	if (!str)
		return;
	nr_cores = atoi(str);
	free(str);

	mb = mem_block_alloc(sizeof(struct nr_cpus));
	hdr = (struct nr_cpus*)mb->wptr;
	mb->wptr += sizeof(struct nr_cpus);

	hdr->nr_cpus_online = nr_cores;
	hdr->nr_cpus_available = nr_cores;

	headers_add_header(ph, hdrs, HEADER_NRCPUS, mb);
}

/* Unfortunately, HEADER_CMDLINE doesn't just take a string.  It takes a list of
 * null-terminated perf_header_strings (i.e. argv), with the a u32 for the
 * number of strings.  We can just send one string for the entire cmd line. */
static void hdr_do_cmdline(struct perf_header *ph, struct perf_headers *hdrs)
{
	struct perf_header_string *hdr;
	struct mem_block *mb;
	size_t str_sz = strlen(cmd_line_save) + 1;
	size_t hdr_sz = sizeof(uint32_t) +
	                ROUNDUP(str_sz + sizeof(struct perf_header_string),
	                        PERF_STRING_ALIGN);

	mb = mem_block_alloc(hdr_sz);
	/* emit the nr strings (1) */
	*(uint32_t*)mb->wptr = 1;
	mb->wptr += sizeof(uint32_t);
	/* emit the perf_header_string, as usual */
	hdr = (struct perf_header_string*)mb->wptr;
	mb->wptr += hdr_sz - sizeof(uint32_t);
	hdr->len = str_sz;
	memcpy(hdr->string, cmd_line_save, str_sz);

	headers_add_header(ph, hdrs, HEADER_CMDLINE, mb);
}

/* Returns TRUE if we already emitted a build-id for path.  If this gets too
 * slow (which we'll know because of perf!) we can use a hash table (don't hash
 * on the first few bytes btw - they are usually either /bin or /lib). */
static bool lookup_buildid(struct perfconv_context *cctx, const char *path)
{
	struct build_id_event *b_evt;
	struct mem_block *mb;
	size_t b_evt_path_sz;

	mb = cctx->hdrs.headers[HEADER_BUILD_ID];
	while (mb) {
		b_evt = (struct build_id_event*)mb->base;
		b_evt_path_sz = b_evt->header.size -
		                offsetof(struct build_id_event, filename);
		/* ignoring the last byte since we forced it to be \0 earlier. */
		if (!strncmp(b_evt->filename, path, b_evt_path_sz - 1))
			return TRUE;
		mb = mb->next;
	}
	return FALSE;
}

/* Helper: given a path, allocs and inits a build_id_event within a mem_block,
 * returning both via parameters.  Caller needs to set header.misc and fill in
 * the actual build_id. */
static void build_id_alloc(const char *path, struct build_id_event **b_evt_p,
                           struct mem_block **mb_p)
{
	struct build_id_event *b_evt;
	struct mem_block *mb;
	size_t path_sz, b_evt_sz;

	path_sz = strlen(path) + 1;
	b_evt_sz = path_sz + sizeof(struct build_id_event);

	mb = mem_block_alloc(b_evt_sz);
	b_evt = (struct build_id_event*)mb->wptr;
	mb->wptr += b_evt_sz;

	b_evt->header.type = 0;	/* if this fails, try 67 (synthetic build id) */
	/* header.size filled in by the caller, depending on the type */
	b_evt->header.size = b_evt_sz;
	strlcpy(b_evt->filename, path, path_sz);

	*b_evt_p = b_evt;
	*mb_p = mb;
}

/* Add a build-id header.  Unlike many of the other headers, this one is built
 * on the fly as we emit other records. */
static void hdr_add_buildid(struct perfconv_context *cctx, const char *path,
                            int pid)
{
	struct build_id_event *b_evt;
	struct mem_block *mb;
	int ret;

	if (lookup_buildid(cctx, path))
		return;

	build_id_alloc(path, &b_evt, &mb);
	b_evt->header.misc = PERF_RECORD_MISC_USER;
	b_evt->pid = pid;
	ret = filename__read_build_id(path, b_evt->build_id, BUILD_ID_SIZE);
	if (ret <= 0)
		free(mb);
	else
		headers_add_header(&cctx->ph, &cctx->hdrs, HEADER_BUILD_ID, mb);
}

static void convert_str_to_binary(char *b_id_str, uint8_t *b_id_raw)
{
	char *c = b_id_str;

	for (int i = 0; i < BUILD_ID_SIZE; i++) {
		b_id_raw[i] = nibble_to_num(*c) << 4 | nibble_to_num(*(c + 1));
		c += 2;
	}
}

void perfconv_add_kernel_buildid(struct perfconv_context *cctx)
{
	struct build_id_event *b_evt;
	struct mem_block *mb;
	int ret, fd;
	char build_id[BUILD_ID_SIZE * 2 + 1] = {0};

	build_id_alloc("[kernel.kallsyms]", &b_evt, &mb);
	b_evt->header.misc = PERF_RECORD_MISC_KERNEL;
	b_evt->pid = -1;
	fd = xopen("#version/build_id", O_RDONLY, 0);
	ret = read(fd, build_id, sizeof(build_id));
	if (ret <= 0) {
		free(mb);
	} else {
		convert_str_to_binary(build_id, b_evt->build_id);
		headers_add_header(&cctx->ph, &cctx->hdrs, HEADER_BUILD_ID, mb);
	}
}

/* Helper: adds all the headers, marking them in PH and storing them in
 * feat_hdrs. */
static void headers_build(struct perf_header *ph, struct perf_headers *hdrs,
                          struct mem_file *feat_hdrs)
{
	hdr_do_osrelease(ph, hdrs);
	hdr_do_nrcpus(ph, hdrs);
	hdr_do_cmdline(ph, hdrs);

	headers_finalize(hdrs, feat_hdrs);
}

static void perf_header_init(struct perf_header *ph)
{
	ZERO_DATA(*ph);
	ph->magic = PERF_MAGIC2;
	ph->size = sizeof(*ph);
	ph->attr_size = sizeof(struct perf_event_attr);
}

/* For each attr we emit, we push out the attr, then a perf_file_section for the
 * id(s) for that attr.  This wasn't mentioned in
 * https://lwn.net/Articles/644919/, but it's what Linux perf expects
 * (util/header.c).  It looks like you can have multiple IDs per event attr.
 * We'll only emit one.  The *contents* of the perf_file_section for the ids
 * aren't in the attr perf_file_section, they are in a separate one
 * (attr_id_mf).
 *
 * Note that *attr_mf*'s relocs are relative to the base of *attr_id_mf*, which
 * we'll need to sort out later. */
static void emit_attr(struct mem_file *attr_mf, struct mem_file *attr_id_mf,
                      const struct perf_event_attr *attr, uint64_t id)
{
	struct perf_file_section *psids;
	struct perf_file_section sids;

	mem_file_write(attr_mf, attr, sizeof(*attr), 0);

	sids.offset = attr_id_mf->size;
	sids.size = sizeof(uint64_t);
	mem_file_write(attr_id_mf, &id, sizeof(uint64_t), 0);

	psids = mem_file_write(attr_mf, &sids, sizeof(sids), MBWR_SOLID);
	mem_file_add_reloc(attr_mf, &psids->offset);
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
	                   PERF_SAMPLE_ADDR | PERF_SAMPLE_IDENTIFIER |
	                   PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN;
	attr.exclude_guest = 1;	/* we can't trace VMs yet */
	attr.exclude_hv = 1;	/* we aren't tracing our hypervisor, AFAIK */
	attr.exclude_user = !PMEV_GET_USR(raw_event);
	attr.exclude_kernel = !PMEV_GET_OS(raw_event);
	attr.type = sel->type;
	attr.config = sel->config;
	emit_attr(&cctx->attrs, &cctx->attr_ids, &attr, raw_info);
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

	hdr_add_buildid(cctx, (char*)rec->path, rec->pid);

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
	/* TODO: -1 means "not a process".  We could track ktasks with IDs, emit
	 * COMM events for them (probably!) and report them as the tid.  For now,
	 * tid of 0 means [swapper] to Linux. */
	if (rec->pid == -1) {
		xrec->pid = -1;
		xrec->tid = 0;
	} else {
		xrec->pid = rec->pid;
		xrec->tid = rec->pid;
	}
	xrec->time = rec->tstamp;
	xrec->addr = rec->trace[0];
	xrec->identifier = perfconv_get_event_id(cctx, rec->info);
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
	xrec->identifier = perfconv_get_event_id(cctx, rec->info);
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
	const char *comm;

	hdr_add_buildid(cctx, (char*)rec->path, rec->pid);

	comm = strrchr((char*)rec->path, '/');
	if (!comm)
		comm = (char*)rec->path;
	else
		comm++;
	emit_comm(rec->pid, comm, cctx);
}

struct perfconv_context *perfconv_create_context(struct perf_context *pctx)
{
	struct perfconv_context *cctx = xzmalloc(sizeof(struct perfconv_context));

	cctx->pctx = pctx;
	perf_header_init(&cctx->ph);
	headers_init(&cctx->hdrs);
	mem_file_init(&cctx->fhdrs);
	mem_file_init(&cctx->attr_ids);
	mem_file_init(&cctx->attrs);
	mem_file_init(&cctx->data);
	/* event_types is ignored in newer versions of perf */
	mem_file_init(&cctx->event_types);

	return cctx;
}

void perfconv_free_context(struct perfconv_context *cctx)
{
	if (cctx)
		free(cctx);
}

void perfconv_process_input(struct perfconv_context *cctx, FILE *input,
							FILE *output)
{
	size_t processed_records = 0;
	uint64_t offset;
	uint64_t attr_ids_off = sizeof(cctx->ph);
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

	/* Add all of the headers before outputting ph */
	headers_build(&cctx->ph, &cctx->hdrs, &cctx->fhdrs);

	/* attrs, events, and data will come after attr_ids. */
	offset = sizeof(cctx->ph) + cctx->attr_ids.size;

	/* These are the perf_file_sections in the main perf header.  We need this
	 * sorted out before we emit the PH. */
	cctx->ph.event_types.offset = offset;
	cctx->ph.event_types.size = cctx->event_types.size;
	offset += cctx->event_types.size;

	cctx->ph.attrs.offset = offset;
	cctx->ph.attrs.size = cctx->attrs.size;
	offset += cctx->attrs.size;

	cctx->ph.data.offset = offset;
	cctx->ph.data.size = cctx->data.size;
	offset += cctx->data.size;

	xfwrite(&cctx->ph, sizeof(cctx->ph), output);

	/* attr_ids comes right after the cctx->ph.  We need to put it before
	 * attrs, since attrs needs to know the offset of the base of attrs_ids for
	 * its relocs. */
	assert(ftell(output) == attr_ids_off);
	mem_file_sync(&cctx->attr_ids, output, OFFSET_NORELOC);
	mem_file_sync(&cctx->event_types, output, OFFSET_NORELOC);
	/* reloc is based off *attr_ids* base */
	mem_file_sync(&cctx->attrs, output, attr_ids_off);
	/* Keep data last, so we can append the feature headers.*/
	mem_file_sync(&cctx->data, output, OFFSET_NORELOC);
	/* The feature headers must be right after the data section.  I didn't see
	 * anything in the ABI about this, but Linux's perf has this line:
	 *
	 *		ph->feat_offset  = header->data.offset + header->data.size;
	 */
	mem_file_sync(&cctx->fhdrs, output,
	              cctx->ph.data.offset + cctx->ph.data.size);

	dbg_print(cctx, 2, stderr, "Conversion succeeded: %lu records converted\n",
			  processed_records);
}
