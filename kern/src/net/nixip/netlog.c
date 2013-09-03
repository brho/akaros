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

enum {
	Nlog		= 16*1024,
};

/*
 *  action log
 */
struct netlog {
	spinlock_t rwlock;
	int	opens;
	char*	buf;
	char	*end;
	char	*rptr;
	int	len;

	int	logmask;			/* mask of things to debug */
	uint8_t	iponly[IPaddrlen];		/* ip address to print debugging for */
	int	iponlyset;

	qlock_t qlock;
	int Rendez; /* put the real thing here. */
};

struct netlogflag {
	char*	name;
	int	mask;
};

static struct netlogflag flags[] =
{
	{ "ppp",	Logppp, },
	{ "ip",		Logip, },
	{ "fs",		Logfs, },
	{ "tcp",	Logtcp, },
	{ "icmp",	Logicmp, },
	{ "udp",	Logudp, },
	{ "compress",	Logcompress, },
	{ "gre",	Loggre, },
	{ "tcpwin",	Logtcp|Logtcpwin, },
	{ "tcprxmt",	Logtcp|Logtcprxmt, },
	{ "udpmsg",	Logudp|Logudpmsg, },
	{ "ipmsg",	Logip|Logipmsg, },
	{ "esp",	Logesp, },
	{ NULL,		0, },
};

char Ebadnetctl[] = "too few arguments for netlog control message";

enum
{
	CMset,
	CMclear,
	CMonly,
};

static
struct cmdtab routecmd[] = {
	{CMset,		"set",		0},
	{CMclear,	"clear",	0},
	{CMonly,		"only",		0},
};

void
netloginit(struct fs *f)
{
	f->alog = kmalloc(sizeof(struct netlog), 0);
}

void
netlogopen(struct fs *f)
{
	ERRSTACK(2);
	spin_lock(&f->alog->rwlock);
	if(waserror()){
		spin_unlock(&f->alog->rwlock);
		nexterror();
	}
	if(f->alog->opens == 0){
		if(f->alog->buf == NULL)
			f->alog->buf = kmalloc(Nlog, 0);
		if(f->alog->buf == NULL)
			error(Enomem);
		f->alog->rptr = f->alog->buf;
		f->alog->end = f->alog->buf + Nlog;
	}
	f->alog->opens++;
	spin_unlock(&f->alog->rwlock);
	poperror();
}

void
netlogclose(struct fs *f)
{
	ERRSTACK(2);
	spin_lock(&f->alog->rwlock);
	if(waserror()){
		spin_unlock(&f->alog->rwlock);
		nexterror();
	}
	f->alog->opens--;
	if(f->alog->opens == 0){
		kfree(f->alog->buf);
		f->alog->buf = NULL;
	}
	spin_unlock(&f->alog->rwlock);
	poperror();
}

static int
netlogready(void *a)
{
	struct fs *f = a;

	return f->alog->len;
}

long
netlogread(struct fs *f, void *a, uint32_t unused_len, long n)
{
	ERRSTACK(2);
	int i, d;
	char *p, *rptr;

	qlock(&f->alog->qlock);
	if(waserror()){
		qunlock(&f->alog->qlock);
		nexterror();
	}

	for(;;){
		spin_lock(&f->alog->rwlock);
		if(f->alog->len){
			if(n > f->alog->len)
				n = f->alog->len;
			d = 0;
			rptr = f->alog->rptr;
			f->alog->rptr += n;
			if(f->alog->rptr >= f->alog->end){
				d = f->alog->rptr - f->alog->end;
				f->alog->rptr = f->alog->buf + d;
			}
			f->alog->len -= n;
			spin_unlock(&f->alog->rwlock);

			i = n-d;
			p = a;
			memmove(p, rptr, i);
			memmove(p+i, f->alog->buf, d);
			break;
		}
		else
			spin_unlock(&f->alog->rwlock);

		sleep(f->alog, netlogready, f);
	}

	qunlock(&f->alog->qlock);
	poperror();

	return n;
}

void
netlogctl(struct fs *f, char* s, int n)
{
	ERRSTACK(2);
	int i, set = 0;
	struct netlogflag *fp;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	cb = parsecmd(s, n);
	if(waserror()){
		kfree(cb);
		nexterror();
	}

	if(cb->nf < 2)
		error(Ebadnetctl);

	ct = lookupcmd(cb, routecmd, ARRAY_SIZE(routecmd));

	switch(ct->index){
	case CMset:
		set = 1;
		break;

	case CMclear:
		set = 0;
		break;

	case CMonly:
		parseip(f->alog->iponly, cb->f[1]);
		if(ipcmp(f->alog->iponly, IPnoaddr) == 0)
			f->alog->iponlyset = 0;
		else
			f->alog->iponlyset = 1;
		kfree(cb);
		poperror();
		return;

	default:
		cmderror(cb, "unknown netlog control message");
	}

	for(i = 1; i < cb->nf; i++){
		for(fp = flags; fp->name; fp++)
			if(strcmp(fp->name, cb->f[i]) == 0)
				break;
		if(fp->name == NULL)
			continue;
		if(set)
			f->alog->logmask |= fp->mask;
		else
			f->alog->logmask &= ~fp->mask;
	}

	kfree(cb);
	poperror();
}

void
netlog(struct fs *f, int mask, char *fmt, ...)
{
	char buf[256], *t, *fp;
	int i, n;
	va_list arg;

	if(!(f->alog->logmask & mask))
		return;

	if(f->alog->opens == 0)
		return;

	va_start(arg, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, arg);
	va_end(arg);

	spin_lock(&f->alog->rwlock);
	i = f->alog->len + n - Nlog;
	if(i > 0){
		f->alog->len -= i;
		f->alog->rptr += i;
		if(f->alog->rptr >= f->alog->end)
			f->alog->rptr = f->alog->buf + (f->alog->rptr - f->alog->end);
	}
	t = f->alog->rptr + f->alog->len;
	fp = buf;
	f->alog->len += n;
	while(n-- > 0){
		if(t >= f->alog->end)
			t = f->alog->buf + (t - f->alog->end);
		*t++ = *fp++;
	}
	spin_unlock(&f->alog->rwlock);

	wakeup(f->alog);
}
