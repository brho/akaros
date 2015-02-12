/* TODO(gvdl): Who holds the copyright?
 * Godfrey van der Linden <gvdl@google.com>
 * See LICENSE for details.
 *
 * Timed tracing.
 * Kernel header
 * TODO(gvdl): Documentation goes here. */
#ifndef INC_TTRACE_H
#define INC_TTRACE_H

#define TTRACE_NULL_STATEMENT do { } while(0)

#ifndef CONFIG_TTRACE

// If the TTRACE is not configured then null out the initters
#define ttrace_init()    TTRACE_NULL_STATEMENT
#define ttrace_cleanup() TTRACE_NULL_STATEMENT

#else /* CONFIG_TTRACE */

#include <atomic.h>
#include <ros/ttrace.h>

// Global data

// Active tracepoint mask, see TTRACE_TYPE... enums in ros/ttrace.h
extern uint64_t ttrace_type_mask;

// Two phase initialisation. Call ttrace_init() as soon as page memory
// allocation is possible so that we can trace very early in system boot.
// However, that early in boot we do not yet know how many cores we will
// manange, ttrace_cleanup() is called once we know and returns memory
// previously allocated for unused cores.
extern void ttrace_init();
extern void ttrace_cleanup();

// devttrace accessor routines
struct trace_ring* get_ttrace_ring_for_core(uint32_t coreid);
void fill_ttrace_version(struct ttrace_version *versp);
const uint8_t *bufferp get_ttrace_aux_buffer(void)
void get_ttrace_aux_buffer_snapshot(ptrdiff_t *firstp, ptrdiff_t *lastp);

// Low level data collection apis, do not use directly but rather use the macro
// wrappers, such as TTRACE_PROC_ALLOC() below.
extern uint64_t _ttrace_point(uint64_t type_start_stop,
							  uintptr_t d0, uintptr_t d1, uintptr_t d2,
							  uintptr_t d3, uintptr_t d4);
extern void _ttrace_point_cont(uint64_t timestamp, uintptr_t d0, uintptr_t d1,
							   uintptr_t d2, uintptr_t d3, uintptr_t d4,
							   uintptr_t d5, uintptr_t d6);
// Slow takes rwlock, updates auxillary buffer, call this rarely
extern void _ttrace_point_string(uint8_t tag, uintptr_t ident, const char *str);

static inline uintptr_t __attribute__((always_inline))
take_tracepoint(uintptr_t type_start_stop)
{
	uintptr_t cur_mask = atomic_read((atomic_t *) &ttrace_type_mask);
	return cur_mask & type_start_stop & TTRACE_TYPE_MASK;
}

static inline uint64_t _ttrace_point5(uint64_t type_start_stop,
									  uintptr_t d0, uintptr_t d1, uintptr_t d2,
									  uintptr_t d3, uintptr_t d4)
{
	if (take_tracepoint(type_start_stop))
		return _ttrace_point(type_start_stop, d0, d1, d2, d3, d4);
	return 0;
}
#define TTRACE_POINT5(type, start_stop, data...) do {						\
	static_assert(type & TTRACE_TYPE_MASK);									\
	_ttrace_point5(type | start_stop, data);								\
} while(false)

static inline uint64_t _ttrace_point12(uint64_t type_start_stop,
									   uintptr_t d0, uintptr_t d1, uintptr_t d2,
									   uintptr_t d3, uintptr_t d4, uintptr_t d5,
									   uintptr_t d6, uintptr_t d7, uintptr_t d8,
									   uintptr_t d9, uintptr_t da, uintptr_t db)
{
	if (take_tracepoint(type_start_stop)) {
		const uint64_t tsc = _ttrace_point(type_start_stop, d0, d1, d2, d3, d4);
		_ttrace_point_cont(tsc, d5, d6, d7, d8, d9, da, db);
		return tsc;
	} else
		return 0;
}
#define TTRACE_POINT12(type, start_stop, data...) do {						\
	static_assert(type & TTRACE_TYPE_MASK);									\
	_ttrace_point12(type | start_stop, data);								\
} while(false)

static inline void _ttrace_string(uint64_t type, uint8_t tag,
								  uintptr_t ident, const char* str)
{
	if (take_tracepoint(type))
		_ttrace_point_string(tag, ident, str);
}
// string len < TTRACE_AUX_ENTRY_MAXSTRLEN
#define TTRACE_STRING(type, tag, ident, str) do {							\
	static_assert(type & TTRACE_TYPE_MASK);									\
	static_assert(tag & TTRACEH_TAG_MASK);									\
	_ttrace_string(type, tag, ident, str);
} while(false)

//
// CONFIG_TTRACE macros
//
// These macros are the data collection side of ttrace. Each CONFIG_TTRACE type
// has its own group of macros which are empty if the group is not configured.

// CONFIG_TTRACE_PROCESS trace point macros.
#ifdef CONFIG_TTRACE_PROCESS
#define TTRACE_PROC_ALLOC(pid, ppid) TTRACE_POINT5(							\
	TTRACE_TYPE_PROCESS, TTRACE_ENTRY_START, pid, ppid, 0, 0, 0);
#define TTRACE_PROC_FREE(pid, cpid) TTRACE_POINT5(							\
	TTRACE_TYPE_PROCESS, TTRACE_ENTRY_STOP, pid, cpid, 0, 0, 0);
#define TTRACE_PROC_READY(pid) TTRACE_POINT5(								\
	TTRACE_TYPE_PROCESS, TTRACE_ENTRY_ENTRY, pid, 0, 0, 0, 0);
#define TTRACE_PROC_SETSTATE(pid, state, curstate) TTRACE_POINT5(			\
	TTRACE_TYPE_PROCESS, TTRACE_ENTRY_EXIT, pid, state, curstate, 0, 0);
#define TTRACE_PROC_SETNAME(pid, name)										\
	TTRACE_STRING(TTRACE_TYPE_PROCESS, TTRACEH_TAG_PROC, pid, name)
#else /* !CONFIG_TTRACE_PROCESS */
#define TTRACE_PROC_ALLOC(pid, ppid)               TTRACE_NULL_STATEMENT
#define TTRACE_PROC_FREE(pid, cpid)                TTRACE_NULL_STATEMENT
#define TTRACE_PROC_READY(pid)                     TTRACE_NULL_STATEMENT
#define TTRACE_PROC_SETSTATE(pid, state, curstate) TTRACE_NULL_STATEMENT
#define TTRACE_PROC_SETNAME(pid, name)             TTRACE_NULL_STATEMENT
#endif /* CONFIG_TTRACE_PROCESS */

#endif /* !CONFIG_TTRACE */
#endif /* INC_TTRACE_H */
