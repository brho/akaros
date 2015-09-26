/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

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
#include <monitor.h>
#include <ros/vmm.h>

struct dev consdevtab;

static char *devname(void)
{
	return consdevtab.name;
}

extern char *eve;
/* much stuff not ready yet. */

extern int cflag;

/* this would be good to have; when processes die, keep them around so we can
 * debug them.
 */
int keepbroken;

#if 0
void (*serwrite) (char *, int);

struct queue *kscanq;			/* keyboard raw scancodes (when needed) */
char *kscanid;					/* name of raw scan format (if defined) */
struct queue *kbdq;				/* unprocessed console input */
struct queue *lineq;			/* processed console input */
struct queue *printq;			/* console output */
struct queue *klogq;			/* kernel print (log) output */
int iprintscreenputs;

static struct {
	rwlock_t rwlock;
	Queue *q;
} kprintq;

static struct {
	qlock_t qlock;

	int raw;					/* true if we shouldn't process input */
	int ctl;					/* number of opens to the control file */
	int kbdr;					/* number of open reads to the keyboard */
	int scan;					/* true if reading raw scancodes */
	int x;						/* index into line */
	char line[1024];			/* current input line */

	char c;
	int count;
	int repeat;
} kbd;

#endif
char *sysname = "Your machine";
char *eve = "eve";

enum {
	CMreboot,
	CMhalt,
	CMpanic,
	CMbroken,
	CMnobroken,
	CMconsole,
	CMV,
};

static struct cmdtab sysctlcmd[] = {
	{CMreboot, "reboot", 0},
	{CMhalt, "halt", 0},
	{CMpanic, "panic", 0},
	{CMconsole, "console", 1},
	{CMbroken, "broken", 0},
	{CMnobroken, "nobroken", 0},
	{CMV, "V", 4},
};

void printinit(void)
{
#if 0
	lineq = qopen(2 * 1024, 0, NULL, NULL);
	if (lineq == NULL)
		panic("printinit");
	qdropoverflow(lineq, 1);
#endif
}

/*
 *  return true if current user is eve
 */
int iseve(void)
{
#if 0
	Osenv *o;

	o = up->env;
	return strcmp(eve, o->user) == 0;
#endif
	return 1;
}

#if 0
static int consactive(void)
{
	if (printq)
		return qlen(printq) > 0;
	return 0;
}

static void prflush(void)
{
	uint32_t now;

	now = m->ticks;
	while (serwrite == NULL && consactive())
		if (m->ticks - now >= HZ)
			break;
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
	char buf[PRINTSIZE + 2];
	ERRSTACK(1);

	/*
	 *  if kprint is open, put the message there, otherwise
	 *  if there's an attached bit mapped display,
	 *  put the message there.
	 */
	m = consoleprint;
	if (canrlock(&(&kprintq)->rwlock)) {
		if (kprintq.q != NULL) {
			if (waserror()) {
				runlock(&(&kprintq)->rwlock);
				nexterror();
			}
			if (usewrite)
				qwrite(kprintq.q, str, n);
			else
				qiwrite(kprintq.q, str, n);
			poperror();
			m = 0;
		}
		runlock(&(&kprintq)->rwlock);
	}
	if (m && screenputs != NULL)
		screenputs(str, n);

	/*
	 *  if there's a serial line being used as a console,
	 *  put the message there.
	 */
	if (serwrite != NULL) {
		serwrite(str, n);
		return;
	}

	if (printq == 0)
		return;

	while (n > 0) {
		t = memchr(str, '\n', n);
		if (t && !kbd.raw) {
			m = t - str;
			if (m > sizeof(buf) - 2)
				m = sizeof(buf) - 2;
			memmove(buf, str, m);
			buf[m] = '\r';
			buf[m + 1] = '\n';
			if (usewrite)
				qwrite(printq, buf, m + 2);
			else
				qiwrite(printq, buf, m + 2);
			str = t + 1;
			n -= m + 1;
		} else {
			if (usewrite)
				qwrite(printq, str, n);
			else
				qiwrite(printq, str, n);
			break;
		}
	}
}

/*
 * mainly for libmp
 */
void sysfatal(char *fmt, ...)
{
	va_list arg;
	char buf[64];

	va_start(arg, fmt);
	vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);
	error(EFAIL, buf);
}

int pprint(char *fmt, ...)
{
	ERRSTACK(1);
	int n;
	struct chan *c;
	Osenv *o;
	va_list arg;
	char buf[2 * PRINTSIZE];

	n = sprint(buf, "%s %ld: ", up->text, up->pid);
	va_start(arg, fmt);
	n = vseprintf(buf + n, buf + sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	o = up->env;
	if (o->fgrp == 0) {
		printd("%s", buf);
		return 0;
	}
	/* TODO: this is probably wrong (VFS hack) */
	c = o->fgrp->fd[2];
	if (c == 0 || (c->mode != OWRITE && c->mode != ORDWR)) {
		printd("%s", buf);
		return 0;
	}

	if (waserror()) {
		printd("%s", buf);
		poperror();
		return 0;
	}
	devtab[c->type].write(c, buf, n, c->offset);
	poperror();

	spin_lock(&c->lock);
	c->offset += n;
	spin_unlock(&c->lock);

	return n;
}

void echo(Rune r, char *buf, int n)
{
	if (kbd.raw)
		return;

	if (r == '\n') {
		if (printq)
			qiwrite(printq, "\r", 1);
	} else if (r == 0x15) {
		buf = "^U\n";
		n = 3;
	}
	if (consoleprint && screenputs != NULL)
		screenputs(buf, n);
	if (printq)
		qiwrite(printq, buf, n);
}
#endif
#if 0
/*
 *	Debug key support.  Allows other parts of the kernel to register debug
 *	key handlers, instead of devcons.c having to know whatever's out there.
 *	A kproc is used to invoke most handlers, rather than tying up the CPU at
 *	splhi, which can choke some device drivers (eg softmodem).
 */
typedef struct {
	Rune r;
	char *m;
	void (*f) (Rune);
	int i;						/* function called at interrupt time */
} Dbgkey;

static struct {
	Rendez;
	Dbgkey *work;
	Dbgkey keys[50];
	int nkeys;
	int on;
} dbg;

static Dbgkey *finddbgkey(Rune r)
{
	int i;
	Dbgkey *dp;

	for (dp = dbg.keys, i = 0; i < dbg.nkeys; i++, dp++)
		if (dp->r == r)
			return dp;
	return NULL;
}

static int dbgwork(void *)
{
	return dbg.work != 0;
}

static void dbgproc(void *)
{
	Dbgkey *dp;

	setpri(PriRealtime);
	for (;;) {
		do {
			rendez_sleep(&dbg, dbgwork, 0);
			dp = dbg.work;
		} while (dp == NULL);
		dp->f(dp->r);
		dbg.work = NULL;
	}
}

void debugkey(Rune r, char *msg, void (*fcn) (), int iflag)
{
	Dbgkey *dp;

	if (dbg.nkeys >= ARRAY_SIZE(dbg.keys))
		return;
	if (finddbgkey(r) != NULL)
		return;
	for (dp = &dbg.keys[dbg.nkeys++] - 1; dp >= dbg.keys; dp--) {
		if (strcmp(dp->m, msg) < 0)
			break;
		dp[1] = dp[0];
	}
	dp++;
	dp->r = r;
	dp->m = msg;
	dp->f = fcn;
	dp->i = iflag;
}

static int isdbgkey(Rune r)
{
	static int ctrlt;
	Dbgkey *dp;
	int echoctrlt = ctrlt;

	/*
	 * ^t hack BUG
	 */
	if (dbg.on || (ctrlt >= 2)) {
		if (r == 0x14 || r == 0x05) {
			ctrlt++;
			return 0;
		}
		if (dp = finddbgkey(r)) {
			if (dp->i || ctrlt > 2)
				dp->f(r);
			else {
				dbg.work = dp;
				rendez_wakeup(&dbg);
			}
			ctrlt = 0;
			return 1;
		}
		ctrlt = 0;
	} else if (r == 0x14) {
		ctrlt++;
		return 1;
	} else
		ctrlt = 0;
	if (echoctrlt) {
		char buf[3];

		buf[0] = 0x14;
		while (--echoctrlt >= 0) {
			echo(buf[0], buf, 1);
			qproduce(kbdq, buf, 1);
		}
	}
	return 0;
}

static void dbgtoggle(Rune)
{
	dbg.on = !dbg.on;
	printd("Debug keys %s\n", dbg.on ? "HOT" : "COLD");
}

static void dbghelp(void)
{
	int i;
	Dbgkey *dp;
	Dbgkey *dp2;
	static char fmt[] = "%c: %-22s";

	dp = dbg.keys;
	dp2 = dp + (dbg.nkeys + 1) / 2;
	for (i = dbg.nkeys; i > 1; i -= 2, dp++, dp2++) {
		printd(fmt, dp->r, dp->m);
		printd(fmt, dp2->r, dp2->m);
		printd("\n");
	}
	if (i)
		printd(fmt, dp->r, dp->m);
	printd("\n");
}

static void debuginit(void)
{
	ktask("consdbg", dbgproc, NULL);
	debugkey('|', "HOT|COLD keys", dbgtoggle, 0);
	debugkey('?', "help", dbghelp, 0);
}
#endif
#if 0
/*
 *  Called by a uart interrupt for console input.
 *
 *  turn '\r' into '\n' before putting it into the queue.
 */
int kbdcr2nl(struct queue *q, int ch)
{
	if (ch == '\r')
		ch = '\n';
	return kbdputc(q, ch);
}

/*
 *  Put character, possibly a rune, into read queue at interrupt time.
 *  Performs translation for compose sequences
 *  Called at interrupt time to process a character.
 */
int kbdputc(struct queue *q, int ch)
{
	int n;
	char buf[3];
	Rune r;
	static Rune kc[15];
	static int nk, collecting = 0;

	r = ch;
	if (r == Latin) {
		collecting = 1;
		nk = 0;
		return 0;
	}
	if (collecting) {
		int c;
		nk += runetochar((char *unused_char_p_t)&kc[nk], &r);
		c = latin1(kc, nk);
		if (c < -1)	/* need more keystrokes */
			return 0;
		collecting = 0;
		if (c == -1) {	/* invalid sequence */
			echo(kc[0], (char *unused_char_p_t)kc, nk);
			qproduce(q, kc, nk);
			return 0;
		}
		r = (Rune) c;
	}
	kbd.c = r;
	n = runetochar(buf, &r);
	if (n == 0)
		return 0;
	if (!isdbgkey(r)) {
		echo(r, buf, n);
		qproduce(q, buf, n);
	}
	return 0;
}

void kbdrepeat(int rep)
{
	kbd.repeat = rep;
	kbd.count = 0;
}

void kbdclock(void)
{
	if (kbd.repeat == 0)
		return;
	if (kbd.repeat == 1 && ++kbd.count > HZ) {
		kbd.repeat = 2;
		kbd.count = 0;
		return;
	}
	if (++kbd.count & 1)
		kbdputc(kbdq, kbd.c);
}
#endif

enum {
	Qdir,
	Qcons,
	Qsysctl,
	Qvmctl,
	Qconsctl,
	Qdrivers,
	Qhostowner,
	Qkeyboard,
	Qklog,
	Qkprint,
	Qscancode,
	Qmemory,
	Qmsec,
	Qnull,
	Qrandom,
	Qsysname,
	Qtime,
	Qurandom,
	Quser,
	Qjit,
};

static struct dirtab consdir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"cons", {Qcons}, 0, 0660},
	{"consctl", {Qconsctl}, 0, 0220},
	{"sysctl", {Qsysctl}, 0, 0666},
	{"vmctl", {Qvmctl}, 0, 0666},
	{"drivers", {Qdrivers}, 0, 0444},
	{"hostowner", {Qhostowner}, 0, 0644},
	{"keyboard", {Qkeyboard}, 0, 0666},
	{"klog", {Qklog}, 0, 0444},
	{"kprint", {Qkprint}, 0, 0444},
	{"scancode", {Qscancode}, 0, 0444},
	{"memory", {Qmemory}, 0, 0444},
	{"msec", {Qmsec}, NUMSIZE, 0444},
	{"null", {Qnull}, 0, 0666},
	{"random", {Qrandom}, 0, 0444},
	{"sysname", {Qsysname}, 0, 0664},
	{"time", {Qtime}, 0, 0664},
	{"user", {Quser}, 0, 0644},
	{"urandom", {Qurandom}, 0, 0444},
	{"jit", {Qjit}, 0, 0666},
};

uint32_t boottime;				/* seconds since epoch at boot */

#if 0
void fddump()
{
	struct proc *p;
	Osenv *o;
	int i;
	struct chan *c;

	p = proctab(6);
	o = p->env;
	for (i = 0; i <= o->fgrp->maxfd; i++) {
		if ((c = o->fgrp->fd[i]) == NULL)
			continue;
		printd("%d: %s\n", i, c->name == NULL ? "???" : c->name->s);
	}
}
#endif

static void consinit(void)
{
	randominit();
#if 0
	debuginit();
	debugkey('f', "files/6", fddump, 0);
	debugkey('q', "panic", qpanic, 1);
	debugkey('r', "exit", rexit, 1);
	klogq = qopen(128 * 1024, 0, 0, 0);
#endif
}

static struct chan *consattach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *conswalk(struct chan *c, struct chan *nc, char **name,
								int nname)
{
	return devwalk(c, nc, name, nname, consdir, ARRAY_SIZE(consdir), devgen);
}

static int consstat(struct chan *c, uint8_t * dp, int n)
{
	return devstat(c, dp, n, consdir, ARRAY_SIZE(consdir), devgen);
}

#if 0
static void flushkbdline(struct queue *q)
{
	if (kbd.x) {
		qwrite(q, kbd.line, kbd.x);
		kbd.x = 0;
	}
}
#endif

static struct chan *consopen(struct chan *c, int omode)
{
	c->aux = 0;
#if 0
	switch ((uint32_t) c->qid.path) {
		case Qconsctl:
			if (!iseve())
				error(EPERM, NULL);
			qlock(&(&kbd)->qlock);
			kbd.ctl++;
			qunlock(&(&kbd)->qlock);
			break;

		case Qkeyboard:
			if ((omode & 3) != OWRITE) {
				qlock(&(&kbd)->qlock);
				kbd.kbdr++;
				flushkbdline(kbdq);
				kbd.raw = 1;
				qunlock(&(&kbd)->qlock);
			}
			break;

		case Qscancode:
			qlock(&(&kbd)->qlock);
			if (kscanq || !kscanid) {
				qunlock(&(&kbd)->qlock);
				c->flag &= ~COPEN;
				if (kscanq)
					error(EBUSY, NULL);
				else
					error(EINVAL, NULL);
			}
			kscanq = qopen(256, 0, NULL, NULL);
			qunlock(&(&kbd)->qlock);
			break;

		case Qkprint:
			if ((omode & 3) != OWRITE) {
				wlock(&(&kprintq)->rwlock);
				if (kprintq.q != NULL) {
					wunlock(&(&kprintq)->rwlock);
					error(EBUSY, NULL);
				}
				kprintq.q = qopen(32 * 1024, Qcoalesce, NULL, NULL);
				if (kprintq.q == NULL) {
					wunlock(&(&kprintq)->rwlock);
					error(ENOMEM, NULL);
				}
				qdropoverflow(kprintq.q, 1);
				wunlock(&(&kprintq)->rwlock);
				c->iounit = qiomaxatomic;
			}
			break;
	}
#endif
	return devopen(c, omode, consdir, ARRAY_SIZE(consdir), devgen);
}

static void consclose(struct chan *c)
{
	if ((c->flag & COPEN) == 0)
		return;

	switch ((uint32_t) c->qid.path) {
#if 0
		case Qconsctl:
			/* last close of control file turns off raw */
			qlock(&(&kbd)->qlock);
			if (--kbd.ctl == 0)
				kbd.raw = 0;
			qunlock(&(&kbd)->qlock);
			break;

		case Qkeyboard:
			if (c->mode != OWRITE) {
				qlock(&(&kbd)->qlock);
				--kbd.kbdr;
				qunlock(&(&kbd)->qlock);
			}
			break;

		case Qscancode:
			qlock(&(&kbd)->qlock);
			if (kscanq) {
				qfree(kscanq);
				kscanq = 0;
			}
			qunlock(&(&kbd)->qlock);
			break;

		case Qkprint:
			wlock(&(&kprintq)->rwlock);
			qfree(kprintq.q);
			kprintq.q = NULL;
			wunlock(&(&kprintq)->rwlock);
			break;
#endif
		default:
			break;
	}
}

/* we do it this way to avoid the many fun deadlock opportunities
 * we keep hitting. And, if you don't suck it
 * out soon enough, you lost it. No real effort to ensure goodness
 * here as it can get called anywhere. Barret will fix it.
 */
static uint8_t logbuffer[1 << 20];
static int index = 0;
static struct queue *logqueue = NULL;
static int reading_kmesg = 0;
void logbuf(int c)
{
	if (reading_kmesg)
		return;
	if (index > 1 << 20)
		return;
	logbuffer[index++] = c;
}

static long consread(struct chan *c, void *buf, long n, int64_t offset)
{
	ERRSTACK(1);
	int l;

	int ch, eol, i;
	char *p, tmp[128];
	char *cbuf = buf;

	if (n <= 0)
		return n;

	switch ((uint32_t) c->qid.path) {
		case Qdir:
			return devdirread(c, buf, n, consdir, ARRAY_SIZE(consdir), devgen);
		case Qsysctl:
			return readstr(offset, buf, n, "akaros");
#if 0
		case Qcons:
		case Qkeyboard:
			qlock(&(&kbd)->qlock);
			if (waserror()) {
				qunlock(&(&kbd)->qlock);
				nexterror();
			}
			if (kbd.raw || kbd.kbdr) {
				if (qcanread(lineq))
					n = qread(lineq, buf, n);
				else {
					/* read as much as possible */
					do {
						i = qread(kbdq, cbuf, n);
						cbuf += i;
						n -= i;
					} while (n > 0 && qcanread(kbdq));
					n = cbuf - (char *unused_char_p_t)buf;
				}
			} else {
				while (!qcanread(lineq)) {
					qread(kbdq, &kbd.line[kbd.x], 1);
					ch = kbd.line[kbd.x];
					eol = 0;
					switch (ch) {
						case '\b':
							if (kbd.x)
								kbd.x--;
							break;
						case 0x15:
							kbd.x = 0;
							break;
						case '\n':
						case 0x04:
							eol = 1;
						default:
							kbd.line[kbd.x++] = ch;
							break;
					}
					if (kbd.x == sizeof(kbd.line) || eol) {
						if (ch == 0x04)
							kbd.x--;
						qwrite(lineq, kbd.line, kbd.x);
						kbd.x = 0;
					}
				}
				n = qread(lineq, buf, n);
			}
			qunlock(&(&kbd)->qlock);
			poperror();
			return n;

		case Qscancode:
			if (offset == 0)
				return readstr(0, buf, n, kscanid);
			else
				return qread(kscanq, buf, n);

		case Qtime:
			snprintf(tmp, sizeof(tmp), "%.lld", (int64_t) mseconds() * 1000);
			return readstr(offset, buf, n, tmp);

		case Qhostowner:
			return readstr(offset, buf, n, eve);

		case Quser:
			return readstr(offset, buf, n, o->user);

		case Qjit:
			snprintf(tmp, sizeof(tmp), "%d", cflag);
			return readstr(offset, buf, n, tmp);
#endif
		case Qnull:
			return 0;
#if 0
		case Qmsec:
			return readnum(offset, buf, n, TK2MS(MACHP(0)->ticks), NUMSIZE);
#endif
		case Qsysname:
			if (sysname == NULL)
				return 0;
			return readstr(offset, buf, n, "Akaros");

		case Qrandom:
		case Qurandom:
			return randomread(buf, n);
#if 0
		case Qmemory:
			return poolread(buf, n, offset);
#endif
		case Qdrivers:
			p = kzmalloc(READSTR, 0);
			if (p == NULL)
				error(ENOMEM, NULL);
			l = 0;
			for (i = 0; &devtab[i] < __devtabend; i++)
				l += snprintf(p + l, READSTR - l, "#%s\n", devtab[i].name);
			if (waserror()) {
				kfree(p);
				nexterror();
			}
			n = readstr(offset, buf, n, p);
			kfree(p);
			poperror();
			return n;
		case Qklog:
			//return qread(klogq, buf, n);
			/* the queue gives us some elasticity for log reading. */
			if (!logqueue)
				logqueue = qopen(1 << 20, 0, 0, 0);
			if (logqueue) {
				int ret;
				/* atomic sets/gets are not that important in this case. */
				reading_kmesg = 1;
				qwrite(logqueue, logbuffer, index);
				index = 0;
				ret = qread(logqueue, buf, n);
				reading_kmesg = 0;
				return ret;
			}
			break;
#if 0
		case Qkprint:
			rlock(&(&kprintq)->rwlock);
			if (waserror()) {
				runlock(&(&kprintq)->rwlock);
				nexterror();
			}
			n = qread(kprintq.q, buf, n);
			poperror();
			runlock(&(&kprintq)->rwlock);
			return n;
#endif
		default:
			printd("consread %llu\n", c->qid.path);
			error(EINVAL, NULL);
	}
	return -1;	/* never reached */
}

static long conswrite(struct chan *c, void *va, long n, int64_t offset)
{
	ERRSTACK(1);
	int64_t t;
	uint64_t ip;
	long l, bp;
	char *a = va;
	struct cmdbuf *cb;
	struct cmdtab *ct;
	char buf[256];
	int x;
	uint64_t rip, rsp, cr3, flags, vcpu;
	int ret;
	struct vmctl vmctl;

	switch ((uint32_t) c->qid.path) {
#if 0
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
				putstrn0(a, bp, 1);
				a += bp;
				l -= bp;
			}
			break;

		case Qconsctl:
			if (n >= sizeof(buf))
				n = sizeof(buf) - 1;
			strncpy(buf, a, n);
			buf[n] = 0;
			for (a = buf; a;) {
				if (strncmp(a, "rawon", 5) == 0) {
					qlock(&(&kbd)->qlock);
					flushkbdline(kbdq);
					kbd.raw = 1;
					qunlock(&(&kbd)->qlock);
				} else if (strncmp(a, "rawoff", 6) == 0) {
					qlock(&(&kbd)->qlock);
					kbd.raw = 0;
					kbd.x = 0;
					qunlock(&(&kbd)->qlock);
				}
				if (a = strchr(a, ' '))
					a++;
			}
			break;

		case Qkeyboard:
			for (x = 0; x < n;) {
				Rune r;
				x += chartorune(&r, &a[x]);
				kbdputc(kbdq, r);
			}
			break;

		case Qtime:
			if (n >= sizeof(buf))
				n = sizeof(buf) - 1;
			strncpy(buf, a, n);
			buf[n] = 0;
			t = strtoll(buf, 0, 0) / 1000000;
			boottime = t - TK2SEC(MACHP(0)->ticks);
			break;

		case Qhostowner:
			if (!iseve())
				error(EPERM, NULL);
			if (offset != 0 || n >= sizeof(buf))
				error(EINVAL, NULL);
			memmove(buf, a, n);
			buf[n] = '\0';
			if (n > 0 && buf[n - 1] == '\n')
				buf[--n] = 0;
			if (n <= 0)
				error(EINVAL, NULL);
			renameuser(eve, buf);
			renameproguser(eve, buf);
			kstrdup(&eve, buf);
			kstrdup(&up->env->user, buf);
			break;

		case Quser:
			if (!iseve())
				error(EPERM, NULL);
			if (offset != 0)
				error(EINVAL, NULL);
			if (n <= 0 || n >= sizeof(buf))
				error(EINVAL, NULL);
			strncpy(buf, a, n);
			buf[n] = 0;
			if (buf[n - 1] == '\n')
				buf[n - 1] = 0;
			kstrdup(&up->env->user, buf);
			break;

		case Qjit:
			if (n >= sizeof(buf))
				n = sizeof(buf) - 1;
			strncpy(buf, va, n);
			buf[n] = '\0';
			x = atoi(buf);
			if (x < 0 || x > 9)
				error(EINVAL, NULL);
			cflag = x;
			return n;

		case Qnull:
			break;

		case Qsysname:
			if (offset != 0)
				error(EINVAL, NULL);
			if (n <= 0 || n >= sizeof(buf))
				error(EINVAL, NULL);
			strncpy(buf, a, n);
			buf[n] = 0;
			if (buf[n - 1] == '\n')
				buf[n - 1] = 0;
			kstrdup(&sysname, buf);
			break;
#endif
		case Qvmctl:
			memmove(&vmctl, a, sizeof(vmctl));
			if ((offset >> 12) ==1) {
				ret = vm_post_interrupt(&vmctl);
				n = ret;
				printk("vm_interrupt_notify returns %d\n", ret);
			}
			else {
				ret = vm_run(&vmctl);
				printd("vm_run returns %d\n", ret);
				n = ret;
				memmove(a, &vmctl, sizeof(vmctl));
			}
			break;
		case Qsysctl:
			//if (!iseve()) error(EPERM, NULL);
			cb = parsecmd(a, n);
			if (cb->nf > 1)
			printd("cons sysctl cmd %s\n", cb->f[0]);
			if (waserror()) {
				kfree(cb);
				nexterror();
			}
			ct = lookupcmd(cb, sysctlcmd, ARRAY_SIZE(sysctlcmd));
			switch (ct->index) {
				case CMreboot:
					reboot();
					break;
				case CMhalt:
					cpu_halt();
					break;
				case CMpanic:
					panic("sysctl");
					//case CMconsole:
					//consoleprint = strcmp(cb->f[1], "off") != 0;
					break;
				case CMbroken:
					keepbroken = 1;
					break;
				case CMnobroken:
					keepbroken = 0;
					break;
				case CMV:
					/* it's ok to throw away this struct each time;
					 * this is stateless and going away soon anyway.
					 * we only kept it here until we can rewrite all the
					 * tests
					 */
					rip =  strtoul(cb->f[1], NULL, 0);
					rsp =  strtoul(cb->f[2], NULL, 0);
					cr3 =  strtoul(cb->f[3], NULL, 0);
					if (cr3) {
						vmctl.command = REG_RSP_RIP_CR3;
						vmctl.cr3 = cr3;
						vmctl.regs.tf_rip = rip;
						vmctl.regs.tf_rsp = rsp;
					} else {
						vmctl.command = RESUME;
					}
					ret = vm_run(&vmctl);
					printd("vm_run returns %d\n", ret);
					n = ret;
					break;
			}
			poperror();
			kfree(cb);
			break;
		default:
			printd("conswrite: %llu\n", c->qid.path);
			error(EINVAL, NULL);
	}
	return n;
}

struct dev consdevtab __devtab = {
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
	devpower,
	devchaninfo,
};

static uint32_t randn;

static void seedrand(void)
{
	randomread((void *)&randn, sizeof(randn));
}

int nrand(int n)
{
	if (randn == 0)
		seedrand();
	randn = randn * 1103515245 + 12345 + read_tsc();
	return (randn >> 16) % n;
}

int rand(void)
{
	nrand(1);
	return randn;
}

uint32_t truerand(void)
{
	uint32_t x;

	randomread(&x, sizeof(x));
	return x;
}

/* TODO: qlock_init this, if you ever use this */
qlock_t grandomlk;

void _genrandomqlock(void)
{
	qlock(&grandomlk);
}

void _genrandomqunlock(void)
{
	qunlock(&grandomlk);
}
