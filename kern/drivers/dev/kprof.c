/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

// get_fn_name is slowing down the kprocread
// 	have an array of translated fns
// 	or a "next" iterator, since we're walking in order
//
// irqsave locks
//
// kprof struct should be a ptr, have them per core
// 		we'll probably need to track the length still, so userspace knows how
// 		big it is
//
// 		will also want more files in the kprof dir for each cpu or something
//
// maybe don't use slot 0 and 1 as total and 'not kernel' ticks
//
// fix the failed assert XXX

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>
#include <oprofile.h>

#define LRES	3		/* log of PC resolution */
#define CELLSIZE	8	/* sizeof of count cell */

struct kprof
{
	uintptr_t	minpc;
	uintptr_t	maxpc;
	int	nbuf;
	int	time;
	uint64_t	*buf;	/* keep in sync with cellsize */
	size_t		buf_sz;
	spinlock_t lock;
	struct queue *systrace;
	bool		mpstat_ipi;
};
struct kprof kprof;

/* output format. Nice fixed size. That makes it seekable.
 * small subtle bit here. You have to convert offset FROM FORMATSIZE units
 * to CELLSIZE units in a few places.
 */
char *outformat = "%016llx %29.29s %016llx\n";
#define FORMATSIZE 64
enum{
	Kprofdirqid = 0,
	Kprofdataqid,
	Kprofctlqid,
	Kprofoprofileqid,
	Kptraceqid,
	Kprintxqid,
	Kmpstatqid,
	Kmpstatrawqid,
};

struct dirtab kproftab[]={
	{".",		{Kprofdirqid, 0, QTDIR},0,	DMDIR|0550},
	{"kpdata",	{Kprofdataqid},		0,	0600},
	{"kpctl",	{Kprofctlqid},		0,	0600},
	{"kpoprofile",	{Kprofoprofileqid},	0,	0600},
	{"kptrace",	{Kptraceqid},		0,	0600},
	{"kprintx",	{Kprintxqid},		0,	0600},
	{"mpstat",	{Kmpstatqid},		0,	0600},
	{"mpstat-raw",	{Kmpstatrawqid},		0,	0600},
};

static size_t mpstatraw_len(void);
static size_t mpstat_len(void);

static struct alarm_waiter *oprof_alarms;
static unsigned int oprof_timer_period = 1000;

static void oprof_alarm_handler(struct alarm_waiter *waiter,
                                struct hw_trapframe *hw_tf)
{
	int coreid = core_id();
	struct timer_chain *tchain = &per_cpu_info[coreid].tchain;
	if (in_kernel(hw_tf))
		oprofile_add_backtrace(get_hwtf_pc(hw_tf), get_hwtf_fp(hw_tf));
	else
		oprofile_add_userpc(get_hwtf_pc(hw_tf));
	reset_alarm_rel(tchain, waiter, oprof_timer_period);
}

static struct chan*
kprofattach(char *spec)
{
	// Did we initialise completely?
	if ( !(oprof_alarms && kprof.buf && kprof.systrace) )
		error(Enomem);

	return devattach('K', spec);
}

static void
kproftimer(uintptr_t pc)
{
	if(kprof.time == 0)
		return;

	/*
	 * if the pc corresponds to the idle loop, don't consider it.

	if(m->inidle)
		return;
	 */
	/*
	 *  if the pc is coming out of spllo or splx,
	 *  use the pc saved when we went splhi.

	if(pc>=PTR2UINT(spllo) && pc<=PTR2UINT(spldone))
		pc = m->splpc;
	 */

//	ilock(&kprof);
	/* this is weird. What we do is assume that all the time since the last
	 * measurement went into this PC. It's the best
	 * we can do I suppose. And we are sampling at 1 ms. for now.
	 * better ideas welcome.
	 */
	kprof.buf[0] += 1; //Total count of ticks.
	if(kprof.minpc<=pc && pc<kprof.maxpc){
		pc -= kprof.minpc;
		pc >>= LRES;
		kprof.buf[pc] += 1;
	}else
		kprof.buf[1] += 1; // Why?
//	iunlock(&kprof);
}

static void setup_timers(void)
{
	void kprof_alarm(struct alarm_waiter *waiter, struct hw_trapframe *hw_tf)
	{
		struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
		kproftimer(get_hwtf_pc(hw_tf));
		set_awaiter_rel(waiter, 1000);
		set_alarm(tchain, waiter);
	}
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter *waiter = kmalloc(sizeof(struct alarm_waiter), 0);
	init_awaiter_irq(waiter, kprof_alarm);
	set_awaiter_rel(waiter, 1000);
	set_alarm(tchain, waiter);
}

static void kprofinit(void)
{
	uint32_t n;

	static_assert(CELLSIZE == sizeof kprof.buf[0]); // kprof size

	/* allocate when first used */
	kprof.minpc = KERN_LOAD_ADDR;
	kprof.maxpc = (uintptr_t) &etext;
	kprof.nbuf = (kprof.maxpc-kprof.minpc) >> LRES;
	n = kprof.nbuf*CELLSIZE;
	kprof.buf = kzmalloc(n, KMALLOC_WAIT);
	if (kprof.buf)
		kprof.buf_sz = n;

	/* no, i'm not sure how we should do this yet. */
	int alloc_cpu_buffers(void);
	alloc_cpu_buffers();
	oprof_alarms = kzmalloc(sizeof(struct alarm_waiter) * num_cores,
	                        KMALLOC_WAIT);
	if (!oprof_alarms)
		error(Enomem);

	for (int i = 0; i < num_cores; i++)
		init_awaiter_irq(&oprof_alarms[i], oprof_alarm_handler);

	kprof.systrace = qopen(2 << 20, 0, 0, 0);
	if (!kprof.systrace) {
		printk("systrace allocate failed. No system call tracing\n");
	}
	kprof.mpstat_ipi = TRUE;

	kproftab[Kprofdataqid].length = kprof.nbuf * FORMATSIZE;
	kproftab[Kmpstatqid].length = mpstat_len();
	kproftab[Kmpstatrawqid].length = mpstatraw_len();
}

static void kprofshutdown(void)
{
	kfree(oprof_alarms); oprof_alarms = NULL;
	kfree(kprof.buf); kprof.buf = NULL;
	qfree(kprof.systrace); kprof.systrace = NULL;
	void free_cpu_buffers(void);
	free_cpu_buffers();
}

static struct walkqid*
kprofwalk(struct chan *c, struct chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static int
kprofstat(struct chan *c, uint8_t *db, int n)
{
	kproftab[Kprofoprofileqid].length = oproflen();
	if (kprof.systrace)
		kproftab[Kptraceqid].length = qlen(kprof.systrace);
	else
		kproftab[Kptraceqid].length = 0;

	return devstat(c, db, n, kproftab, ARRAY_SIZE(kproftab), devgen);
}

static struct chan*
kprofopen(struct chan *c, int omode)
{
	if(c->qid.type & QTDIR){
		if(openmode(omode) != O_READ)
			error(Eperm);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
kprofclose(struct chan*unused)
{
}

static size_t mpstat_len(void)
{
	size_t each_row = 7 + NR_CPU_STATES * 26;
	return each_row * (num_cores + 1) + 1;
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

static size_t mpstatraw_len(void)
{
	size_t header_row = 27 + NR_CPU_STATES * 7 + 1;
	size_t cpu_row = 7 + NR_CPU_STATES * 17;
	return header_row + cpu_row * num_cores + 1;
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

static long
kprofread(struct chan *c, void *va, long n, int64_t off)
{
	uint64_t w, *bp;
	char *a, *ea;
	uintptr_t offset = off;
	uint64_t pc;
	int snp_ret, ret = 0;

	switch((int)c->qid.path){
	case Kprofdirqid:
		return devdirread(c, va, n, kproftab, ARRAY_SIZE(kproftab), devgen);

	case Kprofdataqid:

		if (n < FORMATSIZE){
			n = 0;
			break;
		}
		a = va;
		ea = a + n;

		/* we check offset later before deref bp.  offset / FORMATSIZE is how
		 * many entries we're skipping/offsetting. */
		bp = kprof.buf + offset/FORMATSIZE;
		pc = kprof.minpc + ((offset/FORMATSIZE)<<LRES);
		while((a < ea) && (n >= FORMATSIZE)){
			/* what a pain. We need to manage the
			 * fact that the *prints all make room for
			 * \0
			 */
			char print[FORMATSIZE+1];
			char *name;
			int amt_read;

			if (pc >= kprof.maxpc)
				break;
			/* pc is also our exit for bp.  should be in lockstep */
			// XXX this assert fails, fix it!
			//assert(bp < kprof.buf + kprof.nbuf);
			/* do not attempt to filter these results based on w < threshold.
			 * earlier, we computed bp/pc based on assuming a full-sized file,
			 * and skipping entries will result in read() calls thinking they
			 * received earlier entries when they really received later ones.
			 * imagine a case where there are 1000 skipped items, and read()
			 * asks for chunks of 32.  it'll get chunks of the next 32 valid
			 * items, over and over (1000/32 times). */
			w = *bp++;

			if (pc == kprof.minpc)
				name = "Total";
			else if (pc == kprof.minpc + 8)
				name = "User";
			else
				name = get_fn_name(pc);

			snp_ret = snprintf(print, sizeof(print), outformat, pc, name, w);
			assert(snp_ret == FORMATSIZE);
			if ((pc != kprof.minpc) && (pc != kprof.minpc + 8))
				kfree(name);

			amt_read = readmem(offset % FORMATSIZE, a, n, print, FORMATSIZE);
			offset = 0;	/* future loops have no offset */

			a += amt_read;
			n -= amt_read;
			ret += amt_read;

			pc += (1 << LRES);
		}
		n = ret;
		break;
	case Kprofoprofileqid:
		n = oprofread(va,n);
		break;
	case Kptraceqid:
		if (kprof.systrace) {
			printd("Kptraceqid: kprof.systrace %p len %p\n", kprof.systrace, qlen(kprof.systrace));
			if (qlen(kprof.systrace) > 0)
				n = qread(kprof.systrace, va, n);
			else
				n = 0;
		} else
			error("no systrace queue");
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

static void kprof_clear(struct kprof *kp)
{
	spin_lock(&kp->lock);
	memset(kp->buf, 0, kp->buf_sz);
	spin_unlock(&kp->lock);
}

static void manage_oprof_timer(int coreid, struct cmdbuf *cb)
{
	struct timer_chain *tchain = &per_cpu_info[coreid].tchain;
	struct alarm_waiter *waiter = &oprof_alarms[coreid];
	if (!strcmp(cb->f[2], "on")) {
		/* pcpu waiters already inited.  will set/reset each time (1 ms
		 * default). */
		reset_alarm_rel(tchain, waiter, oprof_timer_period);
	} else if (!strcmp(cb->f[2], "off")) {
		/* since the alarm handler runs and gets reset within IRQ context, then
		 * we should never fail to cancel the alarm if it was already running
		 * (tchain locks synchronize us).  but it might not be set at all, which
		 * is fine. */
		unset_alarm(tchain, waiter);
	} else {
		error("optimer needs on|off");
	}
}

static long
kprofwrite(struct chan *c, void *a, long n, int64_t unused)
{
	ERRSTACK(1);
	uintptr_t pc;
	struct cmdbuf *cb;
	char *ctlstring = "startclr|start|stop|clear|opstart|opstop|optimer";
	cb = parsecmd(a, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	switch((int)(c->qid.path)){
	case Kprofctlqid:
		if (cb->nf < 1)
			error(ctlstring);

		/* Kprof: a "which kaddr are we at when the timer goes off".  not used
		 * much anymore */
		if (!strcmp(cb->f[0], "startclr")) {
			kprof_clear(&kprof);
			kprof.time = 1;
		} else if (!strcmp(cb->f[0], "start")) {
			kprof.time = 1;
			/* this sets up the timer on the *calling* core! */
			setup_timers();
		} else if (!strcmp(cb->f[0], "stop")) {
			/* TODO: stop the timers! */
			kprof.time = 0;
		} else if (!strcmp(cb->f[0], "clear")) {
			kprof_clear(&kprof);

		/* oprof: samples and traces using oprofile */
		} else if (!strcmp(cb->f[0], "optimer")) {
			if (cb->nf < 3)
				error("optimer [<0|1|..|n|all> <on|off>] [period USEC]");
			if (!strcmp(cb->f[1], "period")) {
				oprof_timer_period = strtoul(cb->f[2], 0, 10);
			} else if (!strcmp(cb->f[1], "all")) {
				for (int i = 0; i < num_cores; i++)
					manage_oprof_timer(i, cb);
			} else {
				int pcoreid = strtoul(cb->f[1], 0, 10);
				if (pcoreid >= num_cores)
					error("no such coreid %d", pcoreid);
				manage_oprof_timer(pcoreid, cb);
			}
		} else if (!strcmp(cb->f[0], "opstart")) {
			oprofile_control_trace(1);
		} else if (!strcmp(cb->f[0], "opstop")) {
			oprofile_control_trace(0);
		} else {
			error(ctlstring);
		}
		break;

		/* The format is a long as text. We strtoul, and jam it into the
		 * trace buffer.
		 */
	case Kprofoprofileqid:
		pc = strtoul(a, 0, 0);
		oprofile_add_trace(pc);
		break;
	case Kprintxqid:
		if (!strncmp(a, "on", 2))
			set_printx(1);
		else if (!strncmp(a, "off", 3))
			set_printx(0);
		else if (!strncmp(a, "toggle", 6))	/* why not. */
			set_printx(2);
		else
			error("invalid option to Kprintx %s\n", a);
		break;
	case Kmpstatqid:
	case Kmpstatrawqid:
		if (cb->nf < 1)
			error("mpstat bad option (reset|ipi|on|off)");
		if (!strcmp(cb->f[0], "reset")) {
			for (int i = 0; i < num_cores; i++)
				reset_cpu_state_ticks(i);
		} else if (!strcmp(cb->f[0], "on")) {
			/* TODO: enable the ticks */ ;
		} else if (!strcmp(cb->f[0], "off")) {
			/* TODO: disable the ticks */ ;
		} else if (!strcmp(cb->f[0], "ipi")) {
			if (cb->nf < 2)
				error("need another arg: ipi [on|off]");
			if (!strcmp(cb->f[1], "on"))
				kprof.mpstat_ipi = TRUE;
			else if (!strcmp(cb->f[1], "off"))
				kprof.mpstat_ipi = FALSE;
			else
				error("ipi [on|off]");
		} else {
			error("mpstat bad option (reset|ipi|on|off)");
		}
		break;
	default:
		error(Ebadusefd);
	}
	kfree(cb);
	poperror();
	return n;
}

void kprof_write_sysrecord(char *pretty_buf, size_t len)
{
	int wrote;
	if (kprof.systrace) {
		/* TODO: need qio work so we can simply add the buf as extra data */
		wrote = qiwrite(kprof.systrace, pretty_buf, len);
		/* based on the current queue settings, we only drop when we're running
		 * out of memory.  odds are, we won't make it this far. */
		if (wrote != len)
			printk("DROPPED %s", pretty_buf);
	}
}

void trace_printk(const char *fmt, ...)
{
	va_list ap;
	struct timespec ts_now;
	size_t bufsz = 160;	/* 2x terminal width */
	size_t len = 0;
	char *buf = kmalloc(bufsz, 0);

	if (!buf)
		return;
	tsc2timespec(read_tsc(), &ts_now);
	len += snprintf(buf + len, bufsz - len, "[%7d.%09d] /* ", ts_now.tv_sec,
	                ts_now.tv_nsec);
	va_start(ap, fmt);
	len += vsnprintf(buf + len, bufsz - len, fmt, ap);
	va_start(ap, fmt);
	va_end(ap);
	len += snprintf(buf + len, bufsz - len, " */\n");
	va_start(ap, fmt);
	/* snprintf null terminates the buffer, and does not count that as part of
	 * the len.  if we maxed out the buffer, let's make sure it has a \n */
	if (len == bufsz - 1) {
		assert(buf[bufsz - 1] == '\0');
		buf[bufsz - 2] = '\n';
	}
	kprof_write_sysrecord(buf, len);
	kfree(buf);
}

struct dev kprofdevtab __devtab = {
	'K',
	"kprof",

	devreset,
	kprofinit,
	kprofshutdown,
	kprofattach,
	kprofwalk,
	kprofstat,
	kprofopen,
	devcreate,
	kprofclose,
	kprofread,
	devbread,
	kprofwrite,
	devbwrite,
	devremove,
	devwstat,
};
