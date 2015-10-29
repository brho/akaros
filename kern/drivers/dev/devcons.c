/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

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

void (*consdebug) (void) = NULL;
void (*screenputs) (char *, int) = NULL;

struct queue *kbdq;					/* unprocessed console input */
struct queue *lineq;					/* processed console input */
struct queue *serialoq;				/* serial console output */
struct queue *kprintoq;				/* console output, for /dev/kprint */
uint32_t kprintinuse;				/* test and set whether /dev/kprint is open */
int iprintscreenputs = 1;

int panicking;

static struct {
	qlock_t qlock;

	int raw;					/* true if we shouldn't process input */
	Ref ctl;					/* number of opens to the control file */
	int x;						/* index into line */
	char line[1024];			/* current input line */

	int count;
	int ctlpoff;

	/* a place to save up characters at interrupt time before dumping them in the queue */
	Lock lockputc;
	char istage[1024];
	char *iw;
	char *ir;
	char *ie;
} kbd = {
.iw = kbd.istage,.ir = kbd.istage,.ie = kbd.istage + sizeof(kbd.istage),};

char *sysname;
int64_t fasthz;

static void seedrand(void);
static int readtime(uint32_t, char *, int);
static int readbintime(char *, int);
static int writetime(char *, int);
static int writebintime(char *, int);

enum {
	CMhalt,
	CMreboot,
	CMpanic,
};

struct cmdtab rebootmsg[] = {
	CMhalt, "halt", 1,
	CMreboot, "reboot", 0,
	CMpanic, "panic", 0,
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
	uint32_t now;

	now = m->ticks;
	while (consactive())
		if (m->ticks - now >= HZ)
			break;
}

/*
 * Log console output so it can be retrieved via /dev/kmesg.
 * This is good for catching boot-time messages after the fact.
 */
struct {
	spinlock_t lk;
	char buf[KMESGSIZE];
	unsigned int n;
} kmesg;

static void kmesgputs(char *str, int n)
{
	unsigned int nn, d;

	spin_lock_irqsave(&(&kmesg.lk)->lock);
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
	spin_unlock_irqsave(&(&kmesg.lk)->lock);
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

	if (!islo())
		usewrite = 0;

	/*
	 *  how many different output devices do we need?
	 */
	kmesgputs(str, n);

	/*
	 *  if someone is reading /dev/kpr int unused_int,
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
		uartputs(str, n);
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
	n = vseprintf(buf, buf + sizeof(buf), fmt, arg) - buf;
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
static int iprintcanlock(spinlock_t * l)
{
	int i;

	for (i = 0; i < 1000; i++) {
		if (canlock(l))
			return 1;
		if (l->m == MACHP(m->machno))
			return 0;
		microdelay(100);
	}
	return 0;
}

int iprint(char *fmt, ...)
{
	int n, s, locked;
	va_list arg;
	char buf[PRINTSIZE];

	s = splhi();
	va_start(arg, fmt);
	n = vseprintf(buf, buf + sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	locked = iprintcanlock(&iprintlock);
	if (screenputs != NULL && iprintscreenputs)
		screenputs(buf, n);
	uartputs(buf, n);
	if (locked)
		spin_unlock(&(&iprintlock)->lock);
	splx(s);

	return n;
}

void panic(char *fmt, ...)
{
	int n, s;
	va_list arg;
	char buf[PRINTSIZE];

	kprintoq = NULL;	/* don't try to write to /dev/kprint */

	if (panicking)
		for (;;) ;
	panicking = 1;

	s = splhi();
	strlcpy(buf,  "panic: ", sizeof(buf));
	va_start(arg, fmt);
	n = vseprintf(buf + strlen(buf), buf + sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	iprint("%s\n", buf);
	if (consdebug)
		(*consdebug) ();
	splx(s);
	prflush();
	buf[n] = '\n';
	putstrn(buf, n + 1);
	dumpstack();

	exit(1);
}

/* libmp at least contains a few calls to sysfatal; simulate with panic */
void sysfatal(char *fmt, ...)
{
	char err[256];
	va_list arg;

	va_start(arg, fmt);
	vseprintf(err, err + sizeof err, fmt, arg);
	va_end(arg);
	panic("sysfatal: %s", err);
}

void _assert(char *fmt)
{
	panic("assert failed at %#p: %s", getcallerpc(&fmt), fmt);
}

int pprint(char *fmt, ...)
{
	ERRSTACK(2);
	int n;
	struct chan *c;
	va_list arg;
	char buf[2 * PRINTSIZE];

	if (up == NULL || up->fgrp == NULL)
		return 0;

	c = up->fgrp->fd[2];
	if (c == 0 || (c->mode != OWRITE && c->mode != ORDWR))
		return 0;
	n = snprintf(buf, sizeof buf, "%s %lud: ", up->text, up->pid);
	va_start(arg, fmt);
	n = vseprintf(buf + n, buf + sizeof(buf), fmt, arg) - buf;
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
	int x;
	char *e, *p;

	if (n == 0)
		return;

	e = buf + n;
	for (p = buf; p < e; p++) {
		switch (*p) {
			case 0x10:	/* ^P */
				if (cpuserver && !kbd.ctlpoff) {
					active.exiting = 1;
					return;
				}
				break;
			case 0x14:	/* ^T */
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
			case 'S':
				x = splhi();
				dumpstack();
				procdump();
				splx(x);
				return;
			case 's':
				dumpstack();
				return;
			case 'x':
				xsummary();
				ixsummary();
				mallocsummary();
				//  memorysummary();
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
			case 'r':
				exit(0);
				return;
		}
	}

	qproduce(kbdq, buf, n);
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
int kbdcr2nl(struct queue *, int ch)
{
	char *next;

	spin_lock_irqsave(&(&kbd.lockputc)->lock);	/* just a mutex */
	if (ch == '\r' && !kbd.raw)
		ch = '\n';
	next = kbd.iw + 1;
	if (next >= kbd.ie)
		next = kbd.istage;
	if (next != kbd.ir) {
		*kbd.iw = ch;
		kbd.iw = next;
	}
	spin_unlock_irqsave(&(&kbd.lockputc)->lock);
	return 0;
}

/*
 *  Put character, possibly a rune, into read queue at interrupt time.
 *  Called at interrupt time to process a character.
 */
int kbdputc(struct queue *, int ch)
{
	int i, n;
	char buf[3];
	Rune r;
	char *next;

	if (kbd.ir == NULL)
		return 0;	/* in case we're not inited yet */

	spin_lock_irqsave(&(&kbd.lockputc)->lock);	/* just a mutex */
	r = ch;
	n = runetochar(buf, &r);
	for (i = 0; i < n; i++) {
		next = kbd.iw + 1;
		if (next >= kbd.ie)
			next = kbd.istage;
		if (next == kbd.ir)
			break;
		*kbd.iw = buf[i];
		kbd.iw = next;
	}
	spin_unlock_irqsave(&(&kbd.lockputc)->lock);
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

enum {
	Qdir,
	Qbintime,
	Qcons,
	Qconsctl,
	Qcputime,
	Qdrivers,
	Qkmesg,
	Qkpr int unused_int,
	Qhostdomain,
	Qhostowner,
	Qnull,
	Qosversion,
	Qpgrpid,
	Qpid,
	Qppid,
	Qrandom,
	Qreboot,
	Qswap,
	Qsysname,
	Qsysstat,
	Qtime,
	Quser,
	Qzero,
	Qconfig,
};

enum {
	VLNUMSIZE = 22,
};

static struct dirtab consdir[] = {
	".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555,
	"bintime", {Qbintime}, 24, 0664,
	"cons", {Qcons}, 0, 0660,
	"consctl", {Qconsctl}, 0, 0220,
	"cputime", {Qcputime}, 6 * NUMSIZE, 0444,
	"drivers", {Qdrivers}, 0, 0444,
	"hostdomain", {Qhostdomain}, DOMLEN, 0664,
	"hostowner", {Qhostowner}, 0, 0664,
	"kmesg", {Qkmesg}, 0, 0440,
	"kprint", {Qkpr int unused_int, 0, QTEXCL}, 0, DMEXCL | 0440,
	"null", {Qnull}, 0, 0666,
	"osversion", {Qosversion}, 0, 0444,
	"pgrpid", {Qpgrpid}, NUMSIZE, 0444,
	"pid", {Qpid}, NUMSIZE, 0444,
	"ppid", {Qppid}, NUMSIZE, 0444,
	"random", {Qrandom}, 0, 0444,
	"reboot", {Qreboot}, 0, 0660,
	"swap", {Qswap}, 0, 0664,
	"sysname", {Qsysname}, 0, 0664,
	"sysstat", {Qsysstat}, 0, 0666,
	"time", {Qtime}, NUMSIZE + 3 * VLNUMSIZE, 0664,
	"user", {Quser}, 0, 0666,
	"zero", {Qzero}, 0, 0444,
	"config", {Qconfig}, 0, 0444,
};

int readnum(uint32_t off, char *buf, uint32_t n, uint32_t val, int size)
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

int readstr(uint32_t off, char *buf, uint32_t n, char *str)
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
	todinit();
	randominit();
	/*
	 * at 115200 baud, the 1024 char buffer takes 56 ms to process,
	 * processing it every 22 ms should be fine
	 */
	addclock0link(kbdputcclock, 22);
}

static struct chan *consattach(char *spec)
{
	return devattach('c', spec);
}

static struct walkqid *conswalk(struct chan * c, struct chan * nc, char **name,
			 int nname)
{
	return devwalk(c, nc, name, nname, consdir, ARRAY_SIZE(consdir), devgen);
}

static int consstat(struct chan * c, uint8_t * dp, int n)
{
	return devstat(c, dp, n, consdir, ARRAY_SIZE(consdir), devgen);
}

static struct chan *consopen(struct chan * c, int omode)
{
	c->aux = NULL;
	c = devopen(c, omode, consdir, ARRAY_SIZE(consdir), devgen);
	switch ((uint32_t) c->qid.path) {
		case Qconsctl:
			kref_get(&(&kbd.ctl)->ref, 1);
			break;

		case Qkprint:
			if (tas(&kprintinuse) != 0) {
				c->flag &= ~COPEN;
				error(Einuse);
			}
			if (kprintoq == NULL) {
				kprintoq = qopen(8 * 1024, Qcoalesce, 0, 0);
				if (kprintoq == NULL) {
					c->flag &= ~COPEN;
					error(Enomem);
				}
				qdropoverflow(kprintoq, 1);
			} else
				qreopen(kprintoq);
			c->iounit = qiomaxatomic;
			break;
	}
	return c;
}

static void consclose(struct chan * c)
{
	switch ((uint32_t) c->qid.path) {
			/* last close of control file turns off raw */
		case Qconsctl:
			if (c->flag & COPEN) {
				if (kref_put(&(&kbd.ctl)->ref) == 0)
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

static long consread(struct chan * c, void *buf, long n, int64_t off)
{
	uint32_t l;
	Mach *mp;
	char *b, *bp, ch;
	char tmp[256];				/* must be >= 18*NUMSIZE (Qswap) */
	int i, k, id, send;
	int64_t offset = off;
	extern char configfile[];

	if (n <= 0)
		return n;

	switch ((uint32_t) c->qid.path) {
		case Qdir:
			return devdirread(c, buf, n, consdir, ARRAY_SIZE(consdir), devgen);

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
						case 0x15:	/* ^U */
							kbd.x = 0;
							break;
						case '\n':
						case 0x04:	/* ^D */
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

		case Qcputime:
			k = offset;
			if (k >= 6 * NUMSIZE)
				return 0;
			if (k + n > 6 * NUMSIZE)
				n = 6 * NUMSIZE - k;
			/* easiest to format in a separate buffer and copy out */
			for (i = 0; i < 6 && NUMSIZE * i < k + n; i++) {
				l = up->time[i];
				if (i == TReal)
					l = MACHP(0)->ticks - l;
				l = TK2MS(l);
				readnum(0, tmp + NUMSIZE * i, NUMSIZE, l, NUMSIZE);
			}
			memmove(buf, tmp + k, n);
			return n;

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
			return readnum((uint32_t) offset, buf, n,
				       up->pgrp->pgrpid, NUMSIZE);

		case Qpid:
			return readnum((uint32_t) offset, buf, n, up->pid,
				       NUMSIZE);

		case Qppid:
			return readnum((uint32_t) offset, buf, n,
				       up->parentpid, NUMSIZE);

		case Qtime:
			return readtime((uint32_t) offset, buf, n);

		case Qbintime:
			return readbintime(buf, n);

		case Qhostowner:
			return readstr((uint32_t) offset, buf, n, eve);

		case Qhostdomain:
			return readstr((uint32_t) offset, buf, n,
				       hostdomain);

		case Quser:
			return readstr((uint32_t) offset, buf, n, up->user);

		case Qnull:
			return 0;

		case Qconfig:
			return readstr((uint32_t) offset, buf, n,
				       configfile);

		case Qsysstat:
			b = kzmalloc(conf.nmach * (NUMSIZE * 11 + 1) + 1, 0);	/* +1 for NUL */
			bp = b;
			for (id = 0; id < 32; id++) {
				if (active.machs & (1 << id)) {
					mp = MACHP(id);
					readnum(0, bp, NUMSIZE, id, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->cs, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->intr, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->syscall, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->pfault, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->tlbfault, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->tlbpurge, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE, mp->load, NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE,
							(mp->perf.avg_inidle * 100) / mp->perf.period,
							NUMSIZE);
					bp += NUMSIZE;
					readnum(0, bp, NUMSIZE,
							(mp->perf.avg_inintr * 100) / mp->perf.period,
							NUMSIZE);
					bp += NUMSIZE;
					*bp++ = '\n';
				}
			}
			if (waserror()) {
				kfree(b);
				nexterror();
			}
			n = readstr((uint32_t) offset, buf, n, b);
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

			return readstr((uint32_t) offset, buf, n, tmp);

		case Qsysname:
			if (sysname == NULL)
				return 0;
			return readstr((uint32_t) offset, buf, n, sysname);

		case Qrandom:
			return randomread(buf, n);

		case Qdrivers:
			b = kzmalloc(READSTR, 0);
			if (b == NULL)
				error(Enomem);
			k = 0;
			for (i = 0; devtab[i] != NULL; i++)
				k += snprintf(b + k, READSTR - k, "#%C %s\n",
							 devtab[i]->dc, devtab[i]->name);
			if (waserror()) {
				kfree(b);
				nexterror();
			}
			n = readstr((uint32_t) offset, buf, n, b);
			kfree(b);
			poperror();
			return n;

		case Qzero:
			memset(buf, 0, n);
			return n;

		case Qosversion:
			snprintf(tmp, sizeof tmp, "2000");
			n = readstr((uint32_t) offset, buf, n, tmp);
			return n;

		default:
			printd("consread %#llux\n", c->qid.path);
			error(Egreg);
	}
	return -1;	/* never reached */
}

static long conswrite(struct chan * c, void *va, long n, int64_t off)
{
	char buf[256], ch;
	long l, bp;
	char *a;
	Mach *mp;
	int id, fd;
	struct chan *swc;
	uint32_t offset;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	a = va;
	offset = off;

	switch ((uint32_t) c->qid.path) {
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

		case Qconsctl:
			if (n > sizeof(buf))
				n = sizeof(buf);
			strlcpy(buf, a, sizeof(buf));
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
				if (a = strchr(a, ' '))
					a++;
			}
			break;

		case Qtime:
			if (!iseve())
				error(Eperm);
			return writetime(a, n);

		case Qbintime:
			if (!iseve())
				error(Eperm);
			return writebintime(a, n);

		case Qhostowner:
			return hostownerwrite(a, n);

		case Qhostdomain:
			return hostdomainwrite(a, n);

		case Quser:
			return userwrite(a, n);

		case Qnull:
			break;

		case Qconfig:
			error(Eperm);
			break;

		case Qreboot:
			if (!iseve())
				error(Eperm);
			cb = parsecmd(a, n);

			if (waserror()) {
				kfree(cb);
				nexterror();
			}
			ct = lookupcmd(cb, rebootmsg, ARRAY_SIZE(rebootmsg));
			switch (ct->index) {
				case CMhalt:
					reboot(NULL, 0, 0);
					break;
				case CMreboot:
					rebootcmd(cb->nf - 1, cb->f + 1);
					break;
				case CMpanic:
					*(uint32_t *) 0 = 0;
					panic("/dev/reboot");
			}
			poperror();
			kfree(cb);
			break;

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
				error(Egreg);
			memmove(buf, va, n);	/* so we can NUL-terminate */
			buf[n] = 0;
			/* start a pager if not already started */
			if (strncmp(buf, "start", 5) == 0) {
				kickpager();
				break;
			}
			if (!iseve())
				error(Eperm);
			if (buf[0] < '0' || '9' < buf[0])
				error(Ebadarg);
			fd = strtoul(buf, 0, 0);
			swc = fdtochan(fd, -1, 1, 1);
			setswapchan(swc);
			break;

		case Qsysname:
			if (offset != 0)
				error(Ebadarg);
			if (n <= 0 || n >= sizeof buf)
				error(Ebadarg);
			strlcpy(buf, a, sizeof(buf));
			if (buf[n - 1] == '\n')
				buf[n - 1] = 0;
			kstrdup(&sysname, buf);
			break;

		default:
			printd("conswrite: %#llux\n", c->qid.path);
			error(Egreg);
	}
	return n;
}

struct dev consdevtab = {
	'c',
	"cons",

	devreset,
	consinit,
	devshutdown,
	consattach,
	conswalk,
	consstat,
	consopen,
	devcreate,
	consclose,
	consread,
	devbread,
	conswrite,
	devbwrite,
	devremove,
	devwstat,
};

static uint32_t randn;

static void seedrand(void)
{
	ERRSTACK(2);
	if (!waserror()) {
		randomread((void *)&randn, sizeof(randn));
		poperror();
	}
}

int nrand(int n)
{
	if (randn == 0)
		seedrand();
	randn = randn * 1103515245 + 12345 + MACHP(0)->ticks;
	return (randn >> 16) % n;
}

int rand(void)
{
	nrand(1);
	return randn;
}

static uint64_t uvorder = 0x0001020304050607ULL;

static uint8_t *le2int64_t(int64_t * to, uint8_t * f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t *) to;
	o = (uint8_t *) & uvorder;
	for (i = 0; i < sizeof(int64_t); i++)
		t[o[i]] = f[i];
	return f + sizeof(int64_t);
}

static uint8_t *int64_t2le(uint8_t * t, int64_t from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t *) & from;
	o = (uint8_t *) & uvorder;
	for (i = 0; i < sizeof(int64_t); i++)
		t[i] = f[o[i]];
	return t + sizeof(int64_t);
}

static long order = 0x00010203;

static uint8_t *le2long(long *to, uint8_t * f)
{
	uint8_t *t, *o;
	int i;

	t = (uint8_t *) to;
	o = (uint8_t *) & order;
	for (i = 0; i < sizeof(long); i++)
		t[o[i]] = f[i];
	return f + sizeof(long);
}

static uint8_t *long2le(uint8_t * t, long from)
{
	uint8_t *f, *o;
	int i;

	f = (uint8_t *) & from;
	o = (uint8_t *) & order;
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

	nsec = todget(&ticks);
	if (fasthz == 0LL)
		fastticks((uint64_t *) & fasthz);
	sec = nsec / 1000000000ULL;
	snprintf(str, sizeof(str), "%*lud %*llud %*llud %*llud ",
			NUMSIZE - 1, sec,
			VLNUMSIZE - 1, nsec, VLNUMSIZE - 1, ticks, VLNUMSIZE - 1, fasthz);
	return readstr(off, buf, n, str);
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
		error(Ebadtimectl);
	strlcpy(b, buf, sizeof(b));
	i = strtol(b, 0, 0);
	if (i <= 0)
		error(Ebadtimectl);
	now = i * 1000000000LL;
	todset(now, 0, 0);
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
	uint8_t *b = (uint8_t *) buf;

	i = 0;
	if (fasthz == 0LL)
		fastticks((uint64_t *) & fasthz);
	nsec = todget(&ticks);
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
	int64_t delta;
	long period;

	n--;
	p = (uint8_t *) buf + 1;
	switch (*buf) {
		case 'n':
			if (n < sizeof(int64_t))
				error(Ebadtimectl);
			le2int64_t(&delta, p);
			todset(delta, 0, 0);
			break;
		case 'd':
			if (n < sizeof(int64_t) + sizeof(long))
				error(Ebadtimectl);
			p = le2int64_t(&delta, p);
			le2long(&period, p);
			todset(-1, delta, period);
			break;
		case 'f':
			if (n < sizeof(uint64_t))
				error(Ebadtimectl);
			le2int64_t(&fasthz, p);
			if (fasthz <= 0)
				error(Ebadtimectl);
			todsetfreq(fasthz);
			break;
	}
	return n;
}
