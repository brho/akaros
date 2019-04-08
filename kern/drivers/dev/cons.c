/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <arch/arch.h>
#include <atomic.h>
#include <env.h>
#include <error.h>
#include <event.h>
#include <kmalloc.h>
#include <ns.h>
#include <ros/fs.h>
#include <ros/procinfo.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>

#if 0
void (*consdebug) (void) = NULL;
#endif
void (*screenputs)(const char *, int) = cputbuf;

struct queue *kbdq;       /* unprocessed console input */
struct queue *lineq;      /* processed console input */
struct queue *serialoq;   /* serial console output */
struct queue *kprintoq;   /* console output, for /dev/kprint */
atomic_t kprintinuse = 0; /* test and set whether /dev/kprint is open */
int iprintscreenputs = 1;
int keepbroken = 1;

static bool cons_has_init = FALSE;
struct queue *cons_q; /* Akaros cons input: keyboard, serial, etc */
spinlock_t cons_q_lock = SPINLOCK_INITIALIZER;
struct fdtap_slist cons_q_fd_taps = SLIST_HEAD_INITIALIZER(cons_q_fd_taps);

static uint8_t logbuffer[1 << 20];
static int index = 0;
static struct queue *logqueue = NULL;
static int reading_kmesg = 0;

typedef unsigned char uint8_t;

void logbuf(int c)
{
	if (reading_kmesg)
		return;
	if (index > 1 << 20)
		return;
	logbuffer[index++] = c;
}

/*
 *  return true if current user is eve
 */
int iseve(void)
{
	return strcmp(eve.name, current->user.name) == 0;
}

struct username eve = {.name = "eve", .name_lock = SPINLOCK_INITIALIZER};
char hostdomain[256] = "akaros.org";

static struct {
	qlock_t qlock;

	int raw;         /* true if we shouldn't process input */
	struct kref ctl; /* number of opens to the control file */
	int x;           /* index into line */
	char line[1024]; /* current input line */

	int count;
	int ctlpoff;

	/* a place to save up characters at interrupt time before dumping them
	 * in the queue */
	spinlock_t lockputc;
	char istage[1024];
	char *iw;
	char *ir;
	char *ie;
} kbd = {
    .iw = kbd.istage,
    .ir = kbd.istage,
    .ie = kbd.istage + sizeof(kbd.istage),
};

char *sysname;
int64_t fasthz;

static int readtime(uint32_t, char *, int);
static int readbintime(char *, int);
static int writetime(char *, int);
static int writebintime(char *, int);
static int hostownerwrite(char *, size_t);
static void killkid(void);

enum { CMbroken,
       CMconsole,
       CMhalt,
       CMnobroken,
       CMpanic,
       CMreboot,
};

struct cmdtab rebootmsg[] = {
    {CMbroken, "broken", 0}, {CMconsole, "console", 1},
    {CMhalt, "halt", 1},     {CMnobroken, "nobroken", 0},
    {CMpanic, "panic", 0},   {CMreboot, "reboot", 0},
};

void printinit(void)
{
	lineq = qopen(2 * 1024, 0, NULL, NULL);
	if (lineq == NULL)
		panic("printinit");
	qdropoverflow(lineq, 1);
}

int consactive(void)
{
	if (serialoq)
		return qlen(serialoq) > 0;
	return 0;
}

void prflush(void)
{
	long times = 0;

	while (consactive())
		if (times++ > 1000)
			break;
}

/*
 * Log console output so it can be retrieved via /dev/kmesg.
 * This is good for catching boot-time messages after the fact.
 */
struct {
	spinlock_t lk;
	char buf[1 << 20];
	unsigned int n;
} kmesg;

static void kmesgputs(char *str, int n)
{
	unsigned int nn, d;

	spin_lock_irqsave(&kmesg.lk);
	/* take the tail of huge writes */
	if (n > sizeof kmesg.buf) {
		d = n - sizeof kmesg.buf;
		str += d;
		n -= d;
	}

	/* slide the buffer down to make room */
	nn = kmesg.n;
	if (nn + n >= sizeof kmesg.buf) {
		d = nn + n - sizeof kmesg.buf;
		if (d)
			memmove(kmesg.buf, kmesg.buf + d, sizeof kmesg.buf - d);
		nn -= d;
	}

	/* copy the data in */
	memmove(kmesg.buf + nn, str, n);
	nn += n;
	kmesg.n = nn;
	spin_unlock_irqsave(&kmesg.lk);
}

/*
 *   Print a string on the console.  Convert \n to \r\n for serial
 *   line consoles.  Locking of the queues is left up to the screen
 *   or uart code.  Multi-line messages to serial consoles may get
 *   interspersed with other messages.
 */
static void putstrn0(char *str, int n, int usewrite)
{
	int m;
	char *t;

#if 0
	if (!islo())
		usewrite = 0;
#endif

	/*
	 *  how many different output devices do we need?
	 */
	kmesgputs(str, n);

	/*
	 *  if someone is reading /dev/kprint
	 *  put the message there.
	 *  if not and there's an attached bit mapped display,
	 *  put the message there.
	 *
	 *  if there's a serial line being used as a console,
	 *  put the message there.
	 */
	if (kprintoq != NULL && !qisclosed(kprintoq)) {
		if (usewrite)
			qwrite(kprintoq, str, n);
		else
			qiwrite(kprintoq, str, n);
	} else if (screenputs != NULL)
		screenputs(str, n);

	if (serialoq == NULL) {
#if 0
		uartputs(str, n);
#endif
		return;
	}

	while (n > 0) {
		t = memchr(str, '\n', n);
		if (t && !kbd.raw) {
			m = t - str;
			if (usewrite) {
				qwrite(serialoq, str, m);
				qwrite(serialoq, "\r\n", 2);
			} else {
				qiwrite(serialoq, str, m);
				qiwrite(serialoq, "\r\n", 2);
			}
			n -= m + 1;
			str = t + 1;
		} else {
			if (usewrite)
				qwrite(serialoq, str, n);
			else
				qiwrite(serialoq, str, n);
			break;
		}
	}
}

void putstrn(char *str, int n)
{
	putstrn0(str, n, 0);
}

int noprint;

int print(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	if (noprint)
		return -1;

	va_start(arg, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	putstrn(buf, n);

	return n;
}

/*
 * Want to interlock iprints to avoid interlaced output on
 * multiprocessor, but don't want to deadlock if one processor
 * dies during print and another has something important to say.
 * Make a good faith effort.
 */
static spinlock_t iprintlock;
static int iprintcanlock(spinlock_t *l)
{
	int i;

	for (i = 0; i < 1000; i++) {
		if (spin_trylock(l))
			return 1;
	}
	return 0;
}

int iprint(char *fmt, ...)
{
	int8_t s = 0;
	int n, locked;
	va_list arg;
	char buf[PRINTSIZE];

	disable_irqsave(&s);
	va_start(arg, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	locked = iprintcanlock(&iprintlock);
	if (screenputs != NULL && iprintscreenputs)
		screenputs(buf, n);
#if 0
	uartputs(buf, n);
#endif
	if (locked)
		spin_unlock(&iprintlock);
	enable_irqsave(&s);

	return n;
}

/* libmp at least contains a few calls to sysfatal; simulate with panic */
void sysfatal(char *fmt, ...)
{
	char err[256];
	va_list arg;

	va_start(arg, fmt);
	vsnprintf(err, sizeof err, fmt, arg);
	va_end(arg);
	panic("sysfatal: %s", err);
}

#if 0
int pprint(char *fmt, ...)
{
	ERRSTACK(2);
	int n;
	struct chan *c;
	va_list arg;
	char buf[2 * PRINTSIZE];

	if (up == NULL || current->fgrp == NULL)
		return 0;

	c = current->fgrp->fd[2];
	if (c == 0 || (c->mode != O_WRITE && c->mode != O_RDWR))
		return 0;
	n = snprintf(buf, sizeof buf, "%s %lud: ", current->text, current->pid);
	va_start(arg, fmt);
	n = vsnprintf(buf + n, sizeof(buf), fmt, arg);
	va_end(arg);

	if (waserror())
		return 0;
	devtab[c->type]->write(c, buf, n, c->offset);
	poperror();

	spin_lock(&c->lock);
	c->offset += n;
	spin_unlock(&c->lock);

	return n;
}
#endif

static void echoscreen(char *buf, int n)
{
	char *e, *p;
	char ebuf[128];
	int x;

	p = ebuf;
	e = ebuf + sizeof(ebuf) - 4;
	while (n-- > 0) {
		if (p >= e) {
			screenputs(ebuf, p - ebuf);
			p = ebuf;
		}
		x = *buf++;
		if (x == 0x15) {
			*p++ = '^';
			*p++ = 'U';
			*p++ = '\n';
		} else
			*p++ = x;
	}
	if (p != ebuf)
		screenputs(ebuf, p - ebuf);
}

static void echoserialoq(char *buf, int n)
{
	char *e, *p;
	char ebuf[128];
	int x;

	p = ebuf;
	e = ebuf + sizeof(ebuf) - 4;
	while (n-- > 0) {
		if (p >= e) {
			qiwrite(serialoq, ebuf, p - ebuf);
			p = ebuf;
		}
		x = *buf++;
		if (x == '\n') {
			*p++ = '\r';
			*p++ = '\n';
		} else if (x == 0x15) {
			*p++ = '^';
			*p++ = 'U';
			*p++ = '\n';
		} else
			*p++ = x;
	}
	if (p != ebuf)
		qiwrite(serialoq, ebuf, p - ebuf);
}

static void echo(char *buf, int n)
{
	static int ctrlt, pid;
	char *e, *p;

	if (n == 0)
		return;

	e = buf + n;
	for (p = buf; p < e; p++) {
		switch (*p) {
#if 0
		case 0x10:	/* ^P */
			if (cpuserver && !kbd.ctlpoff) {
				active.exiting = 1;
				return;
			}
			break;
#endif
		case 0x14: /* ^T */
			ctrlt++;
			if (ctrlt > 2)
				ctrlt = 2;
			continue;
		}

		if (ctrlt != 2)
			continue;

		/* ^T escapes */
		ctrlt = 0;
		switch (*p) {
#if 0
		case 'S':{
				int8_t x = 0;
				disable_irqsave(&x);
				dumpstack();
				procdump();
				enable_irqsave(&x);
				return;
			}
#endif
		case 's':
			dumpstack();
			return;
#if 0
		case 'x':
			xsummary();
			ixsummary();
			mallocsummary();
			memorysummary();
			pagersummary();
			return;
		case 'd':
			if (consdebug == NULL)
				consdebug = rdb;
			else
				consdebug = NULL;
			printd("consdebug now %#p\n", consdebug);
			return;
		case 'D':
			if (consdebug == NULL)
				consdebug = rdb;
			consdebug();
			return;
		case 'p':
			x = spllo();
			procdump();
			splx(x);
			return;
		case 'q':
			scheddump();
			return;
		case 'k':
			killbig("^t ^t k");
			return;
#endif
		case 'r':
			exit(0);
			return;
		}
	}

	qwrite(kbdq, buf, n); /* was once qproduce, YMMV */
	if (kbd.raw)
		return;
	kmesgputs(buf, n);
	if (screenputs != NULL)
		echoscreen(buf, n);
	if (serialoq)
		echoserialoq(buf, n);
}

/*
 *  Called by a uart interrupt for console input.
 *
 *  turn '\r' into '\n' before putting it into the queue.
 */
int kbdcr2nl(struct queue *ignored_queue, int ch)
{
	char *next;

	spin_lock_irqsave(&kbd.lockputc); /* just a mutex */
	if (ch == '\r' && !kbd.raw)
		ch = '\n';
	next = kbd.iw + 1;
	if (next >= kbd.ie)
		next = kbd.istage;
	if (next != kbd.ir) {
		*kbd.iw = ch;
		kbd.iw = next;
	}
	spin_unlock_irqsave(&kbd.lockputc);
	return 0;
}

/*
 *  Put character, possibly a rune, into read queue at interrupt time.
 *  Called at interrupt time to process a character.
 */
int kbdputc(struct queue *unused_queue, int ch)
{
	int i, n;
	char buf[3];
	// Akaros does not use Rune et al.
	// Rune r;
	int r;
	char *next;

	if (kbd.ir == NULL)
		return 0; /* in case we're not inited yet */

	spin_lock_irqsave(&kbd.lockputc); /* just a mutex */
	r = ch;
	// n = runetochar(buf, &r);
	// Fake Rune support.
	n = 1;
	buf[0] = r;
	for (i = 0; i < n; i++) {
		next = kbd.iw + 1;
		if (next >= kbd.ie)
			next = kbd.istage;
		if (next == kbd.ir)
			break;
		*kbd.iw = buf[i];
		kbd.iw = next;
	}
	spin_unlock_irqsave(&kbd.lockputc);
	return 0;
}

/*
 *  we save up input characters till clock time to reduce
 *  per character interrupt overhead.
 */
static void kbdputcclock(void)
{
	char *iw;

	/* this amortizes cost of qproduce */
	if (kbd.iw != kbd.ir) {
		iw = kbd.iw;
		if (iw < kbd.ir) {
			echo(kbd.ir, kbd.ie - kbd.ir);
			kbd.ir = kbd.istage;
		}
		if (kbd.ir != iw) {
			echo(kbd.ir, iw - kbd.ir);
			kbd.ir = iw;
		}
	}
}

enum { Qdir,
       Qbintime,
       Qconfig,
       Qcons,
       Qconsctl,
       Qcputime,
       Qdrivers,
       Qhostdomain,
       Qhostowner,
       Qklog,
       Qkmesg,
       Qkprint,
       Qnull,
       Qosversion,
       Qpgrpid,
       Qpid,
       Qppid,
       Qreboot,
       Qstdin,
       Qstdout,
       Qstderr,
       Qswap,
       Qsysctl,
       Qsysname,
       Qsysstat,
       Qtime,
       Quser,
       Qzero,
       Qkillkid,
};

enum { VLNUMSIZE = 22,
       DOMLEN = 256,
};

static struct dirtab consdir[] = {
    {".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
    {"bintime", {Qbintime}, 24, 0664},
    {"config", {Qconfig}, 0, 0444},
    {"cons", {Qcons}, 0, 0660},
    {"consctl", {Qconsctl}, 0, 0220},
    // FIXME -- we don't have real permissions yet so we set it to 222, not 220
    {"killkid", {Qkillkid}, 0, 0220 | /* BOGUS */ 2},
    {"cputime", {Qcputime}, 6 * NUMSIZE, 0444},
    {"drivers", {Qdrivers}, 0, 0444},
    {"hostdomain", {Qhostdomain}, DOMLEN, 0664},
    {"hostowner", {Qhostowner}, 0, 0664},
    {"klog", {Qklog}, 0, 0440},
    {"kmesg", {Qkmesg}, 0, 0440},
    {"kprint", {Qkprint, 0, QTEXCL}, 0, DMEXCL | 0440},
    {"null", {Qnull}, 0, 0666},
    {"osversion", {Qosversion}, 0, 0444},
    {"pgrpid", {Qpgrpid}, NUMSIZE, 0444},
    {"pid", {Qpid}, NUMSIZE, 0444},
    {"ppid", {Qppid}, NUMSIZE, 0444},
    {"reboot", {Qreboot}, 0, 0660},
    {"stdin", {Qstdin}, 0, 0666},
    {"stdout", {Qstdout}, 0, 0666},
    {"stderr", {Qstderr}, 0, 0666},
    {"swap", {Qswap}, 0, 0664},
    {"sysctl", {Qsysctl}, 0, 0666},
    {"sysname", {Qsysname}, 0, 0664},
    {"sysstat", {Qsysstat}, 0, 0666},
    {"time", {Qtime}, NUMSIZE + 3 * VLNUMSIZE, 0664},
    {"user", {Quser}, 0, 0666},
    {"zero", {Qzero}, 0, 0444},
};

int consreadnum(uint32_t off, char *buf, uint32_t n, uint32_t val, int size)
{
	char tmp[64];

	snprintf(tmp, sizeof(tmp), "%*lud", size - 1, val);
	tmp[size - 1] = ' ';
	if (off >= size)
		return 0;
	if (off + n > size)
		n = size - off;
	memmove(buf, tmp + off, n);
	return n;
}

int consreadstr(uint32_t off, char *buf, uint32_t n, char *str)
{
	int size;

	size = strlen(str);
	if (off >= size)
		return 0;
	if (off + n > size)
		n = size - off;
	memmove(buf, str + off, n);
	return n;
}

static void consinit(void)
{
	kstrdup(&sysname, "nanwan");
	cons_q = qopen(256, 0, 0, 0);
#if 0
	todinit();
#endif
	/*
	 * at 115200 baud, the 1024 char buffer takes 56 ms to process,
	 * processing it every 22 ms should be fine
	 */
#if 0
	addclock0link(kbdputcclock, 22);
#endif
	cmb(); /* single-core, just need previous instructions to be issued. */
	cons_has_init = TRUE;
}

static char *devname(void);

static struct chan *consattach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *conswalk(struct chan *c, struct chan *nc, char **name,
                                unsigned int nname)
{
	return devwalk(c, nc, name, nname, consdir, ARRAY_SIZE(consdir),
	               devgen);
}

static size_t consstat(struct chan *c, uint8_t *dp, size_t n)
{
	struct dir dir;
	struct dirtab *tab;
	int perm;

	switch ((uint32_t)c->qid.path) {
	case Qstdin:
		tab = &consdir[Qstdin];
		perm = tab->perm;
		perm |= qreadable(cons_q) ? DMREADABLE : 0;
		devdir(c, tab->qid, tab->name, qlen(cons_q), eve.name, perm,
		       &dir);
		return dev_make_stat(c, &dir, dp, n);
	case Qnull:
		tab = &consdir[Qnull];
		perm = tab->perm | DMWRITABLE;
		devdir(c, tab->qid, tab->name, 0, eve.name, perm, &dir);
		return dev_make_stat(c, &dir, dp, n);
	default:
		return devstat(c, dp, n, consdir, ARRAY_SIZE(consdir), devgen);
	}
}

static struct chan *consopen(struct chan *c, int omode)
{
	c->aux = NULL;
	c = devopen(c, omode, consdir, ARRAY_SIZE(consdir), devgen);
	switch ((uint32_t)c->qid.path) {
	case Qconsctl:
		kref_get(&kbd.ctl, 1);
		break;

	case Qkprint:
		if (atomic_swap(&kprintinuse, 1) != 0) {
			c->flag &= ~COPEN;
			error(EADDRINUSE, "kprintinuse lock failed");
		}
		if (kprintoq == NULL) {
			kprintoq = qopen(8 * 1024, Qcoalesce, 0, 0);
			if (kprintoq == NULL) {
				c->flag &= ~COPEN;
				error(ENOMEM, "Can't allocate kprintoq");
			}
			qdropoverflow(kprintoq, 1);
		} else
			qreopen(kprintoq);
		c->iounit = qiomaxatomic;
		break;
	}
	return c;
}

static void consclose(struct chan *c)
{
	switch ((uint32_t)c->qid.path) {
		/* last close of control file turns off raw */
	case Qconsctl:
		if (c->flag & COPEN) {
			if (kref_put(&kbd.ctl) == 0)
				kbd.raw = 0;
		}
		break;

		/* close of kprint allows other opens */
	case Qkprint:
		if (c->flag & COPEN) {
			kprintinuse = 0;
			qhangup(kprintoq, NULL);
		}
		break;
	}
}

static size_t consread(struct chan *c, void *buf, size_t n, off64_t off)
{
	ERRSTACK(1);
	uint32_t l;
#if 0
	Mach *mp;
#endif
	char *b, *bp, ch;
	char tmp[256]; /* must be >= 18*NUMSIZE (Qswap) */
	int i, k, id, send;
	int64_t offset = off;
#if 0
	extern char configfile[];
#endif

	if (n <= 0)
		return n;

	switch ((uint32_t)c->qid.path) {
	case Qdir:
		return devdirread(c, buf, n, consdir, ARRAY_SIZE(consdir),
		                  devgen);

	case Qcons:
		qlock(&(&kbd)->qlock);
		if (waserror()) {
			qunlock(&(&kbd)->qlock);
			nexterror();
		}
		while (!qcanread(lineq)) {
			if (qread(kbdq, &ch, 1) == 0)
				continue;
			send = 0;
			if (ch == 0) {
				/* flush output on rawoff -> rawon */
				if (kbd.x > 0)
					send = !qcanread(kbdq);
			} else if (kbd.raw) {
				kbd.line[kbd.x++] = ch;
				send = !qcanread(kbdq);
			} else {
				switch (ch) {
				case '\b':
					if (kbd.x > 0)
						kbd.x--;
					break;
				case 0x15: /* ^U */
					kbd.x = 0;
					break;
				case '\n':
				case 0x04: /* ^D */
					send = 1;
				default:
					if (ch != 0x04)
						kbd.line[kbd.x++] = ch;
					break;
				}
			}
			if (send || kbd.x == sizeof kbd.line) {
				qwrite(lineq, kbd.line, kbd.x);
				kbd.x = 0;
			}
		}
		n = qread(lineq, buf, n);
		qunlock(&(&kbd)->qlock);
		poperror();
		return n;

#if 0
	case Qcputime:
		k = offset;
		if (k >= 6 * NUMSIZE)
			return 0;
		if (k + n > 6 * NUMSIZE)
			n = 6 * NUMSIZE - k;
		/* easiest to format in a separate buffer and copy out */
		for (i = 0; i < 6 && NUMSIZE * i < k + n; i++) {
			l = current->time[i];
			if (i == TReal)
				l = MACHP(0)->ticks - l;
			l = TK2MS(l);
			consreadnum(0, tmp + NUMSIZE * i, NUMSIZE, l, NUMSIZE);
		}
		memmove(buf, tmp + k, n);
		return n;
#endif

	case Qkmesg:
		/*
		 * This is unlocked to avoid tying up a process
		 * that's writing to the buffer.  kmesg.n never
		 * gets smaller, so worst case the reader will
		 * see a slurred buffer.
		 */
		if (off >= kmesg.n)
			n = 0;
		else {
			if (off + n > kmesg.n)
				n = kmesg.n - off;
			memmove(buf, kmesg.buf + off, n);
		}
		return n;

	case Qkprint:
		return qread(kprintoq, buf, n);

	case Qpgrpid:
		return consreadnum((uint32_t)offset, buf, n,
		                   current->pgrp->pgrpid, NUMSIZE);

	case Qpid:
		return consreadnum((uint32_t)offset, buf, n, current->pid,
		                   NUMSIZE);

	case Qppid:
		return consreadnum((uint32_t)offset, buf, n, current->ppid,
		                   NUMSIZE);

	case Qtime:
		return readtime((uint32_t)offset, buf, n);

	case Qbintime:
		return readbintime(buf, n);

	case Qhostowner:
		return consreadstr((uint32_t)offset, buf, n, eve.name);

	case Qhostdomain:
		return consreadstr((uint32_t)offset, buf, n, hostdomain);

	case Quser:
		return consreadstr((uint32_t)offset, buf, n,
		                   current->user.name);

	case Qnull:
		return 0;

#if 0
	case Qconfig:
		return consreadstr((uint32_t) offset, buf, n, configfile);

	case Qsysstat:
		/* +1 for NUL */
		b = kzmalloc(conf.nmach * (NUMSIZE * 11 + 1) + 1, 0);
		bp = b;
		for (id = 0; id < 32; id++) {
			if (active.machs & (1 << id)) {
				mp = MACHP(id);
				consreadnum(0, bp, NUMSIZE, id, NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->cs, NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->intr, NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->syscall,
					    NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->pfault,
					    NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->tlbfault,
					    NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->tlbpurge,
					    NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE, mp->load, NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE,
					    (mp->perf.avg_inidle * 100) /
					    mp->perf.period, NUMSIZE);
				bp += NUMSIZE;
				consreadnum(0, bp, NUMSIZE,
					    (mp->perf.avg_inintr * 100) /
					    mp->perf.period, NUMSIZE);
				bp += NUMSIZE;
				*bp++ = '\n';
			}
		}
		if (waserror()) {
			kfree(b);
			nexterror();
		}
		n = consreadstr((uint32_t) offset, buf, n, b);
		kfree(b);
		poperror();
		return n;

	case Qswap:
		snprintf(tmp, sizeof tmp,
				 "%lud memory\n"
				 "%d pagesize\n"
				 "%lud kernel\n"
				 "%lud/%lud user\n"
				 "%lud/%lud swap\n"
				 "%lud/%lud kernel malloc\n"
				 "%lud/%lud kernel draw\n",
				 conf.npage * BY2PG,
				 BY2PG,
				 conf.npage - conf.upages,
				 palloc.user - palloc.freecount, palloc.user,
				 conf.nswap - swapalloc.free, conf.nswap,
				 mainmem->cursize, mainmem->maxsize,
				 imagmem->cursize, imagmem->maxsize);

		return consreadstr((uint32_t) offset, buf, n, tmp);
#endif

	case Qstdin:
		if (c->flag & O_NONBLOCK)
			return qread_nonblock(cons_q, buf, n);
		else
			return qread(cons_q, buf, n);
	case Qsysname:
		/* TODO: this is racy */
		if (sysname == NULL)
			return 0;
		return consreadstr((uint32_t)offset, buf, n, sysname);

	case Qdrivers:
		b = kzmalloc(READSTR, 0);
		if (b == NULL)
			error(ENOMEM,
			      "allocation for /dev/drivers read failed");
		k = 0;
		for (int i = 0; &devtab[i] < __devtabend; i++)
			k += snprintf(b + k, READSTR - k, "#%s\n",
			              devtab[i].name);
		if (waserror()) {
			kfree(b);
			nexterror();
		}
		n = consreadstr((uint32_t)offset, buf, n, b);
		kfree(b);
		poperror();
		return n;

	case Qklog:
		// return qread(klogq, buf, n);
		/* the queue gives us some elasticity for log reading. */
		if (!logqueue)
			logqueue = qopen(1 << 20, 0, 0, 0);
		if (logqueue) {
			int ret;
			/* atomic sets/gets are not that important in this case.
			 */
			reading_kmesg = 1;
			qwrite(logqueue, logbuffer, index);
			index = 0;
			ret = qread(logqueue, buf, n);
			reading_kmesg = 0;
			return ret;
		}
		break;

	case Qzero:
		memset(buf, 0, n);
		return n;

	case Qosversion:
		snprintf(tmp, sizeof tmp, "2000");
		n = consreadstr((uint32_t)offset, buf, n, tmp);
		return n;

	default:
		printd("consread %#llux\n", c->qid.path);
		error(EINVAL, "bad QID in consread");
	}
	return -1; /* never reached */
}

static size_t conswrite(struct chan *c, void *va, size_t n, off64_t off)
{
	ERRSTACK(1);
	char buf[256], ch;
	long l, bp;
	char *a;
	// Mach *mp;
	int id, fd;
	struct chan *swc;
	uint32_t offset;
	struct cmdbuf *cb;
	struct cmdtab *ct;
	int x;
	uint64_t rip, rsp, cr3, flags, vcpu;
	int ret;

	a = va;
	offset = off;

	switch ((uint32_t)c->qid.path) {
	case Qcons:
		/*
		 * Can't page fault in putstrn, so copy the data locally.
		 */
		l = n;
		while (l > 0) {
			bp = l;
			if (bp > sizeof buf)
				bp = sizeof buf;
			memmove(buf, a, bp);
			putstrn0(buf, bp, 1);
			a += bp;
			l -= bp;
		}
		break;

	/* TODO: have it take a command about just *how* to kill the kid? */
	case Qkillkid:
		killkid();
		break;

	case Qconsctl:
		if (n >= sizeof(buf))
			n = sizeof(buf) - 1;
		strncpy(buf, a, n);
		buf[n] = 0;
		for (a = buf; a;) {
			if (strncmp(a, "rawon", 5) == 0) {
				kbd.raw = 1;
				/* clumsy hack - wake up reader */
				ch = 0;
				qwrite(kbdq, &ch, 1);
			} else if (strncmp(a, "rawoff", 6) == 0) {
				kbd.raw = 0;
			} else if (strncmp(a, "ctlpon", 6) == 0) {
				kbd.ctlpoff = 0;
			} else if (strncmp(a, "ctlpoff", 7) == 0) {
				kbd.ctlpoff = 1;
			}
			if ((a = strchr(a, ' ')) != NULL)
				a++;
		}
		break;

	case Qtime:
		if (!iseve())
			error(EPERM, "Hodie Natus Est Radici Frater");
		return writetime(a, n);

	case Qbintime:
		if (!iseve())
			error(EPERM, ERROR_FIXME);
		return writebintime(a, n);

	case Qhostowner:
		return hostownerwrite(a, n);

#if 0
	case Qhostdomain:
		return hostdomainwrite(a, n);

	case Quser:
		return userwrite(a, n);
#endif

	case Qnull:
		break;

	case Qconfig:
		error(EPERM, "Cannot write to config QID");
		break;

	case Qsysctl:
		// if (!iseve()) error(EPERM, ERROR_FIXME);
		cb = parsecmd(a, n);
		if (cb->nf > 1)
			printd("cons sysctl cmd %s\n", cb->f[0]);

	case Qreboot:
		if (!iseve())
			error(EPERM, ERROR_FIXME);
		cb = parsecmd(a, n);

		if (waserror()) {
			kfree(cb);
			nexterror();
		}
		ct = lookupcmd(cb, rebootmsg, ARRAY_SIZE(rebootmsg));
		switch (ct->index) {
		case CMhalt:
			cpu_halt();
			break;
		case CMbroken:
			keepbroken = 1;
			break;
		case CMnobroken:
			keepbroken = 0;
			break;
		case CMreboot:
			reboot();
			break;
		case CMpanic:
			*(uint32_t *)0 = 0;
			panic("/dev/reboot");
			break;
		}
		poperror();
		kfree(cb);
		break;

#if 0
	case Qsysstat:
		for (id = 0; id < 32; id++) {
			if (active.machs & (1 << id)) {
				mp = MACHP(id);
				mp->cs = 0;
				mp->intr = 0;
				mp->syscall = 0;
				mp->pfault = 0;
				mp->tlbfault = 0;
				mp->tlbpurge = 0;
			}
		}
		break;

	case Qswap:
		if (n >= sizeof buf)
			error(EINVAL, "n is bigger than sizeof buf for Qswap");
		memmove(buf, va, n);	/* so we can NUL-terminate */
		buf[n] = 0;
		/* start a pager if not already started */
		if (strncmp(buf, "start", 5) == 0) {
			kickpager();
			break;
		}
		if (!iseve())
			error(EPERM, ERROR_FIXME);
		if (buf[0] < '0' || '9' < buf[0])
			error(EINVAL, ERROR_FIXME);
		fd = strtoul(buf, 0, 0);
		swc = fdtochan(fd, -1, 1, 1);
		setswapchan(swc);
		break;
#endif

	case Qstdout:
	case Qstderr:
		print_lock();
		if (waserror()) {
			print_unlock();
			nexterror();
		}
		/* TODO: tty hack.  they are sending us an escape sequence, and
		 * the keyboard would try to print it (which it can't do yet).
		 * The hack is even dirtier in that we only detect it if it is
		 * the first
		 * char, and we ignore everything else.  \033 is 0x1b. */
		if (((char *)va)[0] != '\033') {
			n = MIN(n, 80);
			cputbuf(va, n);
		}
		poperror();
		print_unlock();
		return n;
	case Qsysname:
		/* TODO: this is racy */
		if (offset != 0)
			error(EINVAL, ERROR_FIXME);
		if (n <= 0 || n >= sizeof buf)
			error(EINVAL, ERROR_FIXME);
		strncpy(buf, a, n);
		buf[n] = 0;
		if (buf[n - 1] == '\n')
			buf[n - 1] = 0;
		kstrdup(&sysname, buf);
		break;

	default:
		printd("conswrite: %#llux\n", c->qid.path);
		error(EINVAL, "bad QID in conswrite");
	}
	return n;
}

static char *cons_chaninfo(struct chan *ch, char *ret, size_t ret_l)
{
	switch ((uint32_t)ch->qid.path) {
	case Qstdin:
		snprintf(ret, ret_l, "qio len: %d", qlen(cons_q));
		break;
	default:
		return devchaninfo(ch, ret, ret_l);
	}
	return ret;
}

static void __consq_fire_taps(uint32_t srcid, long a0, long a1, long a2)
{
	struct fd_tap *tap_i;
	int filter = a0;

	spin_lock(&cons_q_lock);
	SLIST_FOREACH (tap_i, &cons_q_fd_taps, link)
		fire_tap(tap_i, filter);
	spin_unlock(&cons_q_lock);
}

static void cons_q_wake_cb(struct queue *q, void *data, int filter)
{
	/* TODO: taps can't fire from IRQ context, but the qiwrites for stdin
	 * come from IRQ context.  So we need an RKM here. */
	send_kernel_message(core_id(), __consq_fire_taps, filter, 0, 0,
	                    KMSG_ROUTINE);
}

static int tap_stdin(struct chan *c, struct fd_tap *tap, int cmd)
{
	int ret;

/* We don't actually support HANGUP, but epoll implies it. */
#define CONS_STDIN_TAPS (FDTAP_FILT_READABLE | FDTAP_FILT_HANGUP)

	if (tap->filter & ~CONS_STDIN_TAPS) {
		set_error(ENOSYS, "Unsupported #%s tap, must be %p", devname(),
		          CONS_STDIN_TAPS);
		return -1;
	}
	spin_lock(&cons_q_lock);
	switch (cmd) {
	case FDTAP_CMD_ADD:
		if (SLIST_EMPTY(&cons_q_fd_taps))
			qio_set_wake_cb(cons_q, cons_q_wake_cb, 0);
		SLIST_INSERT_HEAD(&cons_q_fd_taps, tap, link);
		ret = 0;
		break;
	case FDTAP_CMD_REM:
		SLIST_REMOVE(&cons_q_fd_taps, tap, fd_tap, link);
		if (SLIST_EMPTY(&cons_q_fd_taps))
			qio_set_wake_cb(cons_q, 0, 0);
		ret = 0;
		break;
	default:
		set_error(ENOSYS, "Unsupported #%s tap command %p", devname(),
		          cmd);
		ret = -1;
	}
	spin_unlock(&cons_q_lock);
	return ret;
}

/* Null isn't really tapable, since there are no edge events - it is always
 * writable.  However, users might want to tap their output files, regardless of
 * whether or not it will generate an event.  We'll pretend that we tapped it
 * (or untapped, based on the command).
 *
 * Note that epoll will generate an event, since it will stat null and see its
 * writable, so epoll will appear to have an event. */
static int tap_null(struct chan *c, struct fd_tap *tap, int cmd)
{
/* We don't actually support HANGUP, but epoll implies it. */
#define CONS_NULL_TAPS (FDTAP_FILT_WRITABLE | FDTAP_FILT_HANGUP)

	if (tap->filter & ~CONS_NULL_TAPS) {
		set_error(ENOSYS, "Unsupported #%s tap, must be %p", devname(),
		          CONS_NULL_TAPS);
		return -1;
	}
	return 0;
}

static int cons_tapfd(struct chan *c, struct fd_tap *tap, int cmd)
{
	switch ((uint32_t)c->qid.path) {
	case Qstdin:
		return tap_stdin(c, tap, cmd);
	case Qnull:
		return tap_null(c, tap, cmd);
	default:
		set_error(ENOSYS, "Can't tap #%s file type %d", devname(),
		          c->qid.path);
		return -1;
	}
}

struct dev consdevtab __devtab = {
    .name = "cons",

    .reset = devreset,
    .init = consinit,
    .shutdown = devshutdown,
    .attach = consattach,
    .walk = conswalk,
    .stat = consstat,
    .open = consopen,
    .create = devcreate,
    .close = consclose,
    .read = consread,
    .bread = devbread,
    .write = conswrite,
    .bwrite = devbwrite,
    .remove = devremove,
    .wstat = devwstat,
    .power = devpower,
    .chaninfo = cons_chaninfo,
    .tapfd = cons_tapfd,
};

static char *devname(void)
{
	return consdevtab.name;
}

static uint64_t uvorder = 0x0001020304050607ULL;

static uint8_t *le2int64_t(int64_t *to, uint8_t *f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t *)to;
	o = (uint8_t *)&uvorder;
	for (i = 0; i < sizeof(int64_t); i++)
		t[o[i]] = f[i];
	return f + sizeof(int64_t);
}

static uint8_t *int64_t2le(uint8_t *t, int64_t from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t *)&from;
	o = (uint8_t *)&uvorder;
	for (i = 0; i < sizeof(int64_t); i++)
		t[i] = f[o[i]];
	return t + sizeof(int64_t);
}

static long order = 0x00010203;

static uint8_t *le2long(long *to, uint8_t *f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t *)&to;
	o = (uint8_t *)&order;
	for (i = 0; i < sizeof(long); i++)
		t[o[i]] = f[i];
	return f + sizeof(long);
}

static uint8_t *long2le(uint8_t *t, long from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t *)&from;
	o = (uint8_t *)&order;
	for (i = 0; i < sizeof(long); i++)
		t[i] = f[o[i]];
	return t + sizeof(long);
}

char *Ebadtimectl = "bad time control";

/*
 *  like the old #c/time but with added info.  Return
 *
 *	secs	nanosecs	fastticks	fasthz
 */
static int readtime(uint32_t off, char *buf, int n)
{
	int64_t nsec, ticks;
	long sec;
	char str[7 * NUMSIZE];

	if (fasthz == 0LL)
		fasthz = __proc_global_info.tsc_freq;
#if 0
	fastticks((uint64_t *) & fasthz);
	nsec = todget(&ticks);
#endif
	ticks = read_tsc();
	nsec = tsc2nsec(ticks);
	sec = nsec / 1000000000ULL;
	snprintf(str, sizeof(str), "%*lud %*llud %*llud %*llud ", NUMSIZE - 1,
	         sec, VLNUMSIZE - 1, nsec, VLNUMSIZE - 1, ticks, VLNUMSIZE - 1,
	         fasthz);
	return consreadstr(off, buf, n, str);
}

/*
 *  set the time in seconds
 */
static int writetime(char *buf, int n)
{
	char b[13];
	long i;
	int64_t now;

	if (n >= sizeof(b))
		error(EINVAL, "bad size in writetime");
	strncpy(b, buf, n);
	b[n] = 0;
	i = strtol(b, 0, 0);
	if (i <= 0)
		error(EINVAL, "Bad time in write");
	now = i * 1000000000LL;
#if 0
	todset(now, 0, 0);
#endif
	return n;
}

/*
 *  read binary time info.  all numbers are little endian.
 *  ticks and nsec are syncronized.
 */
static int readbintime(char *buf, int n)
{
	int i;
	int64_t nsec, ticks;
	uint8_t *b = (uint8_t *)buf;

	i = 0;
	if (fasthz == 0LL)
		fasthz = __proc_global_info.tsc_freq;
#if 0
	fastticks((uint64_t *) & fasthz);
	nsec = todget(&ticks);
#endif
	ticks = read_tsc();
	nsec = tsc2nsec(ticks);
	if (n >= 3 * sizeof(uint64_t)) {
		int64_t2le(b + 2 * sizeof(uint64_t), fasthz);
		i += sizeof(uint64_t);
	}
	if (n >= 2 * sizeof(uint64_t)) {
		int64_t2le(b + sizeof(uint64_t), ticks);
		i += sizeof(uint64_t);
	}
	if (n >= 8) {
		int64_t2le(b, nsec);
		i += sizeof(int64_t);
	}
	return i;
}

/*
 *  set any of the following
 *	- time in nsec
 *	- nsec trim applied over some seconds
 *	- clock frequency
 */
static int writebintime(char *buf, int n)
{
	uint8_t *p;
	int64_t delta = 0;
	long period = 0;

	n--;
	p = (uint8_t *)buf + 1;
	switch (*buf) {
	case 'n':
		if (n < sizeof(int64_t))
			error(EINVAL, ERROR_FIXME);
		le2int64_t(&delta, p);
#if 0
			todset(delta, 0, 0);
#endif
		break;
	case 'd':
		if (n < sizeof(int64_t) + sizeof(long))
			error(EINVAL, ERROR_FIXME);
		p = le2int64_t(&delta, p);
		le2long(&period, p);
#if 0
			todset(-1, delta, period);
#endif
		break;
	case 'f':
		if (n < sizeof(uint64_t))
			error(EINVAL, ERROR_FIXME);
		le2int64_t(&fasthz, p);
		if (fasthz <= 0)
			error(EINVAL, ERROR_FIXME);
#if 0
			todsetfreq(fasthz);
#endif
		break;
	}
	return n;
}

/*
 * set the hostowner, but only if current is the hostowner and the new value
 * isn't too long
 */
static int hostownerwrite(char *buf, size_t n)
{
	ERRSTACK(1);

	if (!iseve())
		error(EPERM, "only hostowner can change hostowner");

	if (strlen(buf) < 1)
		error(EINVAL, "hostowner cannot be set to \"\"");

	spin_lock(&eve.name_lock);

	if (waserror()) {
		spin_unlock(&eve.name_lock);
		nexterror();
	}

	// value at boot is ""
	if (eve.name[0] != 0)
		error(EPERM, "hostowner can only be set once");

	__set_username(&eve, buf);

	poperror();
	spin_unlock(&eve.name_lock);

	// Set this username first so it gets set regardless of an error below
	proc_set_username(current, buf);

	// Update all other hostowner processes
	// This is costly, but should only happen very early on
	struct process_set pset;

	proc_get_set(&pset);
	if (waserror()) {
		proc_free_set(&pset);
		nexterror();
	}

	for (size_t i = 0; i < pset.num_processes; i++) {
		// Won't update current again
		// Note: if a name is being written to the user field as it is
		// read
		//       here, it will appear as ""; but no other writes should
		//       occur as early as this should be run
		if (strcmp(pset.procs[i]->user.name, "") == 0)
			proc_set_username(pset.procs[i], buf);
	}

	poperror();
	proc_free_set(&pset);

	return n;
}

/*
 * find_first_kid finds your immediate descendant. Not because we're
 * trying to give it a check from the estate, mind you. We're here on
 * much more unpleasant business. To make it easy to call this
 * multiple times without lots of fussy checking, we allow it to be
 * called with NULL pointers; hence find_first_kid(find_first_kid(x))
 * works even if x is chaste and will return NULL.
 */
static struct proc *find_first_kid(struct proc *p)
{
	struct proc *kid;

	cv_lock(&p->child_wait);
	kid = TAILQ_FIRST(&p->children);
	if (kid) {
		proc_incref(kid, 1);
		proc_decref(p);
	}
	cv_unlock(&p->child_wait);
	return kid;
}

/*
 * killkid manages population issues by killing your kids.  This is a
 * particular temporary (we think) function for ssh. As such, we are
 * going to make a Command Decision to never kill the top level
 * kid. We start on grandkids, in other words.  Hey, the parent/child
 * terminology was Ken's idea; go yell at him.  If you want, we can
 * rename this to DrownCuteKittens.
 *
 * This was hard to get right so it's way more verbose than you might think we
 * need. Sorry.
 */
void killkid(void)
{
	struct proc *check, *kid, *grandkid, *victim;

	/* find first grandkid */
	proc_incref(current, 1);
	kid = find_first_kid(current);
	if (!kid) {
		proc_decref(current);
		return;
	}
	grandkid = find_first_kid(kid);
	if (!grandkid) {
		proc_decref(kid);
		return;
	}
	victim = grandkid;
	while (1) {
		check = find_first_kid(victim);
		if (!check)
			break;
		victim = check;
	}
	send_posix_signal(victim, SIGINT);
	proc_decref(victim);
}

/* This can be called any time after arch_init()->cons_irq_init().  That can
 * happen *before* the console device is initialized (devtab{reset,init}()).
 *
 * All other functions in #cons shouldn't be called until after cons_init(). */
void cons_add_char(char c)
{
	if (!cons_has_init)
		return;
	qiwrite(cons_q, &c, sizeof(char));
}
