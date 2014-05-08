/**
 * @file cpu_buffer.c
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
 * @author Robert Richter <robert.richter@amd.com>
 *
 * Each CPU has a local buffer that stores PC value/event
 * pairs. We also log context switches when we notice them.
 * Eventually each CPU's buffer is processed into the global
 * event buffer by sync_buffer().
 *
 * We use a local buffer for two reasons: an NMI or similar
 * interrupt cannot synchronise, and high sampling rates
 * would lead to catastrophic global synchronisation if
 * a global buffer was used.
 */
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprof.h"

#define OP_BUFFER_FLAGS	0

/* we allocate an array of these and set the pointer in pcpui */
struct oprofile_cpu_buffer *op_cpu_buffer;

/* this one queue is used by #K to get all events. */
struct queue *opq;

/* this is run from core 0 for all cpu buffers. */
static void wq_sync_buffer(void);
unsigned long oprofile_cpu_buffer_size = 65536;
unsigned long oprofile_backtrace_depth = 8;

#define DEFAULT_TIMER_EXPIRE (HZ / 10)
static int work_enabled;

/*
 * Resets the cpu buffer to a sane state.
 *
 * reset these to invalid values; the next sample collected will
 * populate the buffer with proper values to initialize the buffer
 */
static inline void op_cpu_buffer_reset(int cpu)
{
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];

	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_proc = NULL;
}

/* returns the remaining free size of data in the entry */
static inline
int op_cpu_buffer_add_data(struct op_entry *entry, unsigned long val)
{
	assert(entry->size >= 0);
	if (!entry->size) {
		return 0;
	}
	*entry->data = val;
	entry->size--;
	entry->data++;
	return entry->size;
}

/* returns the size of data in the entry */
static inline
int op_cpu_buffer_get_size(struct op_entry *entry)
{
	return entry->size;
}

/* returns 0 if empty or the size of data including the current value */
static inline
int op_cpu_buffer_get_data(struct op_entry *entry, unsigned long *val)
{
	int size = entry->size;
	if (!size) {
		return 0;
	}
	*val = *entry->data;
	entry->size--;
	entry->data++;
	return size;
}

unsigned long oprofile_get_cpu_buffer_size(void)
{
	return oprofile_cpu_buffer_size;
}

void oprofile_cpu_buffer_inc_smpl_lost(void)
{
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];

	cpu_buf->sample_lost_overflow++;
}

void free_cpu_buffers(void)
{
	kfree(op_cpu_buffer);
	/* we can just leave the queue set up; it will then always return EOF */
}

#define RB_EVENT_HDR_SIZE 4

int alloc_cpu_buffers(void)
{
	int i;
	unsigned long buffer_size = oprofile_cpu_buffer_size;
	unsigned long byte_size = buffer_size * (sizeof(struct op_sample) +
						 RB_EVENT_HDR_SIZE);
	/* this can get called lots of times. Things might have been freed.
	 * So be careful.
	 */
	/* what limit? No idea. */
	if (! opq)
		opq = qopen(1024, Qmsg, NULL, NULL);
	if (! opq)
		goto fail;

	/* we *really* don't want to block. Losing data is better. */
	qnoblock(opq, 1);
	if (! op_cpu_buffer) {
		op_cpu_buffer = kzmalloc(sizeof(*op_cpu_buffer), num_cpus);
		if (! op_cpu_buffer)
			goto fail;

		for(i = 0; i < num_cpus; i++) {
			struct oprofile_cpu_buffer *b = &op_cpu_buffer[i];
			/* short term: for each event, we're going to kmalloc a
			 * sample and shove it into the opq.
			 * Long term: TBD. One option is to create a big damn Block and
			 * add to it as needed. Once the block is full we can push
			 * it onto the opq. That will actually be pretty fast and easy
			 * if we make the block page-sized. Far, far simpler than the
			 * Linux tracebuffer stuff.
			 */
			b->last_proc = NULL;
			b->last_is_kernel = -1;
			b->tracing = 1;
			b->buffer_size = buffer_size;
			b->sample_received = 0;
			b->sample_lost_overflow = 0;
			b->backtrace_aborted = 0;
			b->sample_invalid_eip = 0;
			b->cpu = i;
		}
	}

	return 0;

fail:
	free_cpu_buffers();
	return -ENOMEM;
}

void start_cpu_work(void)
{
	int i;

	work_enabled = 1;
	/* task starts here.
	schedule_delayed_work_on(i, &b->work, DEFAULT_TIMER_EXPIRE + i);
	*/
}

void end_cpu_work(void)
{
	work_enabled = 0;
}

/* placeholder. Not used yet.
 */
void flush_cpu_work(void)
{
	int i;
	struct oprofile_cpu_buffer *b = &op_cpu_buffer[core_id()];

}

/* Not used since we're not doing per-cpu buffering yet.
 */

struct op_sample *op_cpu_buffer_read_entry(struct op_entry *entry, int cpu)
{
	return NULL;
}

static struct block *op_cpu_buffer_write_reserve(struct op_entry *entry, int size)
{
	struct block *b;

	b = allocb(sizeof(struct op_sample) +
		   size * sizeof(entry->sample->data[0]));
	if (!b) {
		printk("%s: fail\n", __func__);
		return NULL;
	}
	entry->sample = (void *)b->wp;
	entry->size = size;
	entry->data = entry->sample->data;

	b->wp += sizeof(struct op_sample) +
		size * sizeof(entry->sample->data[0]);
	return b;

}
static int
op_add_code(struct oprofile_cpu_buffer *cpu_buf, unsigned long backtrace,
	    int is_kernel, struct proc *proc)
{
	struct block *b;
	struct op_entry entry;
	struct op_sample *sample;
	unsigned long flags;
	int size;
	ERRSTACK(1);

	flags = 0;

	if (waserror()) {
		poperror();
		printk("%s: failed\n", __func__);
		return 1;
	}

	if (backtrace)
		flags |= TRACE_BEGIN;

	/* notice a switch from user->kernel or vice versa */
	is_kernel = !!is_kernel;
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		flags |= KERNEL_CTX_SWITCH;
		if (is_kernel)
			flags |= IS_KERNEL;
	}

	/* notice a proc switch */
	if (cpu_buf->last_proc != proc) {
		cpu_buf->last_proc = proc;
		flags |= USER_CTX_SWITCH;
	}

	if (!flags) {
		poperror();
		/* nothing to do */
		return 0;
	}

	if (flags & USER_CTX_SWITCH)
		size = 1;
	else
		size = 0;

	b = op_cpu_buffer_write_reserve(&entry, size);

	entry.sample->eip = ESCAPE_CODE;
	entry.sample->event = flags;

	if (size)
		op_cpu_buffer_add_data(&entry, (unsigned long)proc);

	qbwrite(opq, b); /* note: out of our hands now. Don't free. */
	poperror();
	return 0;
}

static inline int
op_add_sample(struct oprofile_cpu_buffer *cpu_buf,
	      unsigned long pc, unsigned long event)
{
	ERRSTACK(1);
	struct op_entry entry;
	struct op_sample *sample;
	struct block *b;

	if (waserror()) {
		poperror();
		printk("%s: failed\n", __func__);
		return 1;
	}

	b = op_cpu_buffer_write_reserve(&entry, 0);

	sample = entry.sample;
	sample->eip = pc;
	sample->event = event;
	qbwrite(opq, b);
	poperror();
	return 0;
}

/*
 * This must be safe from any context.
 *
 * is_kernel is needed because on some architectures you cannot
 * tell if you are in kernel or user space simply by looking at
 * pc. We tag this in the buffer by generating kernel enter/exit
 * events whenever is_kernel changes
 */
static int
log_sample(struct oprofile_cpu_buffer *cpu_buf, unsigned long pc,
	   unsigned long backtrace, int is_kernel, unsigned long event,
	   struct proc *proc)
{
	struct proc *tsk = proc ? proc : current;
	cpu_buf->sample_received++;

	if (pc == ESCAPE_CODE) {
		cpu_buf->sample_invalid_eip++;
		return 0;
	}

	/* ah, so great. op_add* return 1 in event of failure.
	 * this function returns 0 in event of failure.
	 * what a cluster.
	 */
	if (op_add_code(cpu_buf, backtrace, is_kernel, tsk))
		goto fail;

	if (op_add_sample(cpu_buf, pc, event))
		goto fail;

	return 1;

fail:
	cpu_buf->sample_lost_overflow++;
	return 0;
}

static inline void oprofile_begin_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->tracing = 1;
}

static inline void oprofile_end_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->tracing = 0;
}

static inline void
__oprofile_add_ext_sample(unsigned long pc, void /*struct pt_regs*/ * const regs,
			  unsigned long event, int is_kernel,
			  struct proc *proc)
{
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];
	unsigned long backtrace = oprofile_backtrace_depth;

	/*
	 * if log_sample() fail we can't backtrace since we lost the
	 * source of this event
	 */
	if (!log_sample(cpu_buf, pc, backtrace, is_kernel, event, proc))
		/* failed */
		{
			return;
		}

	if (!backtrace) {
		return;
	}
#if 0
	oprofile_begin_trace(cpu_buf);
	oprofile_ops.backtrace(regs, backtrace);
	oprofile_end_trace(cpu_buf);
#endif
}

void oprofile_add_ext_hw_sample(unsigned long pc, void /*struct pt_regs*/ * const regs,
				unsigned long event, int is_kernel,
				struct proc *proc)
{
	__oprofile_add_ext_sample(pc, regs, event, is_kernel, proc);
}

void oprofile_add_ext_sample(unsigned long pc, void /*struct pt_regs*/ * const regs,
			     unsigned long event, int is_kernel)
{
	__oprofile_add_ext_sample(pc, regs, event, is_kernel, NULL);
}

void oprofile_add_sample(void /*struct pt_regs*/ * const regs, unsigned long event)
{
	int is_kernel;
	unsigned long pc;

	if (regs) {
		is_kernel = 0; // FIXME!user_mode(regs);
		pc = 0; // FIXME profile_pc(regs);
	} else {
		is_kernel = 0;    /* This value will not be used */
		pc = ESCAPE_CODE; /* as this causes an early return. */
	}

	__oprofile_add_ext_sample(pc, regs, event, is_kernel, NULL);
}

/*
 * Add samples with data to the ring buffer.
 *
 * Use oprofile_add_data(&entry, val) to add data and
 * oprofile_write_commit(&entry) to commit the sample.
 */
void
oprofile_write_reserve(struct op_entry *entry, void /*struct pt_regs*/ * const regs,
		       unsigned long pc, int code, int size)
{
	ERRSTACK(1);
	struct op_sample *sample;
	struct block *b;
	int is_kernel = 0; // FIXME!user_mode(regs);
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];

	if (waserror()){
		printk("%s: failed\n", __func__);
		poperror();
		goto fail;
	}
	cpu_buf->sample_received++;

	/* no backtraces for samples with data */
	if (op_add_code(cpu_buf, 0, is_kernel, current))
		goto fail;

	b = op_cpu_buffer_write_reserve(entry, size + 2);
	sample = entry->sample;
	sample->eip = ESCAPE_CODE;
	sample->event = 0;		/* no flags */

	op_cpu_buffer_add_data(entry, code);
	op_cpu_buffer_add_data(entry, pc);
	qbwrite(opq, b);
	poperror();
	return;
fail:
	entry->event = NULL;
	cpu_buf->sample_lost_overflow++;
}

int oprofile_add_data(struct op_entry *entry, unsigned long val)
{
	if (!entry->event) {
		return 0;
	}
	return op_cpu_buffer_add_data(entry, val);
}

int oprofile_add_data64(struct op_entry *entry, uint64_t val)
{
	if (!entry->event) {
		return 0;
	}
	if (op_cpu_buffer_get_size(entry) < 2)
		/*
		 * the function returns 0 to indicate a too small
		 * buffer, even if there is some space left
		 */
		{
			return 0;
		}
	if (!op_cpu_buffer_add_data(entry, (uint32_t)val)) {
		return 0;
	}
	return op_cpu_buffer_add_data(entry, (uint32_t)(val >> 32));
}

int oprofile_write_commit(struct op_entry *entry)
{
	/* not much to do at present. In future, we might write the Block
	 * to opq.
	 */
	return 0;
}

void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event)
{
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];
	log_sample(cpu_buf, pc, 0, is_kernel, event, NULL);
}

void oprofile_add_trace(unsigned long pc)
{
	struct oprofile_cpu_buffer *cpu_buf = &op_cpu_buffer[core_id()];

	if (!cpu_buf->tracing) {
		return;
	}

	/*
	 * broken frame can give an eip with the same value as an
	 * escape code, abort the trace if we get it
	 */
	if (pc == ESCAPE_CODE)
		goto fail;

	if (op_add_sample(cpu_buf, pc, 0))
		goto fail;

	return;
fail:
	printk("%s: fail. Turning of tracing on cpu %d\n", core_id());
	cpu_buf->tracing = 0;
	cpu_buf->backtrace_aborted++;
	return;
}

