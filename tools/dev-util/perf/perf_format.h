/* Copyright (C) 2009-2016, the Linux Perf authors
 * Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Parts of this file come, either directly, or as baseline, from the Linux
 * kernel source file:
 *
 *	 tools/perf/util/header.h
 *
 * Such file is ruled by the general Linux kernel copyright.
 */

#pragma once

#define BITS_PER_LONG (8 * sizeof(long))
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits)				\
	unsigned long name[BITS_TO_LONGS(bits)]

enum {
	HEADER_RESERVED		= 0,	/* Always cleared */
	HEADER_FIRST_FEATURE	= 1,
	HEADER_TRACING_DATA	= 1,
	HEADER_BUILD_ID,

	HEADER_HOSTNAME,
	HEADER_OSRELEASE,
	HEADER_VERSION,
	HEADER_ARCH,
	HEADER_NRCPUS,
	HEADER_CPUDESC,
	HEADER_CPUID,
	HEADER_TOTAL_MEM,
	HEADER_CMDLINE,
	HEADER_EVENT_DESC,
	HEADER_CPU_TOPOLOGY,
	HEADER_NUMA_TOPOLOGY,
	HEADER_BRANCH_STACK,
	HEADER_PMU_MAPPINGS,
	HEADER_GROUP_DESC,
	HEADER_LAST_FEATURE,
	HEADER_FEAT_BITS	= 256,
};

/*
 * Bits that can be set in attr.sample_type to request information
 * in the overflow packets.
 */
enum perf_event_sample_format {
	PERF_SAMPLE_IP				= 1U << 0,
	PERF_SAMPLE_TID				= 1U << 1,
	PERF_SAMPLE_TIME			= 1U << 2,
	PERF_SAMPLE_ADDR			= 1U << 3,
	PERF_SAMPLE_READ			= 1U << 4,
	PERF_SAMPLE_CALLCHAIN			= 1U << 5,
	PERF_SAMPLE_ID				= 1U << 6,
	PERF_SAMPLE_CPU				= 1U << 7,
	PERF_SAMPLE_PERIOD			= 1U << 8,
	PERF_SAMPLE_STREAM_ID			= 1U << 9,
	PERF_SAMPLE_RAW				= 1U << 10,
	PERF_SAMPLE_BRANCH_STACK		= 1U << 11,
	PERF_SAMPLE_REGS_USER			= 1U << 12,
	PERF_SAMPLE_STACK_USER			= 1U << 13,
	PERF_SAMPLE_WEIGHT			= 1U << 14,
	PERF_SAMPLE_DATA_SRC			= 1U << 15,
	PERF_SAMPLE_IDENTIFIER			= 1U << 16,
	PERF_SAMPLE_TRANSACTION			= 1U << 17,
	PERF_SAMPLE_REGS_INTR			= 1U << 18,

	PERF_SAMPLE_MAX = 1U << 19,		/* non-ABI */
};

enum perf_event_type {
	/*
	 * If perf_event_attr.sample_id_all is set then all event types will
	 * have the sample_type selected fields related to where/when
	 * (identity) an event took place (TID, TIME, ID, STREAM_ID, CPU,
	 * IDENTIFIER) described in PERF_RECORD_SAMPLE below, it will be stashed
	 * just after the perf_event_header and the fields already present for
	 * the existing fields, i.e. at the end of the payload. That way a newer
	 * perf.data file will be supported by older perf tools, with these new
	 * optional fields being ignored.
	 *
	 * struct sample_id {
	 *	{ u32			pid, tid; } && PERF_SAMPLE_TID
	 *	{ u64			time;	  } && PERF_SAMPLE_TIME
	 *	{ u64			id;		  } && PERF_SAMPLE_ID
	 *	{ u64			stream_id;} && PERF_SAMPLE_STREAM_ID
	 *	{ u32			cpu, res; } && PERF_SAMPLE_CPU
	 *	{ u64			id;	  } && PERF_SAMPLE_IDENTIFIER
	 * } && perf_event_attr::sample_id_all
	 *
	 * Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.	The
	 * advantage of PERF_SAMPLE_IDENTIFIER is that its position is fixed
	 * relative to header.size.
	 */

	/*
	 * The MMAP events record the PROT_EXEC mappings so that we can
	 * correlate userspace IPs to code. They have the following structure:
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	char				filename[];
	 * };
	 */
	PERF_RECORD_MMAP			= 1,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				id;
	 *	u64				lost;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_LOST			= 2,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	char				comm[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_COMM			= 3,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 *	u32				tid, ptid;
	 *	u64				time;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_EXIT			= 4,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				time;
	 *	u64				id;
	 *	u64				stream_id;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_THROTTLE			= 5,
	PERF_RECORD_UNTHROTTLE			= 6,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 *	u32				tid, ptid;
	 *	u64				time;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_FORK			= 7,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, tid;
	 *
	 *	struct read_format		values;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_READ			= 8,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	#
	 *	# Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.
	 *	# The advantage of PERF_SAMPLE_IDENTIFIER is that its position
	 *	# is fixed relative to header.
	 *	#
	 *
	 *	{ u64		id;	  } && PERF_SAMPLE_IDENTIFIER
	 *	{ u64		ip;	  } && PERF_SAMPLE_IP
	 *	{ u32		pid, tid; } && PERF_SAMPLE_TID
	 *	{ u64		time;	  } && PERF_SAMPLE_TIME
	 *	{ u64		addr;	  } && PERF_SAMPLE_ADDR
	 *	{ u64		id;	  } && PERF_SAMPLE_ID
	 *	{ u64		stream_id;} && PERF_SAMPLE_STREAM_ID
	 *	{ u32		cpu, res; } && PERF_SAMPLE_CPU
	 *	{ u64		period;	  } && PERF_SAMPLE_PERIOD
	 *
	 *	{ struct read_format	values;	  } && PERF_SAMPLE_READ
	 *
	 *	{ u64		nr,
	 *	  u64		ips[nr];  } && PERF_SAMPLE_CALLCHAIN
	 *
	 *	#
	 *	# The RAW record below is opaque data wrt the ABI
	 *	#
	 *	# That is, the ABI doesn't make any promises wrt to
	 *	# the stability of its content, it may vary depending
	 *	# on event, hardware, kernel version and phase of
	 *	# the moon.
	 *	#
	 *	# In other words, PERF_SAMPLE_RAW contents are not an ABI.
	 *	#
	 *
	 *	{ u32		size;
	 *	  char		data[size];}&& PERF_SAMPLE_RAW
	 *
	 *	{ u64		nr;
	 *	{ u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
	 *
	 *	{ u64		abi; # enum perf_sample_regs_abi
	 *	  u64		regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
	 *
	 *	{ u64		size;
	 *	  char		data[size];
	 *	  u64		dyn_size; } && PERF_SAMPLE_STACK_USER
	 *
	 *	{ u64		weight;	  } && PERF_SAMPLE_WEIGHT
	 *	{ u64		data_src; } && PERF_SAMPLE_DATA_SRC
	 *	{ u64		transaction; } && PERF_SAMPLE_TRANSACTION
	 *	{ u64		abi; # enum perf_sample_regs_abi
	 *	  u64		regs[weight(mask)]; } && PERF_SAMPLE_REGS_INTR
	 * };
	 */
	PERF_RECORD_SAMPLE			= 9,

	/*
	 * The MMAP2 records are an augmented version of MMAP, they add
	 * maj, min, ino numbers to be used to uniquely identify each mapping
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	u32				maj;
	 *	u32				min;
	 *	u64				ino;
	 *	u64				ino_generation;
	 *	char				filename[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_MMAP2			= 10,

	PERF_RECORD_MAX,			/* non-ABI */
};

#define PERF_MAX_STACK_DEPTH		127

enum perf_callchain_context {
	PERF_CONTEXT_HV			= (__uint64_t) -32,
	PERF_CONTEXT_KERNEL		= (__uint64_t) -128,
	PERF_CONTEXT_USER		= (__uint64_t) -512,

	PERF_CONTEXT_GUEST		= (__uint64_t) -2048,
	PERF_CONTEXT_GUEST_KERNEL	= (__uint64_t) -2176,
	PERF_CONTEXT_GUEST_USER		= (__uint64_t) -2560,

	PERF_CONTEXT_MAX		= (__uint64_t) -4095,
};

/*
 * attr.type
 */
enum perf_type_id {
	PERF_TYPE_HARDWARE		= 0,
	PERF_TYPE_SOFTWARE		= 1,
	PERF_TYPE_TRACEPOINT		= 2,
	PERF_TYPE_HW_CACHE		= 3,
	PERF_TYPE_RAW			= 4,
	PERF_TYPE_BREAKPOINT		= 5,
	PERF_TYPE_INTEL_CQM		= 6,

	PERF_TYPE_MAX,			/* non-ABI */
};

/*
 * Generalized performance event event_id types, used by the
 * attr.event_id parameter of the sys_perf_event_open()
 * syscall:
 */
enum perf_hw_id {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_HW_CPU_CYCLES		= 0,
	PERF_COUNT_HW_INSTRUCTIONS		= 1,
	PERF_COUNT_HW_CACHE_REFERENCES		= 2,
	PERF_COUNT_HW_CACHE_MISSES		= 3,
	PERF_COUNT_HW_BRANCH_INSTRUCTIONS	= 4,
	PERF_COUNT_HW_BRANCH_MISSES		= 5,
	PERF_COUNT_HW_BUS_CYCLES		= 6,
	PERF_COUNT_HW_STALLED_CYCLES_FRONTEND	= 7,
	PERF_COUNT_HW_STALLED_CYCLES_BACKEND	= 8,
	PERF_COUNT_HW_REF_CPU_CYCLES		= 9,

	PERF_COUNT_HW_MAX,			/* non-ABI */
};

/* We can output a bunch of different versions of perf_event_attr.  The oldest
 * Linux perf I've run across expects version 3 and can't handle anything
 * larger.  Since we're not using anything from versions 1 or higher, we can sit
 * at version 0 for now. */
#define PERF_ATTR_VER0

#ifdef PERF_ATTR_VER1
	#define __PERF_ATTR_VER1 1
#endif
#ifdef PERF_ATTR_VER2
	#define __PERF_ATTR_VER1 1
	#define __PERF_ATTR_VER2 1
#endif
#ifdef PERF_ATTR_VER3
	#define __PERF_ATTR_VER1 1
	#define __PERF_ATTR_VER2 1
	#define __PERF_ATTR_VER3 1
#endif
#ifdef PERF_ATTR_VER4
	#define __PERF_ATTR_VER1 1
	#define __PERF_ATTR_VER2 1
	#define __PERF_ATTR_VER3 1
	#define __PERF_ATTR_VER4 1
#endif

/*
 * Hardware event_id to monitor via a performance monitoring event:
 */
struct perf_event_attr {
	/*
	 * Major type: hardware/software/tracepoint/etc.
	 */
	uint32_t type;

	/*
	 * Size of the attr structure, for fwd/bwd compat.
	 */
	uint32_t size;

	/*
	 * Type specific configuration information.
	 */
	uint64_t config;

	union {
		uint64_t sample_period;
		uint64_t sample_freq;
	};

	uint64_t sample_type;
	uint64_t read_format;

	uint64_t disabled		:  1, /* off by default		   */
		inherit		   :  1, /* children inherit it	  */
		pinned		   :  1, /* must always be on PMU */
		exclusive	   :  1, /* only group on PMU	  */
		exclude_user   :  1, /* don't count user	  */
		exclude_kernel :  1, /* ditto kernel		  */
		exclude_hv	   :  1, /* ditto hypervisor	  */
		exclude_idle   :  1, /* don't count when idle */
		mmap		   :  1, /* include mmap data	  */
		comm		   :  1, /* include comm data	  */
		freq		   :  1, /* use freq, not period  */
		inherit_stat   :  1, /* per task counts		  */
		enable_on_exec :  1, /* next exec enables	  */
		task		   :  1, /* trace fork/exit		  */
		watermark	   :  1, /* wakeup_watermark	  */
	/*
	 * precise_ip:
	 *
	 *	0 - SAMPLE_IP can have arbitrary skid
	 *	1 - SAMPLE_IP must have constant skid
	 *	2 - SAMPLE_IP requested to have 0 skid
	 *	3 - SAMPLE_IP must have 0 skid
	 *
	 *	See also PERF_RECORD_MISC_EXACT_IP
	 */
		precise_ip	   :  2, /* skid constraint		  */
		mmap_data	   :  1, /* non-exec mmap data	  */
		sample_id_all  :  1, /* sample_type all events */

		exclude_host   :  1, /* don't count in host	  */
		exclude_guest  :  1, /* don't count in guest  */

		exclude_callchain_kernel : 1, /* exclude kernel callchains */
		exclude_callchain_user	 : 1, /* exclude user callchains */
		mmap2		   :  1, /* include mmap with inode data */

		__reserved_1   : 40;

	union {
		uint32_t wakeup_events;	  /* wakeup every n events */
		uint32_t wakeup_watermark; /* bytes before wakeup	*/
	};

	uint32_t bp_type;
	union {
		uint64_t bp_addr;
		uint64_t config1; /* extension of config */
	};

#ifdef __PERF_ATTR_VER1
	union {
		uint64_t bp_len;
		uint64_t config2; /* extension of config1 */
	};

# ifdef __PERF_ATTR_VER2
	uint64_t branch_sample_type; /* enum perf_branch_sample_type */

#  ifdef __PERF_ATTR_VER3
	/*
	 * Defines set of user regs to dump on samples.
	 * See asm/perf_regs.h for details.
	 */
	uint64_t sample_regs_user;

	/*
	 * Defines size of the user stack to dump on samples.
	 */
	uint32_t sample_stack_user;

#   ifdef __PERF_ATTR_VER4
	/* Align to u64. */
	uint32_t __reserved_2;

	/*
	 * Defines set of regs to dump for each sample
	 * state captured on:
	 *	- precise = 0: PMU interrupt
	 *	- precise > 0: sampled instruction
	 *
	 * See asm/perf_regs.h for details.
	 */
	uint64_t sample_regs_intr;
#   endif /* __PERF_ATTR_VER4 */
#  endif /* __PERF_ATTR_VER3 */
# endif /* __PERF_ATTR_VER2 */
#endif /* __PERF_ATTR_VER1 */
} __attribute__((packed));

#define PERF_RECORD_MISC_CPUMODE_MASK		(7 << 0)
#define PERF_RECORD_MISC_CPUMODE_UNKNOWN	(0 << 0)
#define PERF_RECORD_MISC_KERNEL			(1 << 0)
#define PERF_RECORD_MISC_USER			(2 << 0)
#define PERF_RECORD_MISC_HYPERVISOR		(3 << 0)
#define PERF_RECORD_MISC_GUEST_KERNEL		(4 << 0)
#define PERF_RECORD_MISC_GUEST_USER		(5 << 0)

#define PERF_RECORD_MISC_MMAP_DATA		(1 << 13)
/*
 * Indicates that the content of PERF_SAMPLE_IP points to
 * the actual instruction that triggered the event. See also
 * perf_event_attr::precise_ip.
 */
#define PERF_RECORD_MISC_EXACT_IP		(1 << 14)
/*
 * Reserve the last bit to indicate some extended misc field
 */
#define PERF_RECORD_MISC_EXT_RESERVED		(1 << 15)

struct perf_event_header {
	uint32_t type;
	uint16_t misc;
	uint16_t size;
} __attribute__((packed));

struct perf_file_section {
	uint64_t offset;	/* Offset from start of file */
	uint64_t size;		/* Size of the section */
} __attribute__((packed));

#define PERF_STRING_ALIGN	64

struct perf_header_string {
	uint32_t len;
	char string[0];		/* Zero terminated */
};

struct perf_header_string_list {
	uint32_t nr;
	struct perf_header_string strings[0];	/* Variable length records */
};

struct nr_cpus {
	uint32_t		nr_cpus_online;
	uint32_t		nr_cpus_available;
};

struct build_id_event {
	struct perf_event_header header;
	pid_t			pid;
	uint8_t			build_id[24];	/* BUILD_ID_SIZE aligned u64 */
	char			filename[];
};

#define MAX_EVENT_NAME 64

struct perf_trace_event_type {
	uint64_t event_id;
	char name[MAX_EVENT_NAME];
};

struct perf_file_attr {
	struct perf_event_attr attr;
	struct perf_file_section ids;
};

/* "PERFILE2"
 */
static const uint64_t PERF_MAGIC2 = 0x32454c4946524550ULL;

struct perf_pipe_file_header {
	uint64_t magic;			/* PERFILE2 */
	uint64_t size;
};

struct perf_header {
	uint64_t magic;			/* PERFILE2 */
	uint64_t size;			/* Size of the header */
	uint64_t attr_size;		/* size of an attribute in attrs */
	struct perf_file_section attrs;
	struct perf_file_section data;
	struct perf_file_section event_types;
	DECLARE_BITMAP(adds_features, HEADER_FEAT_BITS);
} __attribute__((packed));

/* For type PERF_RECORD_MMAP
 */
struct perf_record_mmap {
	struct perf_event_header header;
	uint32_t pid;
	uint32_t tid;
	uint64_t addr;
	uint64_t len;
	uint64_t pgoff;
	char filename[0];
} __attribute__((packed));

/* For type PERF_RECORD_COMM
 */
struct perf_record_comm {
	struct perf_event_header header;
	uint32_t pid;
	uint32_t tid;
	char comm[0];
} __attribute__((packed));

/* For type PERF_RECORD_SAMPLE
 *
 * Configured with: PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
 * PERF_SAMPLE_ADDR | PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_CPU |
 * PERF_SAMPLE_CALLCHAIN. */
struct perf_record_sample {
	struct perf_event_header header;
	uint64_t identifier;
	uint64_t ip;
	uint32_t pid, tid;
	uint64_t time;
	uint64_t addr;
	uint32_t cpu, res;
	uint64_t nr;
	uint64_t ips[0];
} __attribute__((packed));
