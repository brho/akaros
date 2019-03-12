/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <ros/profiler_records.h>
#include <arch/time.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <atomic.h>
#include <kthread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <smp.h>
#include <time.h>
#include <circular_buffer.h>
#include <umem.h>
#include <profiler.h>
#include <kprof.h>
#include <ros/procinfo.h>
#include <init.h>

#define KTRACE_BUFFER_SIZE (128 * 1024)
#define TRACE_PRINTK_BUFFER_SIZE (8 * 1024)

enum {
	Kprofdirqid = 0,
	Kprofdataqid,
	Kprofctlqid,
	Kptracectlqid,
	Kptraceqid,
	Kprintxqid,
	Kmpstatqid,
	Kmpstatrawqid,
};

struct trace_printk_buffer {
	atomic_t in_use;
	char buffer[TRACE_PRINTK_BUFFER_SIZE];
};

struct kprof {
	qlock_t lock;
	bool mpstat_ipi;
	bool profiling;
	bool opened;
};

struct dev kprofdevtab;
struct dirtab kproftab[] = {
	{".",		{Kprofdirqid, 0, QTDIR},0,	DMDIR|0550},
	{"kpdata",	{Kprofdataqid},		0,	0600},
	{"kpctl",	{Kprofctlqid},		0,	0600},
	{"kptrace_ctl",	{Kptracectlqid},	0,	0660},
	{"kptrace",	{Kptraceqid},		0,	0600},
	{"kprintx",	{Kprintxqid},		0,	0600},
	{"mpstat",	{Kmpstatqid},		0,	0600},
	{"mpstat-raw",	{Kmpstatrawqid},	0,	0600},
};

static struct kprof kprof;
static bool ktrace_init_done = FALSE;
static spinlock_t ktrace_lock = SPINLOCK_INITIALIZER_IRQSAVE;
static struct circular_buffer ktrace_data;
static char ktrace_buffer[KTRACE_BUFFER_SIZE];
static char kprof_control_usage[128];

static size_t mpstat_len(void)
{
	size_t each_row = 7 + NR_CPU_STATES * 26;

	return each_row * (num_cores + 1) + 1;
}

static size_t mpstatraw_len(void)
{
	size_t header_row = 27 + NR_CPU_STATES * 7 + 1;
	size_t cpu_row = 7 + NR_CPU_STATES * 17;

	return header_row + cpu_row * num_cores + 1;
}

static char *devname(void)
{
	return kprofdevtab.name;
}

static struct chan *kprof_attach(char *spec)
{
	return devattach(devname(), spec);
}

/* Start collecting samples from perf events into the profiler.
 *
 * This command only runs if the user successfully opened kpctl, which gives
 * them a profiler (the global profiler, for now). */
static void kprof_start_profiler(void)
{
	ERRSTACK(1);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (!kprof.profiling) {
		profiler_start();
		kprof.profiling = TRUE;
	}
	poperror();
	qunlock(&kprof.lock);
}

/* Stops collecting samples from perf events.
 *
 * This command only runs if the user successfully opened kpctl, which gives
 * them a profiler (the global profiler, for now). */
static void kprof_stop_profiler(void)
{
	ERRSTACK(1);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (kprof.profiling) {
		profiler_stop();
		kprof.profiling = FALSE;
	}
	poperror();
	qunlock(&kprof.lock);
}

/* Makes each core flush its results into the profiler queue.  You can do this
 * while the profiler is still running.  However, this does not hang up the
 * queue, so reads on kpdata will block. */
static void kprof_flush_profiler(void)
{
	ERRSTACK(1);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (kprof.profiling)
		profiler_trace_data_flush();
	poperror();
	qunlock(&kprof.lock);
}

static void kprof_init(void)
{
	profiler_init();

	qlock_init(&kprof.lock);
	kprof.profiling = FALSE;
	kprof.opened = FALSE;

	for (int i = 0; i < ARRAY_SIZE(kproftab); i++)
		kproftab[i].length = 0;

	kprof.mpstat_ipi = TRUE;
	kproftab[Kmpstatqid].length = mpstat_len();
	kproftab[Kmpstatrawqid].length = mpstatraw_len();

	strlcpy(kprof_control_usage, "start|stop|flush",
	        sizeof(kprof_control_usage));
	profiler_append_configure_usage(kprof_control_usage,
	                                sizeof(kprof_control_usage));
}

static void kprof_shutdown(void)
{
}

static struct walkqid *kprof_walk(struct chan *c, struct chan *nc, char **name,
                                  unsigned int nname)
{
	return devwalk(c, nc, name, nname, kproftab, ARRAY_SIZE(kproftab),
		       devgen);
}

static size_t kprof_profdata_size(void)
{
	return profiler_size();
}

static long kprof_profdata_read(void *dest, long size, int64_t off)
{
	return profiler_read(dest, size);
}

static size_t kprof_stat(struct chan *c, uint8_t *db, size_t n)
{
	kproftab[Kprofdataqid].length = kprof_profdata_size();
	kproftab[Kptraceqid].length = kprof_tracedata_size();

	return devstat(c, db, n, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static struct chan *kprof_open(struct chan *c, int omode)
{
	if (c->qid.type & QTDIR) {
		if (openmode(omode) != O_READ)
			error(EPERM, ERROR_FIXME);
	}
	switch ((int) c->qid.path) {
	case Kprofctlqid:
		/* We have one global profiler.  Only one FD may be opened at a
		 * time for it.  If we ever have separate profilers, we can
		 * create the profiler here, and every open would get a separate
		 * instance. */
		qlock(&kprof.lock);
		if (kprof.opened) {
			qunlock(&kprof.lock);
			error(EBUSY, "Global profiler is already open");
		}
		kprof.opened = TRUE;
		/* TODO: have a creation function for a non-global profiler */
		profiler_setup();
		qunlock(&kprof.lock);
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void kprof_close(struct chan *c)
{
	if (c->flag & COPEN) {
		switch ((int) c->qid.path) {
		case Kprofctlqid:
			kprof_stop_profiler();
			qlock(&kprof.lock);
			profiler_cleanup();
			kprof.opened = FALSE;
			qunlock(&kprof.lock);
			break;
		}
	}
}

static long mpstat_read(void *va, long n, int64_t off)
{
	size_t bufsz = mpstat_len();
	char *buf = kmalloc(bufsz, MEM_WAIT);
	int len = 0;
	struct per_cpu_info *pcpui;
	uint64_t cpu_total;
	struct timespec ts;

	/* the IPI interferes with other cores, might want to disable that. */
	if (kprof.mpstat_ipi)
		send_broadcast_ipi(I_POKE_CORE);

	len += snprintf(buf + len, bufsz - len, "  CPU: ");
	for (int j = 0; j < NR_CPU_STATES; j++)
		len += snprintf(buf + len, bufsz - len, "%23s%s",
				cpu_state_names[j],
				j != NR_CPU_STATES - 1 ? " " : "  \n");

	for (int i = 0; i < num_cores; i++) {
		pcpui = &per_cpu_info[i];
		cpu_total = 0;
		len += snprintf(buf + len, bufsz - len, "%5d: ", i);
		for (int j = 0; j < NR_CPU_STATES; j++)
			cpu_total += pcpui->state_ticks[j];
		cpu_total = MAX(cpu_total, 1);	/* for the divide later */
		for (int j = 0; j < NR_CPU_STATES; j++) {
			ts = tsc2timespec(pcpui->state_ticks[j]);
			len += snprintf(buf + len, bufsz - len,
					"%10d.%06d (%3d%%)%s",
			                ts.tv_sec, ts.tv_nsec / 1000,
					MIN((pcpui->state_ticks[j] * 100) /
					    cpu_total, 100),
			                j != NR_CPU_STATES - 1 ? ", " : " \n");
		}
	}
	n = readstr(off, va, n, buf);
	kfree(buf);
	return n;
}

static long mpstatraw_read(void *va, long n, int64_t off)
{
	size_t bufsz = mpstatraw_len();
	char *buf = kmalloc(bufsz, MEM_WAIT);
	int len = 0;
	struct per_cpu_info *pcpui;

	/* could spit it all out in binary, though then it'd be harder to
	 * process the data across a mnt (if we export #K).  probably not a big
	 * deal. */

	/* header line: version, num_cores, tsc freq, state names */
	len += snprintf(buf + len, bufsz - len, "v%03d %5d %16llu", 1,
			num_cores, __proc_global_info.tsc_freq);
	for (int j = 0; j < NR_CPU_STATES; j++)
		len += snprintf(buf + len, bufsz - len, " %6s",
				cpu_state_names[j]);
	len += snprintf(buf + len, bufsz - len, "\n");

	for (int i = 0; i < num_cores; i++) {
		pcpui = &per_cpu_info[i];
		len += snprintf(buf + len, bufsz - len, "%5d: ", i);
		for (int j = 0; j < NR_CPU_STATES; j++) {
			len += snprintf(buf + len, bufsz - len, "%16llx%s",
			                pcpui->state_ticks[j],
			                j != NR_CPU_STATES - 1 ? " " : "\n");
		}
	}
	n = readstr(off, va, n, buf);
	kfree(buf);
	return n;
}

static size_t kprof_read(struct chan *c, void *va, size_t n, off64_t off)
{
	uint64_t w, *bp;
	char *a, *ea;
	uintptr_t offset = off;
	uint64_t pc;

	switch ((int) c->qid.path) {
	case Kprofdirqid:
		return devdirread(c, va, n, kproftab, ARRAY_SIZE(kproftab),
				  devgen);
	case Kprofdataqid:
		n = kprof_profdata_read(va, n, off);
		break;
	case Kptraceqid:
		n = kprof_tracedata_read(va, n, off);
		break;
	case Kprintxqid:
		n = readstr(offset, va, n, printx_on ? "on" : "off");
		break;
	case Kmpstatqid:
		n = mpstat_read(va, n, offset);
		break;
	case Kmpstatrawqid:
		n = mpstatraw_read(va, n, offset);
		break;
	default:
		n = 0;
		break;
	}
	return n;
}

static size_t kprof_write(struct chan *c, void *a, size_t n, off64_t unused)
{
	ERRSTACK(1);
	struct cmdbuf *cb = parsecmd(a, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	switch ((int) c->qid.path) {
	case Kprofctlqid:
		if (cb->nf < 1)
			error(EFAIL, kprof_control_usage);
		if (profiler_configure(cb))
			break;
		if (!strcmp(cb->f[0], "start")) {
			kprof_start_profiler();
		} else if (!strcmp(cb->f[0], "flush")) {
			kprof_flush_profiler();
		} else if (!strcmp(cb->f[0], "stop")) {
			kprof_stop_profiler();
		} else {
			error(EFAIL, kprof_control_usage);
		}
		break;
	case Kptracectlqid:
		if (cb->nf < 1)
			error(EFAIL, "Bad kptrace_ctl option (reset)");
		if (!strcmp(cb->f[0], "clear")) {
			spin_lock_irqsave(&ktrace_lock);
			circular_buffer_clear(&ktrace_data);
			spin_unlock_irqsave(&ktrace_lock);
		}
		break;
	case Kptraceqid:
		if (a && (n > 0)) {
			char *uptr = user_strdup_errno(current, a, n);

			if (uptr) {
				trace_printk("%s", uptr);
				user_memdup_free(current, uptr);
			} else {
				n = -1;
			}
		}
		break;
	case Kprintxqid:
		if (!strncmp(a, "on", 2))
			set_printx(1);
		else if (!strncmp(a, "off", 3))
			set_printx(0);
		else if (!strncmp(a, "toggle", 6))
			set_printx(2);
		else
			error(EFAIL, "Invalid option to Kprintx %s\n", a);
		break;
	case Kmpstatqid:
	case Kmpstatrawqid:
		if (cb->nf < 1)
			error(EFAIL, "Bad mpstat option (reset|ipi|on|off)");
		if (!strcmp(cb->f[0], "reset")) {
			for (int i = 0; i < num_cores; i++)
				reset_cpu_state_ticks(i);
		} else if (!strcmp(cb->f[0], "on")) {
			/* TODO: enable the ticks */ ;
		} else if (!strcmp(cb->f[0], "off")) {
			/* TODO: disable the ticks */ ;
		} else if (!strcmp(cb->f[0], "ipi")) {
			if (cb->nf < 2)
				error(EFAIL, "Need another arg: ipi [on|off]");
			if (!strcmp(cb->f[1], "on"))
				kprof.mpstat_ipi = TRUE;
			else if (!strcmp(cb->f[1], "off"))
				kprof.mpstat_ipi = FALSE;
			else
				error(EFAIL, "ipi [on|off]");
		} else {
			error(EFAIL, "Bad mpstat option (reset|ipi|on|off)");
		}
		break;
	default:
		error(EBADFD, ERROR_FIXME);
	}
	kfree(cb);
	poperror();
	return n;
}

size_t kprof_tracedata_size(void)
{
	return circular_buffer_size(&ktrace_data);
}

size_t kprof_tracedata_read(void *data, size_t size, size_t offset)
{
	spin_lock_irqsave(&ktrace_lock);
	if (likely(ktrace_init_done))
		size = circular_buffer_read(&ktrace_data, data, size, offset);
	else
		size = 0;
	spin_unlock_irqsave(&ktrace_lock);

	return size;
}

void kprof_dump_data(void)
{
	void *buf;
	size_t len = kprof_tracedata_size();

	buf = kmalloc(len, MEM_WAIT);
	kprof_tracedata_read(buf, len, 0);
	printk("%s", buf);
	kfree(buf);
}

void kprof_tracedata_write(const char *pretty_buf, size_t len)
{
	spin_lock_irqsave(&ktrace_lock);
	if (unlikely(!ktrace_init_done)) {
		circular_buffer_init(&ktrace_data, sizeof(ktrace_buffer),
		                     ktrace_buffer);
		ktrace_init_done = TRUE;
	}
	circular_buffer_write(&ktrace_data, pretty_buf, len);
	spin_unlock_irqsave(&ktrace_lock);
}

static struct trace_printk_buffer *kprof_get_printk_buffer(void)
{
	static struct trace_printk_buffer boot_tpb;
	static struct trace_printk_buffer *cpu_tpbs;
	static atomic_t alloc_done;

	if (unlikely(booting))
		return &boot_tpb;
	if (unlikely(!cpu_tpbs)) {
		/* Poor man per-CPU data structure. I really do no like
		 * littering global data structures with module specific data.
		 * We cannot take the ktrace_lock to protect the kzmalloc()
		 * call, as that might trigger printk()s, and we would reenter
		 * here.  Let only one core into the kzmalloc() path, and let
		 * the others get the boot_tpb until finished. */
		if (!atomic_cas(&alloc_done, 0, 1))
			return &boot_tpb;
		cpu_tpbs = kzmalloc(num_cores *
				    sizeof(struct trace_printk_buffer), 0);
	}

	return cpu_tpbs + core_id_early();
}

void trace_vprintk(const char *fmt, va_list args)
{
	struct print_buf {
		char *ptr;
		char *top;
	};

	void emit_print_buf_str(struct print_buf *pb, const char *str,
				ssize_t size)
	{
		if (size < 0) {
			for (; *str && (pb->ptr < pb->top); str++)
				*(pb->ptr++) = *str;
		} else {
			for (; (size > 0) && (pb->ptr < pb->top); str++, size--)
				*(pb->ptr++) = *str;
		}
	}

	static const size_t bufsz = TRACE_PRINTK_BUFFER_SIZE;
	static const size_t usr_bufsz = (3 * bufsz) / 8;
	static const size_t kp_bufsz = bufsz - usr_bufsz;
	struct trace_printk_buffer *tpb = kprof_get_printk_buffer();
	struct timespec ts_now = { 0, 0 };
	struct print_buf pb;
	char *usrbuf = tpb->buffer, *kpbuf = tpb->buffer + usr_bufsz;
	const char *utop, *uptr;
	char hdr[64];

	if (!atomic_cas(&tpb->in_use, 0, 1))
		return;
	if (likely(__proc_global_info.tsc_freq))
		ts_now = tsc2timespec(read_tsc());
	snprintf(hdr, sizeof(hdr), "[%lu.%09lu]:cpu%d: ", ts_now.tv_sec,
	         ts_now.tv_nsec, core_id_early());

	pb.ptr = usrbuf + vsnprintf(usrbuf, usr_bufsz, fmt, args);
	pb.top = usrbuf + usr_bufsz;

	if (pb.ptr[-1] != '\n')
		emit_print_buf_str(&pb, "\n", 1);
	/* snprintf null terminates the buffer, and does not count that as part
	 * of the len.  If we maxed out the buffer, let's make sure it has a \n.
	 */
	if (pb.ptr == pb.top)
		pb.ptr[-1] = '\n';
	utop = pb.ptr;

	pb.ptr = kpbuf;
	pb.top = kpbuf + kp_bufsz;
	for (uptr = usrbuf; uptr < utop;) {
		const char *nlptr = memchr(uptr, '\n', utop - uptr);

		if (nlptr == NULL)
			nlptr = utop;
		emit_print_buf_str(&pb, hdr, -1);
		emit_print_buf_str(&pb, uptr, (nlptr - uptr) + 1);
		uptr = nlptr + 1;
	}
	kprof_tracedata_write(kpbuf, pb.ptr - kpbuf);
	atomic_set(&tpb->in_use, 0);
}

void trace_printk(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_vprintk(fmt, args);
	va_end(args);
}

struct dev kprofdevtab __devtab = {
	.name = "kprof",

	.reset = devreset,
	.init = kprof_init,
	.shutdown = kprof_shutdown,
	.attach = kprof_attach,
	.walk = kprof_walk,
	.stat = kprof_stat,
	.open = kprof_open,
	.create = devcreate,
	.close = kprof_close,
	.read = kprof_read,
	.bread = devbread,
	.write = kprof_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
};
