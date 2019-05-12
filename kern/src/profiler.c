/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * This controls the emitting, collecting, and exporting of samples for perf
 * events.  Examples of events are PMU counter overflows, mmaps, and process
 * creation.
 *
 * Events are collected in a central qio queue.  High-frequency events (e.g.
 * IRQ backtraces()) are collected in per-core buffers, which are flushed to the
 * central queue when they fill up or on command.  Lower-frequency events (e.g.
 * profiler_notify_mmap()) just go straight to the central queue.
 *
 * Currently there is one global profiler.  Kprof is careful to only have one
 * open profiler at a time.  See profiler.h for more info.  The only sync we do
 * in this file is for the functions that are not called while holding the kprof
 * mutex - specifically the RCU-protected backtrace sampling code.
 *
 * A few other notes:
 * - profiler_control_trace() controls the per-core trace collection.  When it
 *   is disabled, it also flushes the per-core blocks to the central queue.
 * - The collection of mmap and comm samples is independent of trace collection.
 *   Those will occur whenever the profiler is open, even if it is not started.
 * - Looks like we don't bother with munmap records.  Not sure if perf can
 *   handle it or not. */

#include <ros/common.h>
#include <ros/mman.h>
#include <sys/types.h>
#include <smp.h>
#include <trap.h>
#include <kthread.h>
#include <env.h>
#include <process.h>
#include <mm.h>
#include <kmalloc.h>
#include <pmap.h>
#include <atomic.h>
#include <umem.h>
#include <elf.h>
#include <ns.h>
#include <err.h>
#include <core_set.h>
#include <string.h>
#include "profiler.h"

#define PROFILER_MAX_PRG_PATH	256

#define VBE_MAX_SIZE(t) ((8 * sizeof(t) + 6) / 7)

/* Do not rely on the contents of the PCPU ctx with IRQs enabled. */
struct profiler_cpu_context {
	struct block *block;
	int cpu;
	bool tracing;
	size_t dropped_data_cnt;
};

/* These are a little hokey, and are currently global vars */
static int profiler_queue_limit = 64 * 1024 * 1024;
static size_t profiler_cpu_buffer_size = 65536;

struct profiler {
	struct profiler_cpu_context *pcpu_ctx;
	struct queue *qio;
	bool tracing;
};

static struct profiler __rcu *gbl_prof;

static struct profiler_cpu_context *profiler_get_cpu_ctx(struct profiler *prof,
							 int cpu)
{
	return prof->pcpu_ctx + cpu;
}

static inline char *vb_encode_uint64(char *data, uint64_t n)
{
	/* Classical variable bytes encoding. Encodes 7 bits at a time, using
	 * bit number 7 in the byte, as indicator of end of sequence (when
	 * zero). */
	for (; n >= 0x80; n >>= 7)
		*data++ = (char) (n | 0x80);
	*data++ = (char) n;

	return data;
}

static struct block *profiler_buffer_write(struct profiler *prof,
					   struct profiler_cpu_context *cpu_buf,
                                           struct block *b)
{
	/* qpass will drop b if the queue is over its limit.  we're willing to
	 * lose traces, but we won't lose 'control' events, such as MMAP and
	 * PID. */
	if (b) {
		if (qpass(prof->qio, b) < 0)
			cpu_buf->dropped_data_cnt++;
	}
	return block_alloc(profiler_cpu_buffer_size, MEM_ATOMIC);
}

/* Helper, paired with profiler_cpu_buffer_write_commit.  Ensures there is
 * enough room in the pcpu block for our write.  May alloc a new one.
 *
 * IRQs must be disabled before calling, until after write_commit. */
static char *profiler_cpu_buffer_write_reserve(struct profiler *prof,
	struct profiler_cpu_context *cpu_buf, size_t size, struct block **pb)
{
	struct block *b = cpu_buf->block;

	if (unlikely((!b) || (b->lim - b->wp) < size)) {
		cpu_buf->block = b = profiler_buffer_write(prof, cpu_buf, b);
		if (unlikely(!b))
			return NULL;
	}
	*pb = b;

	return (char *) b->wp;
}

/* Helper, paired with write_reserve.  Finalizes the writing into the block's
 * main body of @size bytes.  IRQs must be disabled until after this is called.
 */
static inline void profiler_cpu_buffer_write_commit(
	struct profiler_cpu_context *cpu_buf, struct block *b, size_t size)
{
	b->wp += size;
}

static inline size_t profiler_max_envelope_size(void)
{
	return 2 * VBE_MAX_SIZE(uint64_t);
}

static void profiler_push_kernel_trace64(struct profiler *prof,
					 struct profiler_cpu_context *cpu_buf,
                                         const uintptr_t *trace, size_t count,
                                         uint64_t info)
{
	struct per_cpu_info *pcpui = &per_cpu_info[core_id()];
	size_t size = sizeof(struct proftype_kern_trace64) +
		count * sizeof(uint64_t);
	struct block *b;
	void *resptr, *ptr;

	assert(!irq_is_enabled());
	resptr = profiler_cpu_buffer_write_reserve(prof,
	    cpu_buf, size + profiler_max_envelope_size(), &b);
	ptr = resptr;

	if (likely(ptr)) {
		struct proftype_kern_trace64 *record;

		ptr = vb_encode_uint64(ptr, PROFTYPE_KERN_TRACE64);
		ptr = vb_encode_uint64(ptr, size);

		record = (struct proftype_kern_trace64 *) ptr;
		ptr += size;

		record->info = info;
		record->tstamp = nsec();
		if (is_ktask(pcpui->cur_kthread) || !pcpui->cur_proc)
			record->pid = -1;
		else
			record->pid = pcpui->cur_proc->pid;
		record->cpu = cpu_buf->cpu;
		record->num_traces = count;
		for (size_t i = 0; i < count; i++)
			record->trace[i] = (uint64_t) trace[i];

		profiler_cpu_buffer_write_commit(cpu_buf, b, ptr - resptr);
	}
}

static void profiler_push_user_trace64(struct profiler *prof,
				       struct profiler_cpu_context *cpu_buf,
                                       struct proc *p, const uintptr_t *trace,
                                       size_t count, uint64_t info)
{
	size_t size = sizeof(struct proftype_user_trace64) +
		count * sizeof(uint64_t);
	struct block *b;
	void *resptr, *ptr;

	assert(!irq_is_enabled());
	resptr = profiler_cpu_buffer_write_reserve(prof,
	    cpu_buf, size + profiler_max_envelope_size(), &b);
	ptr = resptr;

	if (likely(ptr)) {
		struct proftype_user_trace64 *record;

		ptr = vb_encode_uint64(ptr, PROFTYPE_USER_TRACE64);
		ptr = vb_encode_uint64(ptr, size);

		record = (struct proftype_user_trace64 *) ptr;
		ptr += size;

		record->info = info;
		record->tstamp = nsec();
		record->pid = p->pid;
		record->cpu = cpu_buf->cpu;
		record->num_traces = count;
		for (size_t i = 0; i < count; i++)
			record->trace[i] = (uint64_t) trace[i];

		profiler_cpu_buffer_write_commit(cpu_buf, b, ptr - resptr);
	}
}

static void profiler_push_pid_mmap(struct profiler *prof, struct proc *p,
				   uintptr_t addr, size_t msize, size_t offset,
				   const char *path)
{
	size_t plen = strlen(path) + 1;
	size_t size = sizeof(struct proftype_pid_mmap64) + plen;
	void *resptr = kmalloc(size + profiler_max_envelope_size(), MEM_ATOMIC);

	if (likely(resptr)) {
		void *ptr = resptr;
		struct proftype_pid_mmap64 *record;

		ptr = vb_encode_uint64(ptr, PROFTYPE_PID_MMAP64);
		ptr = vb_encode_uint64(ptr, size);

		record = (struct proftype_pid_mmap64 *) ptr;
		ptr += size;

		record->tstamp = nsec();
		record->pid = p->pid;
		record->addr = addr;
		record->size = msize;
		record->offset = offset;
		memcpy(record->path, path, plen);

		qiwrite(prof->qio, resptr, (int) (ptr - resptr));

		kfree(resptr);
	}
}

static void profiler_push_new_process(struct profiler *prof, struct proc *p)
{
	size_t plen = strlen(p->binary_path) + 1;
	size_t size = sizeof(struct proftype_new_process) + plen;
	void *resptr = kmalloc(size + profiler_max_envelope_size(), MEM_ATOMIC);

	if (likely(resptr)) {
		void *ptr = resptr;
		struct proftype_new_process *record;

		ptr = vb_encode_uint64(ptr, PROFTYPE_NEW_PROCESS);
		ptr = vb_encode_uint64(ptr, size);

		record = (struct proftype_new_process *) ptr;
		ptr += size;

		record->tstamp = nsec();
		record->pid = p->pid;
		memcpy(record->path, p->binary_path, plen);

		qiwrite(prof->qio, resptr, (int) (ptr - resptr));

		kfree(resptr);
	}
}

static void profiler_emit_current_system_status(void)
{
	void enum_proc(struct vm_region *vmr, void *opaque)
	{
		struct proc *p = (struct proc *) opaque;

		profiler_notify_mmap(p, vmr->vm_base,
				     vmr->vm_end - vmr->vm_base,
		                     vmr->vm_prot, vmr->vm_flags, vmr->__vm_foc,
		                     vmr->vm_foff);
	}

	struct process_set pset;

	proc_get_set(&pset);

	for (size_t i = 0; i < pset.num_processes; i++) {
		profiler_notify_new_process(pset.procs[i]);
		enumerate_vmrs(pset.procs[i], enum_proc, pset.procs[i]);
	}

	proc_free_set(&pset);
}

static long profiler_get_checked_value(const char *value, long k, long minval,
                                       long maxval)
{
	long lvalue = strtol(value, NULL, 0) * k;

	if (lvalue < minval)
		error(EFAIL, "Value should be greater than %ld", minval);
	if (lvalue > maxval)
		error(EFAIL, "Value should be lower than %ld", maxval);

	return lvalue;
}

/* TODO: This configure stuff is a little hokey.  You have to configure before
 * it's been opened, meaning before you have the kprofctlqid, but you can't
 * configure until you have the chan.  To use this, you'd need to open, then
 * config, then close, then hope that the global settings stick around, then
 * open and run it.
 *
 * Also note that no one uses this. */
int profiler_configure(struct cmdbuf *cb)
{
	if (!strcmp(cb->f[0], "prof_qlimit")) {
		if (cb->nf < 2)
			error(EFAIL, "prof_qlimit KB");
		/* If the profiler is already running, this won't take effect
		 * until the next open.  Feel free to change this. */
		WRITE_ONCE(profiler_queue_limit,
			   profiler_get_checked_value(cb->f[1], 1024,
						      1024 * 1024,
						      max_pmem / 32));
		return 1;
	}
	if (!strcmp(cb->f[0], "prof_cpubufsz")) {
		if (cb->nf < 2)
			error(EFAIL, "prof_cpubufsz KB");
		WRITE_ONCE(profiler_cpu_buffer_size,
			   profiler_get_checked_value(cb->f[1], 1024,
						      16 * 1024,
						      1024 * 1024));
		return 1;
	}

	return 0;
}

void profiler_append_configure_usage(char *msgbuf, size_t buflen)
{
	const char * const cmds[] = {
		"prof_qlimit",
		"prof_cpubufsz",
	};

	for (int i = 0; i < ARRAY_SIZE(cmds); i++) {
		strlcat(msgbuf, "|", buflen);
		strlcat(msgbuf, cmds[i], buflen);
	}
}

int profiler_setup(void)
{
	struct profiler *prof;

	assert(!rcu_dereference_check(gbl_prof, true));
	prof = kzmalloc(sizeof(struct profiler), MEM_WAIT);
	/* It is very important that we enqueue and dequeue entire records at
	 * once.  If we leave partial records, the entire stream will be
	 * corrupt.  Our reader does its best to make sure it has room for
	 * complete records (checks qlen()).
	 *
	 * If we ever get corrupt streams, try making this a Qmsg.  Though it
	 * doesn't help every situation - we have issues with writes greater
	 * than Maxatomic regardless. */
	prof->qio = qopen(profiler_queue_limit, 0, NULL, NULL);
	if (!prof->qio) {
		kfree(prof);
		return -1;
	}
	prof->pcpu_ctx = kzmalloc(sizeof(struct profiler_cpu_context)
				  * num_cores, MEM_WAIT);
	for (int i = 0; i < num_cores; i++) {
		struct profiler_cpu_context *b = &prof->pcpu_ctx[i];

		b->cpu = i;
	}
	rcu_assign_pointer(gbl_prof, prof);
	profiler_emit_current_system_status();
	return 0;
}

void profiler_cleanup(void)
{
	struct profiler *prof = rcu_dereference_protected(gbl_prof, true);

	RCU_INIT_POINTER(gbl_prof, NULL);
	synchronize_rcu();
	kfree(prof->pcpu_ctx);
	qfree(prof->qio);
	kfree(prof);
}

static void profiler_cpu_flush(struct profiler *prof,
			       struct profiler_cpu_context *cpu_buf)
{
	int8_t irq_state = 0;

	disable_irqsave(&irq_state);
	if (cpu_buf->block) {
		qibwrite(prof->qio, cpu_buf->block);

		cpu_buf->block = NULL;
	}
	enable_irqsave(&irq_state);
}

static void __profiler_core_trace_enable(void *opaque)
{
	struct profiler *prof = opaque;
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(prof,
								    core_id());

	cpu_buf->tracing = prof->tracing;
	if (!cpu_buf->tracing)
		profiler_cpu_flush(prof, cpu_buf);
}

static void profiler_control_trace(struct profiler *prof, int onoff)
{
	struct core_set cset;

	assert(prof);

	core_set_init(&cset);
	core_set_fill_available(&cset);
	prof->tracing = onoff;
	/* Note this blocks until all cores have run the function. */
	smp_do_in_cores(&cset, __profiler_core_trace_enable, prof);
}

/* This must only be called by the Kprofctlqid FD holder, ensuring that the
 * profiler exists.  Not thread-safe. */
void profiler_start(void)
{
	struct profiler *prof = rcu_dereference_protected(gbl_prof, true);

	profiler_control_trace(prof, 1);
	qreopen(prof->qio);
}

/* This must only be called by the Kprofctlqid FD holder, ensuring that the
 * profiler exists.  Not thread-safe. */
void profiler_stop(void)
{
	struct profiler *prof = rcu_dereference_protected(gbl_prof, true);

	profiler_control_trace(prof, 0);
	qhangup(prof->qio, 0);
}

static void __profiler_core_flush(void *opaque)
{
	struct profiler *prof = opaque;
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(prof,
								    core_id());

	profiler_cpu_flush(prof, cpu_buf);
}

/* This must only be called by the Kprofctlqid FD holder, ensuring that the
 * profiler exists. */
void profiler_trace_data_flush(void)
{
	struct core_set cset;

	core_set_init(&cset);
	core_set_fill_available(&cset);
	smp_do_in_cores(&cset, __profiler_core_flush, NULL);
}

void profiler_push_kernel_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                    uint64_t info)
{
	struct profiler *prof;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	if (prof) {
		struct profiler_cpu_context *cpu_buf =
			profiler_get_cpu_ctx(prof, core_id());

		if (cpu_buf->tracing)
			profiler_push_kernel_trace64(prof, cpu_buf, pc_list,
						     nr_pcs, info);
	}
	rcu_read_unlock();
}

void profiler_push_user_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                  uint64_t info)
{
	struct profiler *prof;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	if (prof) {
		struct profiler_cpu_context *cpu_buf =
			profiler_get_cpu_ctx(prof, core_id());

		if (cpu_buf->tracing)
			profiler_push_user_trace64(prof, cpu_buf, current,
						   pc_list, nr_pcs, info);
	}
	rcu_read_unlock();
}

size_t profiler_size(void)
{
	struct profiler *prof;
	size_t ret;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	ret = prof ? qlen(prof->qio) : 0;
	rcu_read_unlock();
	return ret;
}

size_t profiler_read(void *va, size_t n)
{
	struct profiler *prof;
	size_t ret;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	ret = prof ? qread(prof->qio, va, n) : 0;
	rcu_read_unlock();
	return ret;
}

void profiler_notify_mmap(struct proc *p, uintptr_t addr, size_t size, int prot,
                          int flags, struct file_or_chan *foc, size_t offset)
{
	struct profiler *prof;
	char *path;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	if (prof && foc && (prot & PROT_EXEC))
		profiler_push_pid_mmap(prof, p, addr, size, offset,
				       foc_abs_path(foc));
	rcu_read_unlock();
}

void profiler_notify_new_process(struct proc *p)
{
	struct profiler *prof;

	rcu_read_lock();
	prof = rcu_dereference(gbl_prof);
	if (prof && p->binary_path)
		profiler_push_new_process(prof, p);
	rcu_read_unlock();
}
