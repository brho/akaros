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
#include <vfs.h>
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

#define KTRACE_BUFFER_SIZE (128 * 1024)
#define TRACE_PRINTK_BUFFER_SIZE (8 * 1024)

enum {
	Kprofdirqid = 0,
	Kprofdataqid,
	Kprofctlqid,
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
	struct alarm_waiter *alarms;
	bool mpstat_ipi;
	bool profiling;
	char *pdata;
	size_t psize;
};

struct dev kprofdevtab;
struct dirtab kproftab[] = {
	{".",			{Kprofdirqid,		0, QTDIR}, 0,	DMDIR|0550},
	{"kpdata",		{Kprofdataqid},		0,	0600},
	{"kpctl",		{Kprofctlqid},		0,	0600},
	{"kptrace",		{Kptraceqid},		0,	0600},
	{"kprintx",		{Kprintxqid},		0,	0600},
	{"mpstat",		{Kmpstatqid},		0,	0600},
	{"mpstat-raw",	{Kmpstatrawqid},	0,	0600},
};

static struct kprof kprof;
static bool ktrace_init_done = FALSE;
static spinlock_t ktrace_lock = SPINLOCK_INITIALIZER_IRQSAVE;
static struct circular_buffer ktrace_data;
static char ktrace_buffer[KTRACE_BUFFER_SIZE];
static int kprof_timer_period = 1000;

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

static void kprof_alarm_handler(struct alarm_waiter *waiter,
                                struct hw_trapframe *hw_tf)
{
	int coreid = core_id();
	struct timer_chain *tchain = &per_cpu_info[coreid].tchain;

	profiler_add_hw_sample(hw_tf, PROF_MKINFO(PROF_DOM_TIMER,
											  kprof_timer_period));
	reset_alarm_rel(tchain, waiter, kprof_timer_period);
}

static struct chan *kprof_attach(char *spec)
{
	if (!kprof.alarms)
		error(ENOMEM, ERROR_FIXME);

	return devattach(devname(), spec);
}

static void kprof_enable_timer(int coreid, int on_off)
{
	struct timer_chain *tchain = &per_cpu_info[coreid].tchain;
	struct alarm_waiter *waiter = &kprof.alarms[coreid];

	if (on_off) {
		/* Per CPU waiters already inited.  Will set/reset each time (1 ms
		 * default). */
		reset_alarm_rel(tchain, waiter, kprof_timer_period);
	} else {
		/* Since the alarm handler runs and gets reset within IRQ context, then
		 * we should never fail to cancel the alarm if it was already running
		 * (tchain locks synchronize us).  But it might not be set at all, which
		 * is fine. */
		unset_alarm(tchain, waiter);
	}
}

static void kprof_profdata_clear(void)
{
	kfree(kprof.pdata);
	kprof.pdata = NULL;
	kprof.psize = 0;
}

static void kprof_start_profiler(void)
{
	ERRSTACK(2);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (!kprof.profiling) {
		profiler_setup();
		if (waserror()) {
			kprof.profiling = FALSE;
			profiler_cleanup();
			nexterror();
		}
		profiler_control_trace(1);
		kprof.profiling = TRUE;
		kprof_profdata_clear();
		poperror();
	}
	poperror();
	qunlock(&kprof.lock);
}

static void kprof_fetch_profiler_data(void)
{
	size_t psize = kprof.psize + profiler_size();
	char *ndata = krealloc(kprof.pdata, psize, KMALLOC_WAIT);

	if (!ndata)
		error(ENOMEM, ERROR_FIXME);
	kprof.pdata = ndata;
	while (kprof.psize < psize) {
		size_t csize = profiler_read(kprof.pdata + kprof.psize,
									 psize - kprof.psize);

		if (csize == 0)
			break;
		kprof.psize += csize;
	}
}

static void kprof_stop_profiler(void)
{
	ERRSTACK(1);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (kprof.profiling) {
		profiler_control_trace(0);
		kprof_fetch_profiler_data();
		profiler_cleanup();

		kprof.profiling = FALSE;
	}
	poperror();
	qunlock(&kprof.lock);
}

static void kprof_flush_profiler(void)
{
	ERRSTACK(1);

	qlock(&kprof.lock);
	if (waserror()) {
		qunlock(&kprof.lock);
		nexterror();
	}
	if (kprof.profiling) {
		profiler_trace_data_flush();
		kprof_fetch_profiler_data();
	}
	poperror();
	qunlock(&kprof.lock);
}

static void kprof_init(void)
{
	int i;
	ERRSTACK(1);

	profiler_init();

	qlock_init(&kprof.lock);
	kprof.profiling = FALSE;
	kprof.pdata = NULL;
	kprof.psize = 0;

	kprof.alarms = kzmalloc(sizeof(struct alarm_waiter) * num_cores,
							KMALLOC_WAIT);
	if (!kprof.alarms)
		error(ENOMEM, ERROR_FIXME);
	if (waserror()) {
		kfree(kprof.alarms);
		kprof.alarms = NULL;
		nexterror();
	}
	for (i = 0; i < num_cores; i++)
		init_awaiter_irq(&kprof.alarms[i], kprof_alarm_handler);

	for (i = 0; i < ARRAY_SIZE(kproftab); i++)
		kproftab[i].length = 0;

	kprof.mpstat_ipi = TRUE;
	kproftab[Kmpstatqid].length = mpstat_len();
	kproftab[Kmpstatrawqid].length = mpstatraw_len();

	poperror();
}

static void kprof_shutdown(void)
{
	kprof_stop_profiler();
	kprof_profdata_clear();

	kfree(kprof.alarms);
	kprof.alarms = NULL;
}

static void kprofclear(void)
{
	qlock(&kprof.lock);
	kprof_profdata_clear();
	qunlock(&kprof.lock);
}

static struct walkqid *kprof_walk(struct chan *c, struct chan *nc, char **name,
								 int nname)
{
	return devwalk(c, nc, name, nname, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static size_t kprof_profdata_size(void)
{
	return kprof.pdata != NULL ? kprof.psize : profiler_size();
}

static long kprof_profdata_read(void *dest, long size, int64_t off)
{
	qlock(&kprof.lock);
	if (kprof.pdata && off < kprof.psize) {
		size = MIN(kprof.psize - off, size);
		memcpy(dest, kprof.pdata + off, size);
	} else {
		size = 0;
	}
	qunlock(&kprof.lock);

	return size;
}

static int kprof_stat(struct chan *c, uint8_t *db, int n)
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
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void kprof_close(struct chan *c)
{
}

static long mpstat_read(void *va, long n, int64_t off)
{
	size_t bufsz = mpstat_len();
	char *buf = kmalloc(bufsz, KMALLOC_WAIT);
	int len = 0;
	struct per_cpu_info *pcpui;
	uint64_t cpu_total;
	struct timespec ts;

	/* the IPI interferes with other cores, might want to disable that. */
	if (kprof.mpstat_ipi)
		send_broadcast_ipi(I_POKE_CORE);

	len += snprintf(buf + len, bufsz - len, "  CPU: ");
	for (int j = 0; j < NR_CPU_STATES; j++)
		len += snprintf(buf + len, bufsz - len, "%23s%s", cpu_state_names[j],
		                j != NR_CPU_STATES - 1 ? "   " : "  \n");

	for (int i = 0; i < num_cores; i++) {
		pcpui = &per_cpu_info[i];
		cpu_total = 0;
		len += snprintf(buf + len, bufsz - len, "%5d: ", i);
		for (int j = 0; j < NR_CPU_STATES; j++)
			cpu_total += pcpui->state_ticks[j];
		cpu_total = MAX(cpu_total, 1);	/* for the divide later */
		for (int j = 0; j < NR_CPU_STATES; j++) {
			tsc2timespec(pcpui->state_ticks[j], &ts);
			len += snprintf(buf + len, bufsz - len, "%10d.%06d (%3d%%)%s",
			                ts.tv_sec, ts.tv_nsec / 1000,
			                MIN((pcpui->state_ticks[j] * 100) / cpu_total, 100),
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
	char *buf = kmalloc(bufsz, KMALLOC_WAIT);
	int len = 0;
	struct per_cpu_info *pcpui;

	/* could spit it all out in binary, though then it'd be harder to process
	 * the data across a mnt (if we export #K).  probably not a big deal. */

	/* header line: version, num_cores, tsc freq, state names */
	len += snprintf(buf + len, bufsz - len, "v%03d %5d %16llu", 1, num_cores,
	                system_timing.tsc_freq);
	for (int j = 0; j < NR_CPU_STATES; j++)
		len += snprintf(buf + len, bufsz - len, " %6s", cpu_state_names[j]);
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

static long kprof_read(struct chan *c, void *va, long n, int64_t off)
{
	uint64_t w, *bp;
	char *a, *ea;
	uintptr_t offset = off;
	uint64_t pc;

	switch ((int) c->qid.path) {
	case Kprofdirqid:
		return devdirread(c, va, n, kproftab, ARRAY_SIZE(kproftab), devgen);
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

static void kprof_manage_timer(int coreid, struct cmdbuf *cb)
{
	if (!strcmp(cb->f[2], "on")) {
		kprof_enable_timer(coreid, 1);
	} else if (!strcmp(cb->f[2], "off")) {
		kprof_enable_timer(coreid, 0);
	} else {
		error(EFAIL, "timer needs on|off");
	}
}

static void kprof_usage_fail(void)
{
	static const char *ctlstring = "clear|start|stop|flush|timer";
	const char * const *cmds = profiler_configure_cmds();
	char msgbuf[128];

	strlcpy(msgbuf, ctlstring, sizeof(msgbuf));
	for (int i = 0; cmds[i]; i++) {
		strlcat(msgbuf, "|", sizeof(msgbuf));
		strlcat(msgbuf, cmds[i], sizeof(msgbuf));
	}

	error(EFAIL, msgbuf);
}

static long kprof_write(struct chan *c, void *a, long n, int64_t unused)
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
			kprof_usage_fail();
		if (profiler_configure(cb))
			break;
		if (!strcmp(cb->f[0], "clear")) {
			kprofclear();
		} else if (!strcmp(cb->f[0], "timer")) {
			if (cb->nf < 3)
				error(EFAIL, "timer {{all, N} {on, off}, period USEC}");
			if (!strcmp(cb->f[1], "period")) {
				kprof_timer_period = strtoul(cb->f[2], 0, 10);
			} else if (!strcmp(cb->f[1], "all")) {
				for (int i = 0; i < num_cores; i++)
					kprof_manage_timer(i, cb);
			} else {
				int pcoreid = strtoul(cb->f[1], 0, 10);

				if (pcoreid >= num_cores)
					error(EFAIL, "No such coreid %d", pcoreid);
				kprof_manage_timer(pcoreid, cb);
			}
		} else if (!strcmp(cb->f[0], "start")) {
			kprof_start_profiler();
		} else if (!strcmp(cb->f[0], "flush")) {
			kprof_flush_profiler();
		} else if (!strcmp(cb->f[0], "stop")) {
			kprof_stop_profiler();
		} else {
			kprof_usage_fail();
		}
		break;
	case Kprofdataqid:
		profiler_add_trace((uintptr_t) strtoul(a, 0, 0), 0);
		break;
	case Kptraceqid:
		if (a && (n > 0)) {
			char *uptr = user_strdup_errno(current, a, n);

			if (uptr) {
				trace_printk(false, "%s", uptr);
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

	if (unlikely(!num_cores))
		return &boot_tpb;
	if (unlikely(!cpu_tpbs)) {
		/* Poor man per-CPU data structure. I really do no like littering global
		 * data structures with module specific data.
		 * We cannot take the ktrace_lock to protect the kzmalloc() call, as
		 * that might trigger printk()s, and we would reenter here.
		 * Let only one core into the kzmalloc() path, and let the others get
		 * the boot_tpb until finished.
		 */
		if (!atomic_cas(&alloc_done, 0, 1))
			return &boot_tpb;
		cpu_tpbs = kzmalloc(num_cores * sizeof(struct trace_printk_buffer), 0);
	}

	return cpu_tpbs + core_id_early();
}

void trace_vprintk(bool btrace, const char *fmt, va_list args)
{
	struct print_buf {
		char *ptr;
		char *top;
	};

	void emit_print_buf_str(struct print_buf *pb, const char *str, ssize_t size)
	{
		if (size < 0) {
			for (; *str && (pb->ptr < pb->top); str++)
				*(pb->ptr++) = *str;
		} else {
			for (; (size > 0) && (pb->ptr < pb->top); str++, size--)
				*(pb->ptr++) = *str;
		}
	}

	void bt_print(void *opaque, const char *str)
	{
		struct print_buf *pb = (struct print_buf *) opaque;

		emit_print_buf_str(pb, "\t", 1);
		emit_print_buf_str(pb, str, -1);
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
	if (likely(system_timing.tsc_freq))
		tsc2timespec(read_tsc(), &ts_now);
	snprintf(hdr, sizeof(hdr), "[%lu.%09lu]:cpu%d: ", ts_now.tv_sec,
			 ts_now.tv_nsec, core_id_early());

	pb.ptr = usrbuf + vsnprintf(usrbuf, usr_bufsz, fmt, args);
	pb.top = usrbuf + usr_bufsz;

	if (pb.ptr[-1] != '\n')
		emit_print_buf_str(&pb, "\n", 1);
	if (btrace) {
		emit_print_buf_str(&pb, "\tBacktrace:\n", -1);
		gen_backtrace(bt_print, &pb);
	}
	/* snprintf null terminates the buffer, and does not count that as part of
	 * the len.  If we maxed out the buffer, let's make sure it has a \n.
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

void trace_printk(bool btrace, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_vprintk(btrace, fmt, args);
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
