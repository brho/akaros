/* TODO(gvdl): Who holds the copyright?
 * Godfrey van der Linden <gvdl@google.com>
 * See LICENSE for details.
 *
 * Timed tracing.
 *
 * This file is only included if CONFIG_TTRACE=y
 * TODO(gvdl): Documentation goes here. */

#include <ttrace.h>

#include <arch/arch.h>
#include <atomic.h>
#include <env.h>
#include <page_alloc.h>
#include <rwlock.h>
#include <smp.h>
#include <syscall.h>
#include <trace.h>
#include <trap.h>

#ifndef min
#define min(a, b) ({ \
	typeof (a) _a = (a); typeof (b) _b = (b); _a < _b ? _a : _b; })
#endif

// Shared buffer must be a power of 2, static_asserted in ttrace_init.
#define TTRACE_AUX_BUFFER_MB   ((size_t) 1)
#define TTRACE_AUX_BUFFER_SZ   (TTRACE_AUX_BUFFER_MB * 1024 * 1024)
#define TTRACE_AUX_BUFFER_MASK (TTRACE_AUX_BUFFER_SZ - 1)

// TODO(gvdl): Need to make this a config parameter
// TODO(gvdl): Implement dynamic buffer resize, virtually contig pages
#define TTRACE_PER_CPU_BUFFER_MB ((size_t) 1) // 16K ttrace entries per cpu

#define TTRACE_PER_CPU_BUFFER_SZ (TTRACE_PER_CPU_BUFFER_MB * 1024 * 1024)
#define PER_CPU_ORDER ({													\
	const uint64_t per_cpu_pages = TTRACE_PER_CPU_BUFFER_SZ / PGSIZE;		\
	(size_t) LOG2_UP(per_cpu_pages);										\
})

// Global data
uint64_t ttrace_type_mask;

// Local data
static rwlock_t ttrace_aux_rwlock;
static ptrdiff_t ttrace_aux_first, ttrace_aux_last; // Offsets into aux
static uint8_t ttrace_aux_buffer[TTRACE_AUX_BUFFER_SZ];
static struct trace_ring ttrace_ring[MAX_NUM_CPUS];

//
// TTRACE implementation
//
void ttrace_init()
{
	// Single tracepoint entries should be atomically written.
	static_assert(sizeof(struct ttrace_entry) == ARCH_CL_SIZE);
	static_assert(
	   sizeof(struct ttrace_entry_continuation) == sizeof(struct ttrace_entry));
	static_assert(sizeof(ttrace_type_mask) == sizeof(atomic_t));
	static_assert(IS_PWR2(TTRACE_AUX_BUFFER_MB));

	atomic_init((atomic_t *) &ttrace_type_mask, 0);
	rwinit(&ttrace_aux_rwlock);

	/* ttrace_init() is called as soon as memory is initialised so that we can
	 * profile the early boot sequence if we wish.  But that means it is called
	 * before arch_init and until arch_init is called num_cpus is invalid. So
	 * we allocate as much memory as we'll ever need here and will give the
	 * memory back in ttrace_cleanup() which is called much later in the init
	 * process. */
	const size_t per_cpu_order = PER_CPU_ORDER;
	const size_t per_cpu_bytes = (1 << per_cpu_order) * PGSIZE;

	for (int i = 0; i < MAX_NUM_CPUS; i++) {
		/* TODO(gvdl): Only need virtually contiguous pages! */
		trace_ring_init(&ttrace_ring[i], get_cont_pages(per_cpu_order, 0),
						TTRACE_PER_CPU_BUFFER_SZ, sizeof(struct ttrace_entry));
	}

	// Quickly record all of the systems calls, known at compile time
	for (size_t i = 0; i < max_syscall; i++) {
		const char * const name = syscall_table[i].name;
		if (name)
			_ttrace_point_string(TTRACEH_TAG_SYSC, i, name);
	}
}

void ttrace_cleanup()
{
	/* Give back un-used trace_rings, assumes core_id is contiguous from 0 */
	const size_t per_cpu_order = PER_CPU_ORDER;
	for (int i = num_cpus; i < MAX_NUM_CPUS; i++) {
		free_cont_pages(ttrace_ring[i].tr_buf, per_cpu_order);
		memset(&ttrace_ring[i], 0, sizeof(ttrace_ring[i]));
	}
}

//
// devttrace interface
//
struct trace_ring* get_ttrace_ring_for_core(uint32_t coreid)
{
	return coreid < num_cpus? &ttrace_ring[coreid] : NULL;
}

void fill_ttrace_version(struct ttrace_version *versp)
{
	versp->magic = TTRACEVH_MAGIC;
	versp->header_version = TTRACEVH_H_V1_0;
	versp->shared_data_version = TTRACEVH_S_V1_0;
	versp->percpu_data_version = TTRACEVH_C_V1_0;
	versp->num_cpus = num_cpus;
	versp->buffer_mask = TTRACE_AUX_BUFFER_MASK;
	versp->first_offset = versp->last_offset = (ptrdiff_t) -1;
}

const uint8_t *bufferp get_ttrace_aux_buffer(void)
{
	return ttrace_aux_buffer;
}

void get_ttrace_aux_buffer_snapshot(ptrdiff_t *firstp, ptrdiff_t *lastp)
{
	rlock(&ttrace_aux_rwlock);
	*firstp = ttrace_aux_first;
	*lastp = ttrace_aux_last;
	runlock(&ttrace_aux_rwlock);
}

//
// ttrace data collection
//

// From Barret
// An arbitrary kernel thread, running in the kernel, that might block in
// sem_down can have a few sources:
//   1) Process issued syscall, when it was submitted locally (trap/sysenter)
//      and before it blocks. Then owning_proc, cur_proc, owning_vcoreid,
//      kthread->sysc are accurate.
//   2) Process issued syscall, either remotely or after it blocked and
//      restarted at least once.  cur_proc and kthread->sysc are accurate.
//   3) Process issued trap (SCP page faults on files are handled by the kernel
//      with blocking kthreads.  There may be others.)  cur_proc is accurate.
//      kthread->sysc should be 0. Might be fault into in cur_ctx.
//   4) Kernel task, aka ktask.  It's kernel thread not in a user's context,
//      has a name. No cur_proc or anything like that. There is kthread->name.
//   5) Unnamed routine kernel messages.  Like a ktask, there is no user
//      component. They have no names. Can use kmsg function pointer.
//
// For the syscalls (case 1 and 2), it is tempting to track the vcoreid, but it
// can be wrong in case 2.  I think cur_proc->pid and kthread->sysc->num will
// work.
//
// Case 3 is like case 1 and 2, but there is no kthread->sysc. Right now, it's
// probably not worth it, but you can peak in cur_ctx and extract the trap
// number.  that might be arch-dependent and nasty, so probably not worth it.
// the only one that happens now is a PF.
//
// Case 4 and 5 are basically the same.  5 just has a blank name.
// Incidentally, we can try to attach names to all RKMs.  It'll require some
// work when it comes to how kthread->name is managed.  Might not be worth it,
// just for this, unless we have a lot of unnamed RKMs gumming up the works.

// TODO(gvdl): This function is a bit heavy weight for a fast path. It will be
// better to add ttrace_ctx to per_cpu_info and set it when we change contexts
// which should be way less frequently than computing it for every record.
static inline void _ttrace_set_type(struct ttrace_type *tt, uint64_t type,
									uint32_t coreid)
{
	const struct per_cpu_info *pcpui = &per_cpu_info[coreid];
	const struct kthread * const kt = pcpui->cur_kthread;
	const struct proc * const cp = pcpui->cur_proc;

	tt->type_id = type & ~TTRACE_CTX_MASK;
#if 0
	/* TODO(gvdl) Investigate TRAP and IRQ contexts */
	if (pcpui->__ctx_depth) {
		tt->type_id |= TTRACE_CTX_IRQTRAP;
		tt->ctx.ctx_depth = pcpui->__ctx_depth;
	}
#endif // 0
	if (!cp) {
		// case 4-5 Kernel task or routine KMSG
		tt->ctx.kthread = (uintptr_t) kt;
		tt->type_id |= (kt->name)? TTRACE_CTX_KTASK : TTRACE_CTX_RKMSG;
	} else if (kt->sysc) { // case 1-2.
		// TODO(gvdl) I'd like to log vcore if possible
		tt->ctx.pid_syscall = (uintptr_t) cp->pid << 32 | kt->sysc->num;
		tt->type_id |= TTRACE_CTX_PIDSYSC;
	} else { // case 3
		// TODO(gvdl) Trap id?
		tt->ctx.pid_syscall = (uintptr_t) cp->pid << 32;
		tt->type_id |= TTRACE_CTX_PIDTRAP;
	}
}

static inline struct ttrace_entry *_ttrace_next_line(uint32_t coreid)
{
	struct trace_ring *per_cpu_ttrace_ring = &ttrace_ring[coreid];
	return get_trace_slot_overwrite_racy(per_cpu_ttrace_ring);
}

static inline bool __attribute__((always_inline))
_ttrace_on_fast_path(uint64_t type_start_stop)
{
	(void) type_start_stop; /* unused */
	return true;  // No slow path dynamic checks yet, will be in future.
}

uint64_t _ttrace_point(uint64_t type_start_stop, uintptr_t d0, uintptr_t d1,
					   uintptr_t d2, uintptr_t d3, uintptr_t d4)
{
	if (!_ttrace_on_fast_path(type_start_stop)) {
		/* Slow path, will contain a list of more selective tests than the
		 * simple TTRACE mask test in the header. */
		assert(false);  // Unimplemented
		return 0;
	}

	/* record tracepoint */
	const uint32_t coreid = core_id();
	struct ttrace_entry *cur_trace_entry = _ttrace_next_line(coreid);

	/* Bottom bit of timestamp is used to indicate a continuation record */
	const uint64_t tsc = read_tscp() & ~1ULL; // Clear bottom bit
	cur_trace_entry->timestamp = -1ULL; // Invalidate entry
	wmb();

	_ttrace_set_type(&cur_trace_entry->t, type_start_stop, coreid);
	cur_trace_entry->data0 = d0;
	cur_trace_entry->data1 = d1;
	cur_trace_entry->data2 = d2;
	cur_trace_entry->data3 = d3;
	cur_trace_entry->data4 = d4;
	cur_trace_entry->timestamp = tsc; // Entry complete
	wmb();

	return tsc | 1;  /* Timestamp for any required continuation */
}

void _ttrace_point_cont(uint64_t timestamp,
						uintptr_t d0, uintptr_t d1, uintptr_t d2, uintptr_t d3,
						uintptr_t d4, uintptr_t d5, uintptr_t d6)
{
	dassert(timestamp & 1);
	const uint32_t coreid = core_id();
	struct ttrace_entry_continuation *cur_trace_cont
		= (struct ttrace_entry_continuation *) _ttrace_next_line(coreid);

	cur_trace_cont->timestamp = -1ULL; // Invalidate entry
	wmb();

	cur_trace_cont->data0 = d0;
	cur_trace_cont->data1 = d1;
	cur_trace_cont->data2 = d2;
	cur_trace_cont->data3 = d3;
	cur_trace_cont->data4 = d4;
	cur_trace_cont->data5 = d5;
	cur_trace_cont->data6 = d6;
	cur_trace_cont->timestamp = timestamp; // Entry complete
	wmb();
}

// Auxiliary buffer handling.
// Offsets can be greater than the buffer's size
static inline struct ttrace_aux_entry *ttrace_aux(ptrdiff_t offset)
{
	offset &= TTRACE_AUX_BUFFER_MASK;
	return (struct ttrace_aux_entry *) (&ttrace_aux_buffer[offset]);
}

// Copy header concatenated with payload data into target, may wrap around the
// ttrace_aux_buffer.
static inline void ttrace_fill_aux(void *vtarget,
								   const void *vheader,  const int header_len,
								   const void *vpaydata, const int pay_len)
{
	uint8_t * const target = (uint8_t *) vtarget;
	uint8_t * const header = (uint8_t *) vheader;
	uint8_t * const paydata = (uint8_t *) vpaydata;
	size_t offset = 0;
	size_t remaining = header_len + pay_len;
	do {
		const ptrdiff_t buf_remaining
			= &ttrace_aux_buffer[sizeof(ttrace_aux_buffer)] - target;
		size_t len = min(remaining, buf_remaining);
		uint8_t * const dcp = &target[offset];
		const uint8_t *scp = NULL;
		if (offset < header_len) {
			scp = &header[offset];
			if ((header_len - offset) < len)
				len = header_len - offset;
		} else {
			const int pay_offset = offset - header_len;
			scp = &paydata[pay_offset];
			if ((pay_len - pay_offset) < len)
				len = pay_len - pay_offset;
		}
		memcpy(dcp, scp, len);
		offset += len;
		remaining -= len;
	} while (remaining > 0);
}

/* The ttrace_aux_buffer is copied out to user land as a large binary blob. As
 * _ttrace_point_string is called from performance critical times, e.g.  proc
 * creation, it is important that writing data does not contend on a long held
 * reader lock.  Hence the complicated protocol between _ttrace_point_string
 * and devttrace:ttdevread_aux. The goal is to minimize mutual exclusion time
 * both between all writers and an occasional reader.
 *
 * aux write:
 *   wlock
 *     compute last entry
 *     compute end of new last entry
 *     advance first entry pointer past new last entry end
 *     mark entry path, (tag, coreid, len, Entry INVALID)
 *     wmb()  // Is this barrier necessary before an unlock?
 *   wunlock
 *   memcpy data to aux buffer at new entry location
 *   wmb()
 *   mark entry path, (tag, coreid, len, Entry VALID)
 *
 *  aux_read:
 *    // Minimise time between offset copies by taking page faults now
 *    touch every page in user space
 *    rlock
 *      copy last entry offset
 *    runlock
 *    memcpy auxilary buffer
 *    rlock
 *      copy first entry offset
 *    runlock
 *  On return from aux_read copied last and first entries are valid, probably,
 *  and each entry can be checked by the client to determine if it stamped as
 *  VALID. Note even if the entry is INVALID the length is always ok.
 */
void _ttrace_point_string(uint8_t tag, uintptr_t ident, const char *str)
{
	int len = strnlen(str, TTRACE_AUX_ENTRY_MAXSTRLEN);
	assert(!str[len]);
	len += sizeof(ttrace_aux_entry);  // Length of new record
	const uint64_t path = TTRACE_AUX_PATH(tag, core_id(), len, 0);
	struct ttrace_aux_entry *last_aux = NULL;

	wlock(&ttrace_aux_rwlock);
	do {
		dassert(ttrace_aux_last == ttrace_aux_align(ttrace_aux_last));
		dassert(ttrace_aux_first == ttrace_aux_align(ttrace_aux_first));

		const ptrdiff_t old_aux_last = ttrace_aux_last;
		struct ttrace_aux_entry *old_last_aux = ttrace_aux(old_aux_last);

		// Don't care about wrapping the ttrace_aux function masks out high bits
		ptrdiff_t new_aux_first = ttrace_aux_first;
		ptrdiff_t new_aux_last
			= old_aux_last + ttrace_aux_align(old_last_aux->len & ~1);
		dassert(new_aux_last == ttrace_aux_align(new_aux_last));

		const ptrdiff_t next_aux_last = new_aux_last + ttrace_aux_align(len);
		struct ttrace_aux_entry *first_aux = ttrace_aux(new_aux_first);
		while (new_aux_first + TTRACE_AUX_BUFFER_SZ < next_aux_last) {
			const ptrdiff_t first_len
				= ttrace_aux_align(TTRACE_AUX_LEN(first_aux->tag_len));
			new_aux_first += first_len;
			dassert(new_aux_first == ttrace_aux_align(new_aux_first));
			first_aux = ttrace_aux(new_aux_first);
		}
		/* Check first for wrap, in which case wrap both first and last */
		if (new_aux_first >= TTRACE_AUX_BUFFER_SZ) {
			new_aux_first -= TTRACE_AUX_BUFFER_SZ;
			new_aux_last -= TTRACE_AUX_BUFFER_SZ;
		}
		dassert(new_aux_first < TTRACE_AUX_BUFFER_SZ);
		dassert(new_aux_last < 2 * TTRACE_AUX_BUFFER_SZ);

		ttrace_aux_last = new_aux_last;
		ttrace_aux_first = new_aux_first;
		last_aux = ttrace_aux(new_aux_last);
		atomic_set((atomic_t *) last_aux, path | 1);  // mark entry as invalid
		/* No barrier necessary before an unlock */
	} while(false);
	wunlock(&ttrace_aux_rwlock);

	struct ttrace_aux_entry header = {
		.path = path | 1;
		.timestamp = read_tscp();
		.ident = ident
	};
	ttrace_fill_aux(last_aux, &header, sizeof(header), str, len);

	/* The reader is doing a memcpy, not a formal read acquire. but I don't
	 * think that will be an issue. It just means that a record will not be
	 * valid this time the buffer is read. Next time the buffer is read the
	 * write will most probably be complete.  But the wmb is still important
	 * here because we can't have the payload write and release write swapped.
	 * Not that that would happen anyway given we have just called a
	 * function. */
	wmb(); // Write release, record is valid now.
	atomic_set((atomic_t *) last_aux, path);
}
