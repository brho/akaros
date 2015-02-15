/* TODO(gvdl): Who holds the copyright?
 * Godfrey van der Linden <gvdl@google.com>
 * See LICENSE for details.
 *
 * Timed tracing.
 *
 * This is the first cut at a TTRACE header. At the moment only sufficient type
 * information for the initial implementation of kprof_ttrace logging output
 * subsystem to be prototyped. That is this file is currently part of the
 * scaffolding necessary to bring up a comlicated facility and break it up into
 * reasonably sized changes.
 */
#ifndef INC_ROS_TTRACE_H
#define INC_ROS_TTRACE_H

#ifndef CONFIG_TTRACE

// If the TTRACE is not configured then null out the initter
#define ttrace_init()    do { } while(false)
#define ttrace_cleanup() do { } while(false)

#else /* CONFIG_TTRACE */

extern uint64_t ttrace_type_mask;

enum {
	/* Block of scheduling tracking trace type bits */
	TTRACE_TYPE_MASK       = 0xfffffffffff00000ULL,

	/* ttrace/data tags and misc */
	TTRACEH_V1             = 0x0100,  /* Version 1.0 ttrace/data format */
	TTRACEE_V1             = 0x0090,  /* Version 0.9 ttrace/cpu* scaffold */
	TTRACEH_TAG_CONT       = 0x80,    /* Continuation record top bit set */
	TTRACEH_TAG_INFO       = 0x01,    /* Data file info version */
	TTRACEH_TAG_SYSC       = 0x02,    /* Syscall entry */
	TTRACEH_TAG_PROC       = 0x03,    /* Proc name */
	TTRACEH_TAG_KTASK      = 0x04,    /* Ktask name */
	TTRACEH_TAG_SEM        = 0x05,    /* Semaphore name */
	TTRACEH_TAG_CV         = 0x06,    /* Condition name */
	TTRACEH_TAG_QLOCK      = 0x07,    /* Qlock name */

	/* ttrace/data header lengths */
	TTRACEH_CONT_LEN = 2 + 2, /* len, tag */
	TTRACEH_LEN = TTRACEH_CONT_LEN + 16, /* len + tag + timestamp */
	TTRACEH_NAME_LEN = TTRACEH_LEN + 16, /* header len + id len */
};

// Timed trace data version 1 record format,
//   All lines are nl terminated.
//   where h[n]-> n hex digits, s[n] -> n byte string
//
//   Header:
//   h[2]: len  Max 127(0xff)
//   h[2]: tag
//   h[16]: timestamp
//
//   Info record:
//   h[20]: header, timestamp is always 1 tag: TTRACEH_TAG_INFO
//   h[4]: summary data version
//   h[4]: cpu data version
//   h[4]: num cpus
//
//   Name record:
//   h[20]: header
//   h[16]: id;  uintptr_t, such as kthread, pid or syscall number
//   s[hdr.len-36]: string (not nul terminated)
//
//   Name continuation record(no timestamp), always follows a Name record:
//   h[2]: len
//   h[2]: tag (top bit set)
//   s[hdr.len-4]: string (not nul terminated)
//

#endif /* CONFIG_TTRACE */
#endif /* INC_ROS_TTRACE_H */
