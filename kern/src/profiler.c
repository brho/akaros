
#include <ros/common.h>
#include <smp.h>
#include <trap.h>
#include <kthread.h>
#include <kmalloc.h>
#include <atomic.h>
#include <sys/types.h>
#include "profiler.h"

struct op_sample {
	uint64_t hdr;
	uint64_t event;
	uint64_t data[0];
};

struct op_entry {
	struct op_sample *sample;
	size_t size;
	uint64_t *data;
};

struct profiler_cpu_context {
	spinlock_t lock;
	int tracing;
	struct block *block;
};

static int profiler_queue_limit = 1024;
static size_t profiler_cpu_buffer_size = 65536;
static size_t profiler_backtrace_depth = 16;
static struct semaphore mtx = SEMAPHORE_INITIALIZER(mtx, 1);
static int profiler_users = 0;
static struct profiler_cpu_context *profiler_percpu_ctx;
static struct queue *profiler_queue;

static inline struct profiler_cpu_context *profiler_get_cpu_ctx(int cpu)
{
	return profiler_percpu_ctx + cpu;
}

static inline uint64_t profiler_create_header(int cpu, size_t nbt)
{
	return (((uint64_t) 0xee01) << 48) | ((uint64_t) cpu << 16) |
		(uint64_t) nbt;
}

static inline size_t profiler_cpu_buffer_add_data(struct op_entry *entry,
												  const uintptr_t *values,
												  size_t count)
{
	size_t i;

	if (unlikely(count > entry->size))
		count = entry->size;
	for (i = 0; i < count; i++)
		entry->data[i] = (uint64_t) values[i];
	entry->size -= count;
	entry->data += count;

	return entry->size;
}

static void free_cpu_buffers(void)
{
	kfree(profiler_percpu_ctx);
	profiler_percpu_ctx = NULL;

	qclose(profiler_queue);
	profiler_queue = NULL;
}

static int alloc_cpu_buffers(void)
{
	int i;

	profiler_queue = qopen(profiler_queue_limit, 0, NULL, NULL);
	if (!profiler_queue)
		return -ENOMEM;

	qdropoverflow(profiler_queue, 1);
	qnonblock(profiler_queue, 1);

	profiler_percpu_ctx =
		kzmalloc(sizeof(*profiler_percpu_ctx) * num_cores, KMALLOC_WAIT);
	if (!profiler_percpu_ctx)
		goto fail;

	for (i = 0; i < num_cores; i++) {
		struct profiler_cpu_context *b = &profiler_percpu_ctx[i];

		b->tracing = 0;
		spinlock_init_irqsave(&b->lock);
	}

	return 0;

fail:
	qclose(profiler_queue);
	profiler_queue = NULL;
	return -ENOMEM;
}

int profiler_init(void)
{
	int error = 0;

	sem_down(&mtx);
	if (!profiler_queue)
		error = alloc_cpu_buffers();
	profiler_users++;
	sem_up(&mtx);

	return error;
}

void profiler_cleanup(void)
{
	sem_down(&mtx);
	profiler_users--;
	if (profiler_users == 0)
		free_cpu_buffers();
	sem_up(&mtx);
}

static struct block *profiler_cpu_buffer_write_reserve(
	struct profiler_cpu_context *cpu_buf, struct op_entry *entry, size_t size)
{
	struct block *b = cpu_buf->block;
    size_t totalsize = sizeof(struct op_sample) +
		size * sizeof(entry->sample->data[0]);

	if (unlikely((!b) || (b->lim - b->wp) < totalsize)) {
		if (b)
			qibwrite(profiler_queue, b);
		/* For now. Later, we will grab a block off the
		 * emptyblock queue.
		 */
		cpu_buf->block = b = iallocb(profiler_cpu_buffer_size);
        if (unlikely(!b)) {
			printk("%s: fail\n", __func__);
			return NULL;
		}
	}
	entry->sample = (struct op_sample *) b->wp;
	entry->size = size;
	entry->data = entry->sample->data;

	b->wp += totalsize;

	return b;
}

static inline int profiler_add_sample(struct profiler_cpu_context *cpu_buf,
									  uintptr_t pc, unsigned long event)
{
	ERRSTACK(1);
	struct op_entry entry;
	struct block *b;

	if (waserror()) {
		poperror();
		printk("%s: failed\n", __func__);
		return 1;
	}

	b = profiler_cpu_buffer_write_reserve(cpu_buf, &entry, 0);
	if (likely(b)) {
		entry.sample->hdr = profiler_create_header(core_id(), 1);
		entry.sample->event = (uint64_t) event;
		profiler_cpu_buffer_add_data(&entry, &pc, 1);
	}
	poperror();

	return b == NULL;
}

static inline void profiler_begin_trace(struct profiler_cpu_context *cpu_buf)
{
	cpu_buf->tracing = 1;
}

static inline void profiler_end_trace(struct profiler_cpu_context *cpu_buf)
{
	cpu_buf->tracing = 0;
}

static void profiler_cpubuf_flushone(int core, int newbuf)
{
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(core);

	spin_lock_irqsave(&cpu_buf->lock);
	if (cpu_buf->block) {
		printk("Core %d has data\n", core);
		qibwrite(profiler_queue, cpu_buf->block);
		printk("After qibwrite in %s, profiler_queue len %d\n",
			   __func__, qlen(profiler_queue));
	}
	if (newbuf)
		cpu_buf->block = iallocb(profiler_cpu_buffer_size);
	else
		cpu_buf->block = NULL;
	spin_unlock_irqsave(&cpu_buf->lock);
}

void profiler_control_trace(int onoff)
{
	int core;

	for (core = 0; core < num_cores; core++) {
		struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(core);

		cpu_buf->tracing = onoff;
		if (onoff) {
			printk("Enable tracing on %d\n", core);
		} else {
			printk("Disable tracing on %d\n", core);
			profiler_cpubuf_flushone(core, 0);
		}
	}
}

void profiler_add_trace(uintptr_t pc)
{
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(core_id());

	if (profiler_percpu_ctx && cpu_buf->tracing)
		profiler_add_sample(cpu_buf, pc, nsec());
}

/* Format for samples:
 * first word:
 * high 8 bits is ee, which is an invalid address on amd64.
 * next 8 bits is protocol version
 * next 16 bits is unused, MBZ. Later, we can make it a packet type.
 * next 16 bits is core id
 * next 8 bits is unused
 * next 8 bits is # PCs following. This should be at least 1, for one EIP.
 *
 * second word is time in ns.
 *
 * Third and following words are PCs, there must be at least one of them.
 */
void profiler_add_backtrace(uintptr_t pc, uintptr_t fp)
{
	int cpu = core_id();
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(cpu);

	if (profiler_percpu_ctx && cpu_buf->tracing) {
		struct op_entry entry;
		struct block *b;
		uintptr_t bt_pcs[profiler_backtrace_depth];
		size_t n = backtrace_list(pc, fp, bt_pcs, profiler_backtrace_depth);

		b = profiler_cpu_buffer_write_reserve(cpu_buf, &entry, n);
		if (likely(b)) {
			entry.sample->hdr = profiler_create_header(cpu, n);
			entry.sample->event = nsec();
			profiler_cpu_buffer_add_data(&entry, bt_pcs, n);
		}
	}
}

void profiler_add_userpc(uintptr_t pc)
{
	int cpu = core_id();
	struct profiler_cpu_context *cpu_buf = profiler_get_cpu_ctx(cpu);

	if (profiler_percpu_ctx && cpu_buf->tracing) {
		struct op_entry entry;
		struct block *b = profiler_cpu_buffer_write_reserve(cpu_buf,
															&entry, 1);

		if (likely(b)) {
			entry.sample->hdr = profiler_create_header(cpu, 1);
			entry.sample->event = nsec();
			profiler_cpu_buffer_add_data(&entry, &pc, 1);
		}
	}
}

void profiler_add_hw_sample(struct hw_trapframe *hw_tf)
{
	if (in_kernel(hw_tf))
		profiler_add_backtrace(get_hwtf_pc(hw_tf), get_hwtf_fp(hw_tf));
	else
		profiler_add_userpc(get_hwtf_pc(hw_tf));
}

int profiler_size(void)
{
	return qlen(profiler_queue);
}

int profiler_read(void *va, int n)
{
	return qread(profiler_queue, va, n);
}
