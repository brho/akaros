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
#include <net/ip.h>

enum {
	Nlog = 4 * 1024,
};

/*
 *  action log
 */
struct Netlog {
	spinlock_t lock;
	int opens;
	char *buf;
	char *end;
	char *rptr;
	int len;

	int logmask;			/* mask of things to debug */
	uint8_t iponly[IPaddrlen];	/* ip address to print debugging for */
	int iponlyset;

	qlock_t qlock;
	struct rendez r;
};

typedef struct Netlogflag {
	char *name;
	int mask;
} Netlogflag;

static Netlogflag flags[] = {
	{"ppp", Logppp,},
	{"ip", Logip,},
	{"fs", Logfs,},
	{"tcp", Logtcp,},
	{"il", Logil,},
	{"icmp", Logicmp,},
	{"udp", Logudp,},
	{"compress", Logcompress,},
	{"ilmsg", Logil | Logilmsg,},
	{"gre", Loggre,},
	{"tcpreset", Logtcp | Logtcpreset,},
	{"tcprxmt", Logtcp | Logtcprxmt,},
	{"tcpall", Logtcp | Logtcpreset | Logtcprxmt | Logtcpverbose,},
	{"udpmsg", Logudp | Logudpmsg,},
	{"ipmsg", Logip | Logipmsg,},
	{"esp", Logesp,},
	{NULL, 0,},
};

enum {
	CMset,
	CMclear,
	CMonly,
};

static struct cmdtab routecmd[] = {
	{CMset, "set", 0},
	{CMclear, "clear", 0},
	{CMonly, "only", 0},
};

void netloginit(struct Fs *f)
{
	f->alog = kzmalloc(sizeof(struct Netlog), 0);
	spinlock_init(&f->alog->lock);
	qlock_init(&f->alog->qlock);
	rendez_init(&f->alog->r);
}

void netlogopen(struct Fs *f)
{
	ERRSTACK(1);
	spin_lock(&f->alog->lock);
	if (waserror()) {
		spin_unlock(&f->alog->lock);
		nexterror();
	}
	if (f->alog->opens == 0) {
		if (f->alog->buf == NULL)
			f->alog->buf = kzmalloc(Nlog, 0);
		f->alog->rptr = f->alog->buf;
		f->alog->end = f->alog->buf + Nlog;
	}
	f->alog->opens++;
	spin_unlock(&f->alog->lock);
	poperror();
}

void netlogclose(struct Fs *f)
{
	ERRSTACK(1);
	spin_lock(&f->alog->lock);
	if (waserror()) {
		spin_unlock(&f->alog->lock);
		nexterror();
	}
	f->alog->opens--;
	if (f->alog->opens == 0) {
		kfree(f->alog->buf);
		f->alog->buf = NULL;
	}
	spin_unlock(&f->alog->lock);
	poperror();
}

static int netlogready(void *a)
{
	struct Fs *f = a;

	return f->alog->len;
}

long netlogread(struct Fs *f, void *a, uint32_t unused, long n)
{
	ERRSTACK(1);
	int i, d;
	char *p, *rptr;

	qlock(&f->alog->qlock);
	if (waserror()) {
		qunlock(&f->alog->qlock);
		nexterror();
	}

	for (;;) {
		spin_lock(&f->alog->lock);
		if (f->alog->len) {
			if (n > f->alog->len)
				n = f->alog->len;
			d = 0;
			rptr = f->alog->rptr;
			f->alog->rptr += n;
			if (f->alog->rptr >= f->alog->end) {
				d = f->alog->rptr - f->alog->end;
				f->alog->rptr = f->alog->buf + d;
			}
			f->alog->len -= n;
			spin_unlock(&f->alog->lock);

			i = n - d;
			p = a;
			memmove(p, rptr, i);
			memmove(p + i, f->alog->buf, d);
			break;
		} else
			spin_unlock(&f->alog->lock);

		rendez_sleep(&f->alog->r, netlogready, f);
	}

	qunlock(&f->alog->qlock);
	poperror();

	return n;
}

void netlogctl(struct Fs *f, char *s, int n)
{
	ERRSTACK(1);
	int i, set = 0;
	Netlogflag *fp;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	cb = parsecmd(s, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	if (cb->nf < 2)
		error(EINVAL, ERROR_FIXME);

	ct = lookupcmd(cb, routecmd, ARRAY_SIZE(routecmd));

	switch (ct->index) {
	case CMset:
		set = 1;
		break;

	case CMclear:
		set = 0;
		break;

	case CMonly:
		parseip(f->alog->iponly, cb->f[1]);
		if (ipcmp(f->alog->iponly, IPnoaddr) == 0)
			f->alog->iponlyset = 0;
		else
			f->alog->iponlyset = 1;
		kfree(cb);
		poperror();
		return;

	default:
		cmderror(cb, "unknown ip control message");
	}

	for (i = 1; i < cb->nf; i++) {
		for (fp = flags; fp->name; fp++)
			if (strcmp(fp->name, cb->f[i]) == 0)
				break;
		if (fp->name == NULL)
			continue;
		if (set)
			f->alog->logmask |= fp->mask;
		else
			f->alog->logmask &= ~fp->mask;
	}

	kfree(cb);
	poperror();
}

void netlog(struct Fs *f, int mask, char *fmt, ...)
{
	char buf[256], *t, *fp;
	int i, n;
	va_list arg;
	struct timespec ts_now;

	if (!(f->alog->logmask & mask))
		return;

	if (f->alog->opens == 0)
		return;

	/* Same style as trace_printk */
	if (likely(__proc_global_info.tsc_freq))
		ts_now = tsc2timespec(read_tsc());
	n = snprintf(buf, sizeof(buf), "[%lu.%09lu]: ",
	             ts_now.tv_sec, ts_now.tv_nsec);

	va_start(arg, fmt);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, arg);
	va_end(arg);

	spin_lock(&f->alog->lock);
	i = f->alog->len + n - Nlog;
	if (i > 0) {
		f->alog->len -= i;
		f->alog->rptr += i;
		if (f->alog->rptr >= f->alog->end)
			f->alog->rptr = f->alog->buf + (f->alog->rptr -
							f->alog->end);
	}
	t = f->alog->rptr + f->alog->len;
	fp = buf;
	f->alog->len += n;
	while (n-- > 0) {
		if (t >= f->alog->end)
			t = f->alog->buf + (t - f->alog->end);
		*t++ = *fp++;
	}
	spin_unlock(&f->alog->lock);

	rendez_wakeup(&f->alog->r);
}
