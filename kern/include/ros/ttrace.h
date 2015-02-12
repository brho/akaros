/* TODO(gvdl): Who holds the copyright?
 * Godfrey van der Linden <gvdl@google.com>
 * See LICENSE for details.
 *
 * Timed tracing.
 * TODO(gvdl): Documentation goes here. */
#ifndef INC_ROS_TTRACE_H
#define INC_ROS_TTRACE_H

#define TTRACE_DEV        "#T"
#define TTRACE_DEV_CTL    TTRACE_DEV "/ctl"
#define TTRACE_DEV_AUX    TTRACE_DEV "/aux"
#define TTRACE_DEV_CPUFMT TTRACE_DEV "/cpu%03d"

enum {
	TTRACE_ENTRY_MASK      = 0x0000000000000003ULL,  /* two spare bits */
	/* Exact meaning varies depends on TYPE */
	TTRACE_ENTRY_START     = 0x0000000000000000ULL,  /* Start/unblock */
	TTRACE_ENTRY_STOP      = 0x0000000000000001ULL,  /* Stop/block */
	TTRACE_ENTRY_ENTRY     = 0x0000000000000002ULL,  /* Entry */
	TTRACE_ENTRY_EXIT      = 0x0000000000000003ULL,  /* Exit */

	/* Tag mask for the ctx union */
	TTRACE_CTX_MASK        = 0x0000000000000070ULL,  /* one spare bit */
	TTRACE_CTX_KTASK       = 0x0000000000000000ULL,  /* ctx.kthread */
	TTRACE_CTX_RKMSG       = 0x0000000000000010ULL,  /* ctx.kthread */
	TTRACE_CTX_PIDSYSC     = 0x0000000000000020ULL,  /* ctx.pid_syscall */
	TTRACE_CTX_PIDTRAP     = 0x0000000000000030ULL,  /* ctx.pid_syscall */
	TTRACE_CTX_IRQ         = 0x0000000000000040ULL,  /* ctx.ctx_depth */
	TTRACE_CTX_TRAP        = 0x0000000000000050ULL,  /* ctx.ctx_depth */

	/* 12 bits to encode cpu_id */
	TTRACE_TYPE_PCPU_MASK  = 0x00000000000fff00ULL,  /* Not set by kernel */

	/* Block of scheduling tracking trace type bits */
	TTRACE_TYPE_MASK       = 0xfffffffffff00000ULL,
	TTRACE_TYPE_SCHED      = 0x0000000000100000ULL,
	TTRACE_TYPE_SEMA       = 0x0000000000200000ULL,
	TTRACE_TYPE_RWLOCK     = 0x0000000000400000ULL,
	TTRACE_TYPE_SYSCALL    = 0x0000000000800000ULL,
	TTRACE_TYPE_INTR       = 0x0000000001000000ULL,
	TTRACE_TYPE_PROCESS    = 0x0000000002000000ULL,

	/* ttrace_version */
	TTRACEVH_MAGIC         = 0x0001020304050607;
	TTRACEVH_CIGAM         = 0x0706050403020100;
	TTRACEVH_H_V1_0        = 0x00010000,  /* Version 1.0 ttrace/data format */
	TTRACEVH_S_V1_0        = 0x00010000,  /* Version 1.0 ttrace/data format */
	TTRACEVH_C_V1_0        = 0x00010000,  /* Version 1.0 ttrace/cpu* format */

	/* ttrace/shared data tags */
	TTRACES_TAG_CONT       = 0x80000000,  /* Tag continuation, top bit set */
	TTRACES_TAG_INFO       = 0x00000001,  /* Data file specification */
	TTRACES_TAG_SYSC       = 0x00000002,  /* Syscall entry */
	TTRACES_TAG_PROC       = 0x00000003,  /* Proc name */
	TTRACES_TAG_KTASK      = 0x00000004,  /* Ktask name */
	TTRACES_TAG_SEM        = 0x00000005,  /* Semaphore name */
	TTRACES_TAG_CV         = 0x00000006,  /* Condition name */
	TTRACES_TAG_QLOCK      = 0x00000007,  /* Qlock name */
}

// Binary information is always in host endianness, the reader makes right
// assert(((struct ttrace_version *) <hdrp>)->magic == TTRACEVH_MAGIC);
// TODO: Endianness correcting inlines when required

struct ttrace_version {
	uint64_t magic; // Endianness of ttrace binary data, reader makes right
	uint32_t header_version; // Version of this header
	uint32_t shared_data_version; // Version of shared data
	uint32_t percpu_data_version; // Version of percpu entries
	uint32_t num_cpus;
	ptrdiff_t buffer_mask; // Mask for offsets into shared buffer
	ptrdiff_t first_offset; // First valid data in auxilary buffer
	ptrdiff_t last_offset; // Last data entry in auxilary
} __attribute__ ((packed));

struct ttrace_type {
	/* 0x00 */ uint64_t type_id;
	/* 0x08 */ union {
		uintptr_t kthread;
		uintptr_t pid_syscall;
		uint32_t ctx_depth;
	} ctx;
} __attribute__ ((packed));

/* ttrace entry must be a 64byte (that is power of two cachelines) */
struct ttrace_entry {
	/* 0x00 */ uint64_t timestamp;
	/* 0x08 */ struct ttrace_type t;
	/* 0x18 */ uintptr_t data0;
	/* 0x20 */ uintptr_t data1;
	/* 0x28 */ uintptr_t data2;
	/* 0x30 */ uintptr_t data3;
	/* 0x38 */ uintptr_t data4;
} __attribute__ ((packed));

/* Continuations have the same timestamp with 0x1 bit set */
struct ttrace_entry_continuation {
	/* 0x00 */ uint64_t timestamp;
	/* 0x08 */ uintptr_t data0;
	/* 0x10 */ uintptr_t data1;
	/* 0x18 */ uintptr_t data2;
	/* 0x20 */ uintptr_t data3;
	/* 0x28 */ uintptr_t data4;
	/* 0x30 */ uintptr_t data5;
	/* 0x38 */ uintptr_t data6;
} __attribute__ ((packed));

// len in bytes but records are laid down on 64bit boundaries, actual
// length in memory is computed using ttrace_aux_entry_size().
struct ttrace_aux_entry {
	uint64_t path;     // See path decode macros below
	uint64_t timestamp;
	// Payload
	uintptr_t ident;  // Optional: Identifier for record, eg. syscall number
	uint8_t string[]; // String is not nul terminated
} __attribute__ ((packed));

#define TTRACE_AUX_PATH(tag, coreid, len, flags) ({							\
	const uint64_t t = (uint32_t) tag;										\
	const uint64_t c = (uint16_t) coreid;									\
	const uint64_t paylen = (uint16_t) len - 16;							\
	const uint64_t f = flags & 0xf;											\
	(t << 32) | c << 16 | paylen << 4 | f;									\
})
#define TTRACE_AUX_TAG(path)    ((path >> 32))
#define TTRACE_AUX_CPUID(path)  ((path >> 16) & 0xffff)
#define TTRACE_AUX_LEN(path)   (((path >>  4) &  0xfff) + 16)
#define TTRACE_AUX_FLAGS(path)   (path & 0xf)
#define TTRACE_AUX_ENTRY_MAXLEN ((size_t) 4096)
#define TTRACE_AUX_ENTRY_MAXPAYLEN (TTRACE_AUX_ENTRY_MAXLEN  - 16)
#define TTRACE_AUX_ENTRY_MAXSTRLEN \
	(TTRACE_AUX_ENTRY_MAXLEN - sizeof(struct ttrace_aux_entry))

static inline size_t ttrace_aux_align(ptrdiff_t offset)
{
	const size_t align_mask = sizeof(uint64_t) - 1;  // 8 byte alignment
	return ((offset & ~align_mask) + align_mask) & ~align_mask;
}

static inline struct ttrace_aux_entry *ttrace_aux_entry_next(
	   const struct ttrace_aux_entry * const entry)
{
	const size_t len = ttrace_aux_entry_size(entry->len);
	return (struct ttrace_aux_entry *) &((uint8_t*) entry)[len];
}

#endif /* INC_ROS_TTRACE_H */
