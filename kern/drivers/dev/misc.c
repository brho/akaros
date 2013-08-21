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


enum{
	Qdir,
	Qbintime,
	Qcons,
	Qconsctl,
	Qcputime,
	Qdrivers,
	Qkmesg,
	Qkprint,
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
	Qdebug,
};

enum
{
	VLNUMSIZE=	22,
};

static struct dirtab consdir[]={
	{".",	{Qdir, 0, QTDIR},	0,		DMDIR|0555},
	{"bintime",	{Qbintime},	24,		0664},
	{"cputime",	{Qcputime},	6*NUMSIZE,	0444},
	{"drivers",	{Qdrivers},	0,		0444},
	//	{"hostdomain",	{Qhostdomain},	DOMLEN,		0664},
	{"hostowner",	{Qhostowner},	0,		0664},
	{"null",		{Qnull},	0,		0666},
	{"osversion",	{Qosversion},	0,		0444},
	{"pgrpid",	{Qpgrpid},	NUMSIZE,	0444},
	{"pid",		{Qpid},		NUMSIZE,	0444},
	{"ppid",		{Qppid},	NUMSIZE,	0444},
	{"random",	{Qrandom},	0,		0444},
	{"reboot",	{Qreboot},	0,		0664},
	{"swap",		{Qswap},	0,		0664},
	{"sysname",	{Qsysname},	0,		0664},
	{"sysstat",	{Qsysstat},	0,		0666},
	{"time",		{Qtime},	NUMSIZE+3*VLNUMSIZE,	0664},
	{"user",		{Quser},	0,		0666},
	{"zero",		{Qzero},	0,		0444},
	{"debug",	{Qdebug},	0,		0666},
};

static void
consinit(void)
{
}

static struct chan*
consattach(char *spec, struct errbuf *perrbuf)
{
	return devattach('c', spec, perrbuf);
}

static struct walkqid*
conswalk(struct chan *c, struct chan *nc, char **name, int nname, struct errbuf *perrbuf)
{
	return devwalk(c, nc, name,nname, consdir, ARRAY_SIZE(consdir), devgen, perrbuf);
}

static long
consstat(struct chan *c, uint8_t *dp, long n, struct errbuf *perrbuf)
{
	return devstat(c, dp, n, consdir, ARRAY_SIZE(consdir), devgen, perrbuf);
}

static struct chan*
consopen(struct chan *c, int omode, struct errbuf *perrbuf)
{
	c->aux = NULL;
	c = devopen(c, omode, consdir, ARRAY_SIZE(consdir), devgen, perrbuf);
	return c;
}

static void
consclose(struct chan *c, struct errbuf *perrbuf)
{
}

static long
consread(struct chan *c, void *buf, long n, int64_t off, struct errbuf *perrbuf)
{
	uint32_t l;
	char *b, *bp, ch, *s, *e;
	char tmp[512];		/* Qswap is 381 bytes at clu */
	int i, k, id, send;
	long offset, nread;


	if(n <= 0)
		return n;

	nread = n;
	offset = off;
	switch((uint32_t)c->qid.path){
	case Qdir:
	  return devdirread(c, buf, n, consdir, ARRAY_SIZE(consdir), devgen, perrbuf);

	case Qcputime:
		error("not now");
#if 0
		k = offset;
		if(k >= 6*NUMSIZE)
			return 0;
		if(k+n > 6*NUMSIZE)
			n = 6*NUMSIZE - k;
		/* easiest to format in a separate buffer and copy out */
		for(i=0; i<6 && NUMSIZE*i<k+n; i++){
			l = current->time[i];
			if(i == TReal)
				l = sys->ticks - l;
			l = TK2MS(l);
			readnum(0, tmp+NUMSIZE*i, NUMSIZE, l, NUMSIZE);
		}
		memmove(buf, tmp+k, n);
#endif
		return n;

	case Qkmesg:
		error("not never?");
#if 0
		/*
		 * This is unlocked to avoid tying up a process
		 * that's writing to the buffer.  kmesg.n never
		 * gets smaller, so worst case the reader will
		 * see a slurred buffer.
		 */
		if(off >= kmesg.n)
			n = 0;
		else{
			if(off+n > kmesg.n)
				n = kmesg.n - off;
			memmove(buf, kmesg.buf+off, n);
		}
		return n;
#endif
#if 0
	case Qkprint:
		return qread(kprintoq, buf, n);
#endif
	case Qpgrpid:
		return readnum(offset, buf, n, current->pgrp->pgrpid, NUMSIZE);

	case Qpid:
		return readnum(offset, buf, n, current->pid, NUMSIZE);
#if 0
	case Qppid:
		return readnum(offset, buf, n, current->parentpid, NUMSIZE);

	case Qtime:
		return readtime(offset, buf, n);

	case Qbintime:
		return readbintime(buf, n);

	case Qhostowner:
		return readstr(offset, buf, n, eve);

	case Qhostdomain:
		return readstr(offset, buf, n, hostdomain);

	case Quser:
		return readstr(offset, buf, n, current->user);
#endif
	case Qnull:
		return 0;
#if 0
	case Qsysname:
		if(sysname == NULL)
			return 0;
		return readstr(offset, buf, n, sysname);

	case Qrandom:
		return randomread(buf, n);
#endif
	case Qdrivers:
		return devtabread(c, buf, n, off, perrbuf);

	case Qzero:
		memset(buf, 0, n);
		return n;

	case Qosversion:
		snprintf(tmp, sizeof tmp, "2000");
		n = readstr(offset, buf, n, tmp);
		return n;

	case Qdebug:
#if 0
		s = seprint(tmp, tmp + sizeof tmp, "locks %uld\n", lockstats.locks);
		s = seprint(s, tmp + sizeof tmp, "glare %uld\n", lockstats.glare);
		s = seprint(s, tmp + sizeof tmp, "inglare %uld\n", lockstats.inglare);
		s = seprint(s, tmp + sizeof tmp, "qlock %uld\n", qlockstats.qlock);
		seprint(s, tmp + sizeof tmp, "qlockq %uld\n", qlockstats.qlockq);
		return readstr(offset, buf, n, tmp);
#endif
		return 0;
		break;
	default:
		printd("consread %#llux\n", c->qid.path);
		error(Egreg);
	}
	return -1;		/* never reached */
}

static long
conswrite(struct chan *c, void *va, long n, int64_t off, struct errbuf *perrbuf)
{
	char buf[256], ch;
	long l, bp;
	char *a;
	int i;
	uint32_t offset;
	struct cmdbuf *cb;
	struct cmdtab *ct;
	a = va;
	offset = off;

	switch((uint32_t)c->qid.path){
#if 0
	case Qtime:
		if(!iseve())
			error(Eperm);
		return writetime(a, n);

	case Qbintime:
		if(!iseve())
			error(Eperm);
		return writebintime(a, n);

	case Qhostowner:
		return hostownerwrite(a, n);

	case Qhostdomain:
		return hostdomainwrite(a, n);

	case Quser:
		return userwrite(a, n);
#endif
	case Qnull:
		break;
#if 0
	case Qreboot:

		if(!iseve())
			error(Eperm);
		cb = parsecmd(a, n);

		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, rebootmsg, ARRAY_SIZE(rebootmsg));
		switch(ct->index) {
		case CMhalt:
			reboot(NULL, 0, 0);
			break;
		case CMreboot:
			rebootcmd(cb->nf-1, cb->f+1);
			break;
		case CMpanic:
			panic("/dev/reboot");
		}
		poperror();
		free(cb);
		break;

	case Qsysname:
		if(offset != 0)
			error(Ebadarg);
		if(n <= 0 || n >= sizeof buf)
			error(Ebadarg);
		strncpy(buf, a, n);
		buf[n] = 0;
		if(buf[n-1] == '\n')
			buf[n-1] = 0;
		kstrdup(&sysname, buf);
		break;

	case Qdebug:
		if(n >= sizeof(buf))
			n = sizeof(buf)-1;
		strncpy(buf, a, n);
		buf[n] = 0;
		if(n > 0 && buf[n-1] == '\n')
			buf[n-1] = 0;
		error(Ebadctl);
		break;
#endif
	default:
		printd("conswrite: %#llux\n", c->qid.path);
		error(Egreg);
	}
	return n;
}
struct dev miscdevtab = {
	'c',
	"misc",

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

