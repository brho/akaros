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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ros/profiler_records.h>
#include "perf_format.h"

#define MAX_PERF_RECORD_SIZE (32 * 1024 * 1024)
#define PERF_RECORD_BUFFER_SIZE 1024
#define OFFSET_NORELOC ((uint64_t) -1)

#define ID_PERF_SAMPLE 0

#define MBWR_SOLID (1 << 0)

#define min(a, b)								\
	({ __typeof__(a) _a = (a);					\
		__typeof__(b) _b = (b);					\
		_a < _b ? _a : _b; })
#define max(a, b)								\
	({ __typeof__(a) _a = (a);					\
		__typeof__(b) _b = (b);					\
		_a > _b ? _a : _b; })
#define always_assert(c)												\
	do {																\
		if (!(c))														\
			fprintf(stderr, "%s: %d: Assertion failed: " #c "\n",		\
					__FILE__, __LINE__);								\
	} while (0)

struct perf_record {
	uint64_t type;
	uint64_t size;
	char *data;
	char buffer[PERF_RECORD_BUFFER_SIZE];
};

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

struct perf_headers {
	struct mem_block *headers[HEADER_FEAT_BITS];
};

struct static_mmap64 {
	struct static_mmap64 *next;
	uint64_t addr;
	uint64_t size;
	uint64_t offset;
	uint32_t pid;
	char *path;
};

static int debug_level;
static uint64_t kernel_load_address = 0xffffffffc0000000;
static size_t std_block_size = 64 * 1024;
static struct static_mmap64 *static_mmaps;

static inline void set_bitno(void *data, size_t bitno)
{
	((char *) data)[bitno / 8] |= 1 << (bitno % 8);
}

static inline const char* vb_decode_uint64(const char *data, uint64_t *pval)
{
	unsigned int i;
	uint64_t val = 0;

	for (i = 0; (*data & 0x80) != 0; i += 7, data++)
		val |= (((uint64_t) *data) & 0x7f) << i;
	*pval = val | ((uint64_t) *data) << i;

	return data + 1;
}

static inline int vb_fdecode_uint64(FILE *file, uint64_t *pval)
{
	unsigned int i = 0;
	uint64_t val = 0;

	for (;;) {
		int c = fgetc(file);

		if (c == EOF)
			return EOF;
		val |= (((uint64_t) c) & 0x7f) << i;
		i += 7;
		if ((c & 0x80) == 0)
			break;
	}
	*pval = val;

	return i / 7;
}

static void dbg_print(int level, FILE *file, const char *fmt, ...)
{
	if (debug_level >= level) {
		va_list args;

		va_start(args, fmt);
		vfprintf(file, fmt, args);
		va_end(args);
	}
}

static void *xmalloc(size_t size)
{
	void *data = malloc(size);

	if (!data) {
		fprintf(stderr, "Unable to allocate %lu bytes: %s\n", size,
				strerror(errno));
		exit(1);
	}

	return data;
}

static void *xzmalloc(size_t size)
{
	void *data = xmalloc(size);

	memset(data, 0, size);

	return data;
}

static FILE *xfopen(const char *path, const char *mode)
{
	FILE *file = fopen(path, mode);

	if (!file) {
		fprintf(stderr, "Unable to open file '%s' for mode '%s': %s\n",
				path, mode, strerror(errno));
		exit(1);
	}

	return file;
}

static void xfwrite(const void *data, size_t size, FILE *file)
{
	if (fwrite(data, 1, size, file) != size) {
		fprintf(stderr, "Unable to write %lu bytes: %s\n", size,
				strerror(errno));
		exit(1);
	}
}

static void xfseek(FILE *file, long offset, int whence)
{
	if (fseek(file, offset, whence)) {
		int error = errno;

		fprintf(stderr, "Unable to seek at offset %ld from %s (fpos=%ld): %s\n",
				offset, whence == SEEK_SET ? "beginning of file" :
				(whence == SEEK_END ? "end of file" : "current position"),
				ftell(file), strerror(error));
		exit(1);
	}
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
	memset(mf, 0, sizeof(*mf));
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
			mb = mem_block_alloc(max(std_block_size, size));
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

static void add_static_mmap(const char *path, uint64_t addr, uint64_t offset,
							uint32_t pid)
{
	struct static_mmap64 *mm;
	struct stat stb;

	if (stat(path, &stb)) {
		fprintf(stderr, "Unable to stat mmapped file '%s': %s\n",
				path, strerror(errno));
		exit(1);
	}

	mm = xzmalloc(sizeof(struct static_mmap64));
	mm->pid = pid;
	mm->addr = addr;
	mm->size = stb.st_size;
	mm->offset = offset;
	mm->path = strdup(path);

	mm->next = static_mmaps;
	static_mmaps = mm;
}

static void add_kernel_mmap(const char *path)
{
	add_static_mmap(path, kernel_load_address, kernel_load_address, 0);
}

static void headers_init(struct perf_headers *hdrs)
{
	memset(hdrs, 0, sizeof(*hdrs));
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
	memset(ph, 0, sizeof(*ph));
	ph->magic = PERF_MAGIC2;
	ph->size = sizeof(*ph);
	ph->attr_size = sizeof(struct perf_event_attr);
}

static void emit_static_mmaps(struct mem_file *mf)
{
	struct static_mmap64 *mm;

	for (mm = static_mmaps; mm; mm = mm->next) {
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

		mem_file_write(mf, xrec, size, 0);

		free(xrec);
	}
}

static void emit_comm(uint32_t pid, const char *comm, struct mem_file *mf)
{
	size_t size = sizeof(struct perf_record_comm) + strlen(comm) + 1;
	struct perf_record_comm *xrec = xzmalloc(size);

	xrec->header.type = PERF_RECORD_COMM;
	xrec->header.misc = PERF_RECORD_MISC_USER;
	xrec->header.size = size;
	xrec->pid = xrec->tid = pid;
	strcpy(xrec->comm, comm);

	mem_file_write(mf, xrec, size, 0);

	free(xrec);
}

static void emit_pid_mmap64(struct perf_record *pr, struct mem_file *mf)
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

	mem_file_write(mf, xrec, size, 0);

	free(xrec);
}

static void emit_kernel_trace64(struct perf_record *pr, struct mem_file *mf)
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
	xrec->id = ID_PERF_SAMPLE;
	xrec->cpu = rec->cpu;
	xrec->nr = rec->num_traces - 1;
	memcpy(xrec->ips, rec->trace + 1, (rec->num_traces - 1) * sizeof(uint64_t));

	mem_file_write(mf, xrec, size, 0);

	free(xrec);
}

static void emit_user_trace64(struct perf_record *pr, struct mem_file *mf)
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
	xrec->id = ID_PERF_SAMPLE;
	xrec->cpu = rec->cpu;
	xrec->nr = rec->num_traces - 1;
	memcpy(xrec->ips, rec->trace + 1, (rec->num_traces - 1) * sizeof(uint64_t));

	mem_file_write(mf, xrec, size, 0);

	free(xrec);
}

static void emit_new_process(struct perf_record *pr, struct mem_file *mf)
{
	struct proftype_new_process *rec = (struct proftype_new_process *) pr->data;
	const char *comm = strrchr(rec->path, '/');

	if (!comm)
		comm = rec->path;
	else
		comm++;
	emit_comm(rec->pid, comm, mf);
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
								  uint64_t event_id, uint64_t id)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HARDWARE;
	attr.size = sizeof(attr);
	attr.config = event_id;
	attr.mmap = 1;
	attr.comm = 1;
	attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
		PERF_SAMPLE_ADDR | PERF_SAMPLE_ID | PERF_SAMPLE_CPU |
		PERF_SAMPLE_CALLCHAIN;

	add_attribute(amf, mmf, &attr, &id, 1);
}

static void add_event_type(struct mem_file *mf, uint64_t id, const char *name)
{
	struct perf_trace_event_type evt;

	memset(&evt, 0, sizeof(evt));
	evt.event_id = id;
	strncpy(evt.name, name, sizeof(evt.name));
	evt.name[sizeof(evt.name) - 1] = 0;

	mem_file_write(mf, &evt, sizeof(evt), 0);
}

static void process_input(FILE *input, FILE *output)
{
	size_t processed_records = 0;
	uint64_t offset, rel_offset;
	struct perf_record pr;
	struct perf_header ph;
	struct perf_headers hdrs;
	struct mem_file fhdrs, misc, attrs, data, event_types;

	perf_header_init(&ph);
	headers_init(&hdrs);
	mem_file_init(&fhdrs);
	mem_file_init(&misc);
	mem_file_init(&attrs);
	mem_file_init(&data);
	mem_file_init(&event_types);

	add_event_type(&event_types, PERF_COUNT_HW_CPU_CYCLES, "cycles");
	add_default_attribute(&attrs, &misc, PERF_COUNT_HW_CPU_CYCLES,
						  ID_PERF_SAMPLE);

	emit_comm(0, "[kernel]", &data);
	emit_static_mmaps(&data);

	while (read_record(input, &pr) == 0) {
		dbg_print(8, stderr, "Valid record: type=%lu size=%lu\n",
				  pr.type, pr.size);

		processed_records++;

		switch (pr.type) {
		case PROFTYPE_KERN_TRACE64:
			emit_kernel_trace64(&pr, &data);
			break;
		case PROFTYPE_USER_TRACE64:
			emit_user_trace64(&pr, &data);
			break;
		case PROFTYPE_PID_MMAP64:
			emit_pid_mmap64(&pr, &data);
			break;
		case PROFTYPE_NEW_PROCESS:
			emit_new_process(&pr, &data);
			break;
		default:
			fprintf(stderr, "Unknown record: type=%lu size=%lu\n", pr.type,
					pr.size);
			processed_records--;
		}

		free_record(&pr);
	}

	headers_write(&hdrs, &ph, &fhdrs);
	offset = sizeof(ph) + fhdrs.size + misc.size;

	if (event_types.size > 0) {
		ph.event_types.offset = offset;
		ph.event_types.size = event_types.size;
		offset += event_types.size;
	}
	if (attrs.size > 0) {
		ph.attrs.offset = offset;
		ph.attrs.size = attrs.size;
		offset += attrs.size;
	}
	if (data.size > 0) {
		ph.data.offset = offset;
		ph.data.size = data.size;
		offset += data.size;
	}

	xfwrite(&ph, sizeof(ph), output);
	mem_file_sync(&fhdrs, output, OFFSET_NORELOC);

	rel_offset = (uint64_t) ftell(output);
	mem_file_sync(&misc, output, rel_offset);

	mem_file_sync(&event_types, output, rel_offset);
	mem_file_sync(&attrs, output, rel_offset);
	mem_file_sync(&data, output, rel_offset);

	fprintf(stderr, "Conversion succeeded: %lu records converted\n",
			processed_records);
}

static void usage(const char *prg)
{
	fprintf(stderr, "Use: %s [-ioskDh]\n"
			"\t-i INPUT_FILE		  : Sets the input file path (STDIN).\n"
			"\t-o OUTPUT_FILE		  : Sets the output file path (STDOUT).\n"
			"\t-k KERN_ELF_FILE		  : Sets the kernel file path.\n"
			"\t-s SIZE				  : Sets the default memory block size (%lu).\n"
			"\t-D DBG_LEVEL			  : Sets the debug level for messages.\n"
			"\t-h					  : Displays this help screen.\n",
			prg, std_block_size);
	exit(1);
}

int main(int argc, const char **argv)
{
	int i;
	const char *inpath = NULL, *outpath = NULL;
	FILE *input = stdin, *output = stdout;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-i") == 0) {
			if (++i < argc)
				inpath = argv[i];
		} else if (strcmp(argv[i], "-o") == 0) {
			if (++i < argc)
				outpath = argv[i];
		} else if (strcmp(argv[i], "-s") == 0) {
			if (++i < argc)
				std_block_size = atol(argv[i]);
		} else if (strcmp(argv[i], "-k") == 0) {
			if (++i < argc)
				add_kernel_mmap(argv[i]);
		} else if (strcmp(argv[i], "-D") == 0) {
			if (++i < argc)
				debug_level = atoi(argv[i]);
		} else
			usage(argv[0]);
	}
	if (inpath)
		input = xfopen(inpath, "rb");
	if (outpath)
		output = xfopen(outpath, "wb");

	process_input(input, output);

	fflush(output);

	return 0;
}
