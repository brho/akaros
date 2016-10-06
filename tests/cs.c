/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <ctype.h>
#include <error.h>
#include <fcall.h>
#include <fcntl.h>
#include <iplib/iplib.h>
#include <ndblib/fcallfmt.h>
#include <ndblib/ndb.h>
#include <parlib/parlib.h>
#include <parlib/spinlock.h>
#include <pthread.h>
#include <ros/common.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
	Nreply = 20,
	Maxreply = 256,
	Maxrequest = 128,
	Maxpath = 128,
	Maxfdata = 8192,
	Maxhost = 64,    /* maximum host name size */
	Maxservice = 64, /* maximum service name size */

	Qdir = 0,
	Qcs = 1,
};

typedef struct Mfile Mfile;
typedef struct Mlist Mlist;
typedef struct Network Network;
typedef struct Flushreq Flushreq;
typedef struct Job Job;

int vers; /* incremented each clone/attach */
/* need to resolve the #inluce for all this stuff. */
#define DMDIR 0x80000000 /* mode bit for directories */
#define QTDIR 0x80
#define QTFILE 0
#define ERRMAX 128

struct Mfile {
	int busy;

	char *user;
	struct qid qid;
	int fid;

	/*
	 *  current request
	 */
	char *net;
	char *host;
	char *serv;
	char *rem;

	/*
	 *  result of the last lookup
	 */
	Network *nextnet;
	int nreply;
	char *reply[Nreply];
	int replylen[Nreply];
};

struct Mlist {
	Mlist *next;
	Mfile mf;
};

/*
 *  active requests
 */
struct Job {
	Job *next;
	int flushed;
	struct fcall request;
	struct fcall reply;
	pthread_t thread;
};

spinlock_t joblock = SPINLOCK_INITIALIZER;
Job *joblist;

Mlist *mlist;
int mfd[2];
int debug;
int paranoia;
int ipv6lookups = 1;
char *dbfile = "lib/ndb/local";
struct ndb *db, *netdb;

void rversion(Job *);
void rflush(Job *);
void rattach(Job *, Mfile *);
char *rwalk(Job *, Mfile *);
void ropen(Job *, Mfile *);
void rcreate(Job *, Mfile *);
void rread(Job *, Mfile *);
void rwrite(Job *, Mfile *);
void rclunk(Job *, Mfile *);
void rremove(Job *, Mfile *);
void rstat(Job *, Mfile *);
void rwstat(Job *, Mfile *);
void rauth(Job *);
void sendmsg(Job *, char *);
void mountinit(char *, char *);
void io(void);
void ndbinit(void);
void netinit(int);
void netadd(char *);
char *genquery(Mfile *, char *);
char *ipinfoquery(Mfile *, char **, int);
int needproto(Network *, struct ndbtuple *);
int lookup(Mfile *);
struct ndbtuple *reorder(struct ndbtuple *, struct ndbtuple *);
void ipid(void);
void readipinterfaces(void);
void *emalloc(int);
char *estrdup(char *);
Job *newjob(void);
void freejob(Job *);
void setext(char *, int, char *);
void cleanmf(Mfile *);

extern void paralloc(void);

spinlock_t dblock = SPINLOCK_INITIALIZER;  /* mutex on database operations */
spinlock_t netlock = SPINLOCK_INITIALIZER; /* mutex for netinit() */

char *logfile = "cs";
char *paranoiafile = "cs.paranoia";

char mntpt[Maxpath];
char netndb[Maxpath];

/*
 *  Network specific translators
 */
struct ndbtuple *iplookup(Network *, char *, char *, int);
char *iptrans(struct ndbtuple *, Network *, char *, char *, int);
struct ndbtuple *telcolookup(Network *, char *, char *, int);
char *telcotrans(struct ndbtuple *, Network *, char *, char *, int);
struct ndbtuple *dnsiplookup(char *, struct ndbs *);

struct Network {
	char *net;
	struct ndbtuple *(*lookup)(Network *, char *, char *, int);
	char *(*trans)(struct ndbtuple *, Network *, char *, char *, int);
	int considered;      /* flag: ignored for "net!"? */
	int fasttimeouthack; /* flag. was for IL */
	Network *next;
};

enum {
	Ntcp = 0,
};

/*
 *  net doesn't apply to (r)udp, icmp(v6), or telco (for speed).
 */
Network network[] = {
    [Ntcp] { "tcp",	iplookup,	iptrans,	0 }, {"udp", iplookup, iptrans, 1},
    {"icmp", iplookup, iptrans, 1},         {"icmpv6", iplookup, iptrans, 1},
    {"rudp", iplookup, iptrans, 1},         {"ssh", iplookup, iptrans, 1},
    {"telco", telcolookup, telcotrans, 1},  {0},
};

spinlock_t ipifclock = SPINLOCK_INITIALIZER;
struct ipifc *ipifcs;

char eaddr[16];         /* ascii ethernet address */
char ipaddr[64];        /* ascii internet address */
uint8_t ipa[IPaddrlen]; /* binary internet address */
char *mysysname;

Network *netlist; /* networks ordered by preference */
Network *last;

static void nstrcpy(char *to, char *from, int len)
{
	strncpy(to, from, len);
	to[len - 1] = 0;
}

char *argv0;
void usage(void)
{
	fprintf(stderr, "CS:usage: %s [-dn] [-f ndb-file] [-x netmtpt]\n", argv0);
	fprintf(stderr, "CS:usage");
	exit(1);
}

/*
 * based on libthread's threadsetname, but drags in less library code.
 * actually just sets the arguments displayed.
 */
void procsetname(char *fmt, ...)
{
/* someday ... */
#if 0
	int fd;
	char *cmdname;
	char buf[128];
	va_list arg;

	va_start(arg, fmt);
	cmdname = vsmprint(fmt, arg);
	va_end(arg);
	if (cmdname == NULL)
		return;
	snprintf(buf, sizeof buf, "#proc/%d/args", getpid());
	if((fd = open(buf, OWRITE)) >= 0){
		write(fd, cmdname, strlen(cmdname)+1);
		close(fd);
	}
	free(cmdname);
#endif
}

void main(int argc, char *argv[])
{
	int justsetname;
	char ext[Maxpath], servefile[Maxpath];
	argv0 = argv[0];
	justsetname = 0;
	setnetmtpt(mntpt, sizeof(mntpt), NULL);
	register_printf_specifier('F', printf_fcall, printf_fcall_info);
	ext[0] = 0;
	argc--, argv++;
	while (argc && **argv == '-') {
		switch (argv[0][1]) {
		case '4':
			ipv6lookups = 0;
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			if (argc < 2)
				usage();
			dbfile = argv[1];
			argc--, argv++;
			break;
		case 'n':
			justsetname = 1;
			break;
		case 'x':
			if (argc < 2)
				usage();
			setnetmtpt(mntpt, sizeof(mntpt), argv[1]);
			argc--, argv++;
			setext(ext, sizeof(ext), mntpt);
			break;
		}
		argc--, argv++;
	}

	// rfork(RFREND|RFNOTEG);
	/* Make us an SCP with a 2LS */
	parlib_wants_to_be_mcp = FALSE;

	snprintf(servefile, sizeof(servefile), "#srv/cs%s", ext);
	snprintf(netndb, sizeof(netndb), "%s/ndb", mntpt);
	syscall(SYS_nunmount, (unsigned long)servefile, strlen(servefile),
	        (unsigned long)mntpt, strlen(mntpt));
	remove(servefile);

	ndbinit();
	netinit(0);

	if (!justsetname) {
		mountinit(servefile, mntpt);
		io();
	}
	exit(0);
}

/*
 *  if a mount point is specified, set the cs extention to be the mount point
 *  with '_'s replacing '/'s
 */
void setext(char *ext, int n, char *p)
{
	int i, c;

	n--;
	for (i = 0; i < n; i++) {
		c = p[i];
		if (c == 0)
			break;
		if (c == '/')
			c = '_';
		ext[i] = c;
	}
	ext[i] = 0;
}

void mountinit(char *service, char *mntpt)
{
	int f;
	int p[2];
	char buf[32];
	int ret;

	ret = pipe(p);
	if (ret < 0) {
		error(1, 0, "pipe: %r");
		exit(1);
	}

	/*
	 *  make a /srv/cs
	 * ORCLOSE means remove on last close. Handy. Not here yet.
	 */
	f = open(service, O_WRONLY | O_CREAT /*|ORCLOSE*/, 0666);
	if (f < 0)
		error(1, 0, "%s: %r", service);
	snprintf(buf, sizeof(buf), "%d", p[1]);
	if (write(f, buf, strlen(buf)) != strlen(buf))
		error(1, 0, "Write %s: %r", service);
	/* using #s: we create a pipe and drop it into #srv.
	 * we no longer mount. That's up to you.
	 * #srv will route requests to us.
	 */
	close(p[1]);

	mfd[0] = mfd[1] = p[0];
}

void ndbinit(void)
{
	db = ndbopen(dbfile);
	if (db == NULL)
		error(1, 0, "%s: %r", "can't open network database");

	netdb = ndbopen(netndb);
	if (netdb != NULL) {
		netdb->nohash = 1;
		db = ndbcat(netdb, db);
	}
}

Mfile *newfid(int fid)
{
	Mlist *f, *ff;
	Mfile *mf;

	ff = 0;
	for (f = mlist; f; f = f->next)
		if (f->mf.busy && f->mf.fid == fid)
			return &f->mf;
		else if (!ff && !f->mf.busy)
			ff = f;
	if (ff == 0) {
		ff = emalloc(sizeof *f);
		ff->next = mlist;
		mlist = ff;
	}
	mf = &ff->mf;
	memset(mf, 0, sizeof *mf);
	mf->fid = fid;
	return mf;
}

Job *newjob(void)
{
	Job *job;

	job = calloc(1, sizeof(Job));
	if (!job) {
		error(1, 0, "%s: %r", "job calloc");
	}
	spinlock_lock(&joblock);
	job->next = joblist;
	joblist = job;
	job->request.tag = -1;
	spinlock_unlock(&joblock);
	return job;
}

void freejob(Job *job)
{
	Job **l;
	Job *to_free = 0;
	spinlock_lock(&joblock);
	for (l = &joblist; *l; l = &(*l)->next) {
		if ((*l) == job) {
			*l = job->next;
			to_free = job;
			break;
		}
	}
	spinlock_unlock(&joblock);
	if (to_free)
		free(to_free);
}

void flushjob(int tag)
{
	Job *job;

	spinlock_lock(&joblock);
	for (job = joblist; job; job = job->next) {
		if (job->request.tag == tag && job->request.type != Tflush) {
			job->flushed = 1;
			break;
		}
	}
	spinlock_unlock(&joblock);
}

void *job_thread(void *arg)
{
	Mfile *mf;
	Job *job = arg;
	spinlock_lock(&dblock);
	mf = newfid(job->request.fid);

	if (debug)
		fprintf(stderr, "CS:%F", &job->request);
	switch (job->request.type) {
	default:
		fprintf(stderr, "CS:unknown request type %d", job->request.type);
		break;
	case Tversion:
		rversion(job);
		break;
	case Tauth:
		rauth(job);
		break;
	case Tflush:
		rflush(job);
		break;
	case Tattach:
		rattach(job, mf);
		break;
	case Twalk:
		rwalk(job, mf);
		break;
	case Topen:
		ropen(job, mf);
		break;
	case Tcreate:
		rcreate(job, mf);
		break;
	case Tread:
		rread(job, mf);
		break;
	case Twrite:
		rwrite(job, mf);
		break;
	case Tclunk:
		rclunk(job, mf);
		break;
	case Tremove:
		rremove(job, mf);
		break;
	case Tstat:
		rstat(job, mf);
		break;
	case Twstat:
		rwstat(job, mf);
		break;
	}
	spinlock_unlock(&dblock);

	freejob(job);

	if (debug)
		fprintf(stderr, "CS:Job done\n");
	return 0;
}

void io(void)
{
	long n;

	uint8_t mdata[IOHDRSZ + Maxfdata];
	Job *job;

	/*
	 * each request is handled via a thread. Somewhat less efficient than the
	 * old
	 * cs but way cleaner.
	 */

	for (;;) {
		n = read9pmsg(mfd[0], mdata, sizeof mdata);
		if (n <= 0)
			error(1, 0, "%s: %r", "mount read");
		job = newjob();
		if (convM2S(mdata, n, &job->request) != n) {
			fprintf(stderr,
			        "convM2S went south: format error %ux %ux %ux %ux %ux",
			        mdata[0], mdata[1], mdata[2], mdata[3], mdata[4]);
			error(1, 0, "format error %ux %ux %ux %ux %ux", mdata[0], mdata[1],
			      mdata[2], mdata[3], mdata[4]);
			freejob(job);
			continue;
		}
		/* stash the thread in the job so we can join them all
		 * later if we want to.
		 */
		if (pthread_create(&job->thread, NULL, &job_thread, job)) {
			error(1, 0, "%s: %r", "Failed to create job");
			continue;
		}
	}
}

void rversion(Job *job)
{
	if (job->request.msize > IOHDRSZ + Maxfdata)
		job->reply.msize = IOHDRSZ + Maxfdata;
	else
		job->reply.msize = job->request.msize;
	if (strncmp(job->request.version, "9P2000", 6) != 0)
		sendmsg(job, "unknown 9P version");
	else {
		job->reply.version = "9P2000";
		sendmsg(job, 0);
	}
}

void rauth(Job *job)
{
	sendmsg(job, "cs: authentication not required");
}

/*
 *  don't flush till all the threads  are done
 */
void rflush(Job *job)
{
	flushjob(job->request.oldtag);
	sendmsg(job, 0);
}

void rattach(Job *job, Mfile *mf)
{
	if (mf->busy == 0) {
		mf->busy = 1;
		mf->user = estrdup(job->request.uname);
	}
	mf->qid.vers = vers++;
	mf->qid.type = QTDIR;
	mf->qid.path = 0LL;
	job->reply.qid = mf->qid;
	sendmsg(job, 0);
}

char *rwalk(Job *job, Mfile *mf)
{
	char *err;
	char **elems;
	int nelems;
	int i;
	Mfile *nmf;
	struct qid qid;

	err = 0;
	nmf = NULL;
	elems = job->request.wname;
	nelems = job->request.nwname;
	job->reply.nwqid = 0;

	if (job->request.newfid != job->request.fid) {
		/* clone fid */
		nmf = newfid(job->request.newfid);
		if (nmf->busy) {
			nmf = NULL;
			err = "clone to used channel";
			goto send;
		}
		*nmf = *mf;
		nmf->user = estrdup(mf->user);
		nmf->fid = job->request.newfid;
		nmf->qid.vers = vers++;
		mf = nmf;
	}
	/* else nmf will be nil */

	qid = mf->qid;
	if (nelems > 0) {
		/* walk fid */
		for (i = 0; i < nelems && i < MAXWELEM; i++) {
			if ((qid.type & QTDIR) == 0) {
				err = "not a directory";
				break;
			}
			if (strcmp(elems[i], "..") == 0 || strcmp(elems[i], ".") == 0) {
				qid.type = QTDIR;
				qid.path = Qdir;
			Found:
				job->reply.wqid[i] = qid;
				job->reply.nwqid++;
				continue;
			}
			if (strcmp(elems[i], "cs") == 0) {
				qid.type = QTFILE;
				qid.path = Qcs;
				goto Found;
			}
			err = "file does not exist";
			break;
		}
	}

send:
	if (nmf != NULL && (err != NULL || job->reply.nwqid < nelems)) {
		cleanmf(nmf);
		free(nmf->user);
		nmf->user = 0;
		nmf->busy = 0;
		nmf->fid = 0;
	}
	if (err == NULL)
		mf->qid = qid;
	sendmsg(job, err);
	return err;
}

void ropen(Job *job, Mfile *mf)
{
	int mode;
	char *err;

	err = 0;
	mode = job->request.mode;
	if (mf->qid.type & QTDIR) {
		if (mode)
			err = "permission denied";
	}
	job->reply.qid = mf->qid;
	job->reply.iounit = 0;
	sendmsg(job, err);
}

void rcreate(Job *job, Mfile *mf)
{
	sendmsg(job, "creation permission denied");
}

void rread(Job *job, Mfile *mf)
{
	int i, n, cnt;
	long off, toff, clock;
	struct dir dir;
	uint8_t buf[Maxfdata];
	char *err;

	n = 0;
	err = 0;
	off = job->request.offset;
	cnt = job->request.count;
	if (mf->qid.type & QTDIR) {
		//	clock = time(0);
		if (off == 0) {
			memset(&dir, 0, sizeof dir);
			dir.name = "cs";
			dir.qid.type = QTFILE;
			dir.qid.vers = vers;
			dir.qid.path = Qcs;
			dir.mode = 0666;
			dir.length = 0;
			dir.uid = mf->user;
			dir.gid = mf->user;
			dir.muid = mf->user;
			dir.atime = clock; /* wrong */
			dir.mtime = clock; /* wrong */
			n = convD2M(&dir, buf, sizeof buf);
		}
		job->reply.data = (char *)buf;
	} else {
		for (;;) {
			/* look for an answer at the right offset */
			toff = 0;
			for (i = 0; mf->reply[i] && i < mf->nreply; i++) {
				n = mf->replylen[i];
				if (off < toff + n)
					break;
				toff += n;
			}
			if (i < mf->nreply)
				break; /* got something to return */

			/* try looking up more answers */
			if (lookup(mf) == 0) {
				/* no more */
				n = 0;
				goto send;
			}
		}

		/* give back a single reply (or part of one) */
		job->reply.data = mf->reply[i] + (off - toff);
		if (cnt > toff - off + n)
			n = toff - off + n;
		else
			n = cnt;
	}
send:
	job->reply.count = n;
	sendmsg(job, err);
}
void cleanmf(Mfile *mf)
{
	int i;

	if (mf->net != NULL) {
		free(mf->net);
		mf->net = NULL;
	}
	if (mf->host != NULL) {
		free(mf->host);
		mf->host = NULL;
	}
	if (mf->serv != NULL) {
		free(mf->serv);
		mf->serv = NULL;
	}
	if (mf->rem != NULL) {
		free(mf->rem);
		mf->rem = NULL;
	}
	for (i = 0; i < mf->nreply; i++) {
		free(mf->reply[i]);
		mf->reply[i] = NULL;
		mf->replylen[i] = 0;
	}
	mf->nreply = 0;
	mf->nextnet = netlist;
}

void rwrite(Job *job, Mfile *mf)
{
	int cnt, n;
	char *err;
	char *field[4];
	char curerr[64];

	err = 0;
	cnt = job->request.count;
	if (mf->qid.type & QTDIR) {
		err = "can't write directory";
		goto send;
	}
	if (cnt >= Maxrequest) {
		err = "request too long";
		goto send;
	}
	job->request.data[cnt] = 0;
	/*
	 *  toggle debugging
	 */
	if (strncmp(job->request.data, "debug", 5) == 0) {
		debug ^= 1;
		fprintf(stderr, "CS:debug %d", debug);
		goto send;
	}

	/*
	 *  toggle ipv6 lookups
	 */
	if (strncmp(job->request.data, "ipv6", 4) == 0) {
		ipv6lookups ^= 1;
		fprintf(stderr, "CS:ipv6lookups %d", ipv6lookups);
		goto send;
	}

	/*
	 *  toggle debugging
	 */
	if (strncmp(job->request.data, "paranoia", 8) == 0) {
		paranoia ^= 1;
		fprintf(stderr, "CS:paranoia %d", paranoia);
		goto send;
	}

	/*
	 *  add networks to the default list
	 */
	if (strncmp(job->request.data, "add ", 4) == 0) {
		if (job->request.data[cnt - 1] == '\n')
			job->request.data[cnt - 1] = 0;
		netadd(job->request.data + 4);
		readipinterfaces();
		goto send;
	}

	/*
	 *  refresh all state
	 */
	if (strncmp(job->request.data, "refresh", 7) == 0) {
		netinit(0 /*1*/);
		goto send;
	}

	/* start transaction with a clean slate */
	cleanmf(mf);

	/*
	 *  look for a general query
	 */
	if (*job->request.data == '!') {
		err = genquery(mf, job->request.data + 1);
		goto send;
	}

	if (debug)
		fprintf(stderr, "CS:write %s", job->request.data);
	if (paranoia)
		fprintf(stderr, "CS:write %s by %s", job->request.data, mf->user);

	/*
	 *  break up name
	 */
	n = getfields(job->request.data, field, 4, 1, "!");
	switch (n) {
	case 1:
		mf->net = strdup("net");
		mf->host = strdup(field[0]);
		break;
	case 4:
		mf->rem = strdup(field[3]);
	/* fall through */
	case 3:
		mf->serv = strdup(field[2]);
	/* fall through */
	case 2:
		mf->host = strdup(field[1]);
		mf->net = strdup(field[0]);
		break;
	}
	/*
	 *  do the first net worth of lookup
	 */
	if (lookup(mf) == 0) {
		snprintf(curerr, sizeof curerr, "%r");
		err = curerr;
	}
send:
	job->reply.count = cnt;
	sendmsg(job, err);
}

void rclunk(Job *job, Mfile *mf)
{
	cleanmf(mf);
	free(mf->user);
	mf->user = 0;
	mf->busy = 0;
	mf->fid = 0;
	sendmsg(job, 0);
}

void rremove(Job *job, Mfile *mf)
{
	sendmsg(job, "remove permission denied");
}

void rstat(Job *job, Mfile *mf)
{
	struct dir dir;
	uint8_t buf[IOHDRSZ + Maxfdata];

	memset(&dir, 0, sizeof dir);
	if (mf->qid.type & QTDIR) {
		dir.name = ".";
		dir.mode = DMDIR | 0555;
	} else {
		dir.name = "cs";
		dir.mode = 0666;
	}
	dir.qid = mf->qid;
	dir.length = 0;
	dir.uid = mf->user;
	dir.gid = mf->user;
	dir.muid = mf->user;
	// dir.atime = dir.mtime = time(0);
	job->reply.nstat = convD2M(&dir, buf, sizeof buf);
	job->reply.stat = buf;
	sendmsg(job, 0);
}

void rwstat(Job *job, Mfile *mf)
{
	sendmsg(job, "wstat permission denied");
}

void sendmsg(Job *job, char *err)
{
	int n;
	uint8_t mdata[IOHDRSZ + Maxfdata];
	char ename[ERRMAX];

	if (err) {
		job->reply.type = Rerror;
		snprintf(ename, sizeof(ename), "cs: %s", err);
		job->reply.ename = ename;
	} else {
		job->reply.type = job->request.type + 1;
	}
	job->reply.tag = job->request.tag;
	n = convS2M(&job->reply, mdata, sizeof mdata);
	if (n == 1) {
		fprintf(stderr, "CS:sendmsg convS2M of %F returns 0", &job->reply);
		abort();
	}
	spinlock_lock(&joblock);
	if (job->flushed == 0)
		if (write(mfd[1], mdata, n) != n)
			error(1, 0, "%s: %r", "mount write");
	spinlock_unlock(&joblock);
	if (debug)
		fprintf(stderr, "CS:%F %d", &job->reply, n);
}

static int isvalidip(uint8_t *ip)
{
	return ipcmp(ip, IPnoaddr) != 0 && ipcmp(ip, v4prefix) != 0;
}

static uint8_t loopbacknet[IPaddrlen] = {0, 0, 0,    0,    0,   0, 0, 0,
                                         0, 0, 0xff, 0xff, 127, 0, 0, 0};
static uint8_t loopbackmask[IPaddrlen] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                          0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                          0xff, 0,    0,    0};

void readipinterfaces(void)
{
	if (myipaddr(ipa, mntpt) != 0)
		ipmove(ipa, IPnoaddr);
	snprintf(ipaddr, sizeof(ipaddr), "%I", ipa);
	if (debug)
		fprintf(stderr, "CS:dns", "ipaddr is %s\n", ipaddr);
}

/*
 *  get the system name
 */
void ipid(void)
{
	uint8_t addr[6];
	struct ndbtuple *t, *tt;
	char *p, *attr;
	struct ndbs s;
	int f;
	char buf[Maxpath];

	/* use environment, ether addr, or ipaddr to get system name */
	if (mysysname == 0) {
		/*
		 *  environment has priority.
		 *
		 *  on the sgi power the default system name
		 *  is the ip address.  ignore that.
		 *
		 */
		p = getenv("sysname");
		if (p && *p) {
			attr = ipattr(p);
			if (strcmp(attr, "ip") != 0)
				mysysname = strdup(p);
		}

		/*
		 *  the /net/ndb contains what the network
		 *  figured out from DHCP.  use that name if
		 *  there is one.
		 */
		if (mysysname == 0 && netdb != NULL) {
			ndbreopen(netdb);
			for (tt = t = ndbparse(netdb); t != NULL; t = t->entry) {
				if (strcmp(t->attr, "sys") == 0) {
					mysysname = strdup(t->val);
					break;
				}
			}
			ndbfree(tt);
		}

		/* next network database, ip address, and ether address to find a name
		 */
		if (mysysname == 0) {
			t = NULL;
			if (isvalidip(ipa))
				free(ndbgetvalue(db, &s, "ip", ipaddr, "sys", &t));
			if (t == NULL) {
				for (f = 0; f < 3; f++) {
					snprintf(buf, sizeof buf, "%s/ether%d", mntpt, f);
					if (myetheraddr(addr, buf) >= 0) {
						snprintf(eaddr, sizeof(eaddr), "%E", addr);
						free(ndbgetvalue(db, &s, "ether", eaddr, "sys", &t));
						if (t != NULL)
							break;
					}
				}
			}
			for (tt = t; tt != NULL; tt = tt->entry) {
				if (strcmp(tt->attr, "sys") == 0) {
					mysysname = strdup(tt->val);
					break;
				}
			}
			ndbfree(t);
		}

		/* nothing else worked, use the ip address */
		if (mysysname == 0 && isvalidip(ipa))
			mysysname = strdup(ipaddr);

		/* set /dev/sysname if we now know it */
		if (mysysname) {
			f = open("/dev/sysname", O_RDWR);
			if (f >= 0) {
				write(f, mysysname, strlen(mysysname));
				close(f);
			}
		}
	}
}

/*
 *  Set up a list of default networks by looking for
 *  /net/^*^/clone.
 *  For now, never background.
 */
void netinit(int background)
{
	char clone[Maxpath];
	Network *np;
	static int working;

	/* add the mounted networks to the default list */
	for (np = network; np->net; np++) {
		int fuckup;
		if (np->considered)
			continue;
		snprintf(clone, sizeof(clone), "%s/%s/clone", mntpt, np->net);
		fuckup = open(clone, O_RDONLY);
		if (fuckup < 0)
			continue;
		close(fuckup);
		// if(access(clone, R_OK))
		// continue;
		if (netlist)
			last->next = np;
		else
			netlist = np;
		last = np;
		np->next = 0;
		np->considered = 1;
	}

	/* find out what our ip address is */
	readipinterfaces();

	/* set the system name if we need to, these days ip is all we have */
	ipid();

	if (debug)
		fprintf(stderr, logfile, "CS:mysysname %s eaddr %s ipaddr %s ipa %I\n",
		        mysysname ? mysysname : "???", eaddr, ipaddr, ipa);
}

/*
 *  add networks to the standard list
 */
void netadd(char *p)
{
	Network *np;
	char *field[12];
	int i, n;

	n = getfields(p, field, 12, 1, " ");
	for (i = 0; i < n; i++) {
		for (np = network; np->net; np++) {
			if (strcmp(field[i], np->net) != 0)
				continue;
			if (np->considered)
				break;
			if (netlist)
				last->next = np;
			else
				netlist = np;
			last = np;
			np->next = 0;
			np->considered = 1;
		}
	}
}

int lookforproto(struct ndbtuple *t, char *proto)
{
	for (; t != NULL; t = t->entry)
		if (strcmp(t->attr, "proto") == 0 && strcmp(t->val, proto) == 0)
			return 1;
	return 0;
}

/*
 *  lookup a request.  the network "net" means we should pick the
 *  best network to get there.
 */
int lookup(Mfile *mf)
{
	Network *np;
	char *cp;
	struct ndbtuple *nt, *t;
	char reply[Maxreply];
	int i, rv;
	int hack;

	/* open up the standard db files */
	if (db == 0)
		ndbinit();
	if (db == 0)
		error(1, 0, "%s: %r", "can't open mf->network database\n");

	rv = 0;

	if (mf->net == NULL)
		return 0; /* must have been a genquery */

	if (strcmp(mf->net, "net") == 0) {
		/*
		 *  go through set of default nets
		 */
		for (np = mf->nextnet; np; np = np->next) {
			nt = (*np->lookup)(np, mf->host, mf->serv, 1);
			if (nt == NULL)
				continue;
			hack = np->fasttimeouthack && !lookforproto(nt, np->net);
			for (t = nt; mf->nreply < Nreply && t; t = t->entry) {
				cp = (*np->trans)(t, np, mf->serv, mf->rem, hack);
				if (cp) {
					/* avoid duplicates */
					for (i = 0; i < mf->nreply; i++)
						if (strcmp(mf->reply[i], cp) == 0)
							break;
					if (i == mf->nreply) {
						/* save the reply */
						mf->replylen[mf->nreply] = strlen(cp);
						mf->reply[mf->nreply++] = cp;
						rv++;
					}
				}
			}
			ndbfree(nt);
			np = np->next;
			break;
		}
		mf->nextnet = np;
		return rv;
	}

	/*
	 *  if not /net, we only get one lookup
	 */
	if (mf->nreply != 0)
		return 0;
	/*
	 *  look for a specific network
	 */
	for (np = netlist; np && np->net != NULL; np++) {
		if (np->fasttimeouthack)
			continue;
		if (strcmp(np->net, mf->net) == 0)
			break;
	}

	if (np && np->net != NULL) {
		/*
		 *  known network
		 */
		nt = (*np->lookup)(np, mf->host, mf->serv, 1);
		for (t = nt; mf->nreply < Nreply && t; t = t->entry) {
			cp = (*np->trans)(t, np, mf->serv, mf->rem, 0);
			if (cp) {
				mf->replylen[mf->nreply] = strlen(cp);
				mf->reply[mf->nreply++] = cp;
				rv++;
			}
		}
		ndbfree(nt);
		return rv;
	} else {
		/*
		 *  not a known network, don't translate host or service
		 */
		if (mf->serv)
			snprintf(reply, sizeof(reply), "%s/%s/clone %s!%s", mntpt, mf->net,
			         mf->host, mf->serv);
		else
			snprintf(reply, sizeof(reply), "%s/%s/clone %s", mntpt, mf->net,
			         mf->host);
		mf->reply[0] = strdup(reply);
		mf->replylen[0] = strlen(reply);
		mf->nreply = 1;
		return 1;
	}
}

/*
 *  translate an ip service name into a port number.  If it's a numeric port
 *  number, look for restricted access.
 *
 *  the service '*' needs no translation.
 */
char *ipserv(Network *np, char *name, char *buf, int blen)
{
	char *p;
	int alpha = 0;
	int restr = 0;
	char port[10];
	struct ndbtuple *t, *nt;
	struct ndbs s;

	/* '*' means any service */
	if (strcmp(name, "*") == 0) {
		strcpy(buf, name);
		return buf;
	}

	/*  see if it's numeric or symbolic */
	port[0] = 0;
	for (p = name; *p; p++) {
		if (isdigit(*p)) {
		} else if (isalpha(*p) || *p == '-' || *p == '$')
			alpha = 1;
		else
			return 0;
	}
	t = NULL;
	p = NULL;
	if (alpha) {
		p = ndbgetvalue(db, &s, np->net, name, "port", &t);
		if (p == NULL)
			return 0;
	} else {
		/* look up only for tcp ports < 1024 to get the restricted
		 * attribute
		 */
		if (atoi(name) < 1024 && strcmp(np->net, "tcp") == 0)
			p = ndbgetvalue(db, &s, "port", name, "port", &t);
		if (p == NULL)
			p = strdup(name);
	}

	if (t) {
		for (nt = t; nt; nt = nt->entry)
			if (strcmp(nt->attr, "restricted") == 0)
				restr = 1;
		ndbfree(t);
	}
	snprintf(buf, blen, "%s%s", p, restr ? "!r" : "");
	free(p);
	return buf;
}

/*
 *  lookup an ip attribute
 */
int ipattrlookup(struct ndb *db, char *ipa, char *attr, char *val, int vlen)
{

	struct ndbtuple *t, *nt;
	char *alist[2];

	alist[0] = attr;
	t = ndbipinfo(db, "ip", ipa, alist, 1);
	if (t == NULL)
		return 0;
	for (nt = t; nt != NULL; nt = nt->entry) {
		if (strcmp(nt->attr, attr) == 0) {
			nstrcpy(val, nt->val, vlen);
			ndbfree(t);
			return 1;
		}
	}

	/* we shouldn't get here */
	ndbfree(t);
	return 0;
}

/*
 *  lookup (and translate) an ip destination
 */
struct ndbtuple *iplookup(Network *np, char *host, char *serv, int nolookup)
{
	char *attr, *dnsname;
	struct ndbtuple *t, *nt;
	struct ndbs s;
	char ts[Maxservice];
	char dollar[Maxhost];
	uint8_t ip[IPaddrlen];
	uint8_t net[IPaddrlen];
	uint8_t tnet[IPaddrlen];
	struct ipifc *ifc;
	struct iplifc *lifc;

	/*
	 *  start with the service since it's the most likely to fail
	 *  and costs the least
	 */
	werrstr("can't translate address");
	if (serv == 0 || ipserv(np, serv, ts, sizeof ts) == 0) {
		werrstr("can't translate service");
		return 0;
	}

	/* for dial strings with no host */
	if (strcmp(host, "*") == 0)
		return ndbnew("ip", "*");

	/*
	 *  hack till we go v6 :: = 0.0.0.0
	 */
	if (strcmp("::", host) == 0)
		return ndbnew("ip", "*");

	/*
	 *  '$' means the rest of the name is an attribute that we
	 *  need to search for
	 */
	if (*host == '$') {
		if (ipattrlookup(db, ipaddr, host + 1, dollar, sizeof dollar))
			host = dollar;
	}

	/*
	 *  turn '[ip address]' into just 'ip address'
	 */
	if (*host == '[' && host[strlen(host) - 1] == ']') {
		host++;
		host[strlen(host) - 1] = 0;
	}

	/*
	 *  just accept addresses
	 */
	attr = ipattr(host);
	if (strcmp(attr, "ip") == 0)
		return ndbnew("ip", host);

	/*
	 *  give the domain name server the first opportunity to
	 *  resolve domain names.  if that fails try the database.
	 */
	t = 0;
	werrstr("can't translate address");
	if (strcmp(attr, "dom") == 0)
		t = dnsiplookup(host, &s);
	if (t == 0)
		free(ndbgetvalue(db, &s, attr, host, "ip", &t));
	if (t == 0) {
		dnsname = ndbgetvalue(db, &s, attr, host, "dom", NULL);
		if (dnsname) {
			t = dnsiplookup(dnsname, &s);
			free(dnsname);
		}
	}
	if (t == 0)
		t = dnsiplookup(host, &s);
	if (t == 0)
		return 0;

	/*
	 *  reorder the tuple to have the matched line first and
	 *  save that in the request structure.
	 */
	t = reorder(t, s.t);

	/*
	 * reorder according to our interfaces
	 */
	spinlock_lock(&ipifclock);
	for (ifc = ipifcs; ifc != NULL; ifc = ifc->next) {
		for (lifc = ifc->lifc; lifc != NULL; lifc = lifc->next) {
			maskip(lifc->ip, lifc->mask, net);
			for (nt = t; nt; nt = nt->entry) {
				if (strcmp(nt->attr, "ip") != 0)
					continue;
				parseip(ip, nt->val);
				maskip(ip, lifc->mask, tnet);
				if (memcmp(net, tnet, IPaddrlen) == 0) {
					t = reorder(t, nt);
					spinlock_unlock(&ipifclock);
					return t;
				}
			}
		}
	}
	spinlock_unlock(&ipifclock);

	return t;
}

/*
 *  translate an ip address
 */
char *iptrans(struct ndbtuple *t, Network *np, char *serv, char *rem, int hack)
{
	char ts[Maxservice];
	char reply[Maxreply];
	char x[Maxservice];

	if (strcmp(t->attr, "ip") != 0)
		return 0;

	if (serv == 0 || ipserv(np, serv, ts, sizeof ts) == 0) {
		werrstr("can't translate service");
		return 0;
	}
	if (rem != NULL)
		snprintf(x, sizeof(x), "!%s", rem);
	else
		*x = 0;

	if (*t->val == '*')
		snprintf(reply, sizeof(reply), "%s/%s/clone %s%s", mntpt, np->net, ts,
		         x);
	else
		snprintf(reply, sizeof(reply), "%s/%s/clone %s!%s%s%s", mntpt, np->net,
		         t->val, ts, x, hack ? "!fasttimeout" : "");

	return strdup(reply);
}

/*
 *  lookup a telephone number
 */
struct ndbtuple *telcolookup(Network *np, char *host, char *serv, int nolookup)
{
	struct ndbtuple *t;
	struct ndbs s;

	werrstr("can't translate address");
	free(ndbgetvalue(db, &s, "sys", host, "telco", &t));
	if (t == 0)
		return ndbnew("telco", host);

	return reorder(t, s.t);
}

/*
 *  translate a telephone address
 */
char *telcotrans(struct ndbtuple *t, Network *np, char *serv, char *rem,
                 int unused)
{
	char reply[Maxreply];
	char x[Maxservice];

	if (strcmp(t->attr, "telco") != 0)
		return 0;

	if (rem != NULL)
		snprintf(x, sizeof(x), "!%s", rem);
	else
		*x = 0;
	if (serv)
		snprintf(reply, sizeof(reply), "%s/%s/clone %s!%s%s", mntpt, np->net,
		         t->val, serv, x);
	else
		snprintf(reply, sizeof(reply), "%s/%s/clone %s%s", mntpt, np->net,
		         t->val, x);
	return strdup(reply);
}

/*
 *  reorder the tuple to put x's line first in the entry
 */
struct ndbtuple *reorder(struct ndbtuple *t, struct ndbtuple *x)
{
	struct ndbtuple *nt;
	struct ndbtuple *line;

	/* find start of this entry's line */
	for (line = x; line->entry == line->line; line = line->line)
		;
	line = line->line;
	if (line == t)
		return t; /* already the first line */

	/* remove this line and everything after it from the entry */
	for (nt = t; nt->entry != line; nt = nt->entry)
		;
	nt->entry = 0;

	/* make that the start of the entry */
	for (nt = line; nt->entry; nt = nt->entry)
		;
	nt->entry = t;
	return line;
}

static struct ndbtuple *dnsip6lookup(char *mntpt, char *buf, struct ndbtuple *t)
{
	struct ndbtuple *t6, *tt;

	t6 = dnsquery(mntpt, buf, "ipv6"); /* lookup AAAA dns RRs */
	if (t6 == NULL)
		return t;

	/* convert ipv6 attr to ip */
	for (tt = t6; tt != NULL; tt = tt->entry)
		if (strcmp(tt->attr, "ipv6") == 0)
			strncpy(tt->attr, "ip", sizeof tt->attr - 1);

	if (t == NULL)
		return t6;

	/* append t6 list to t list */
	for (tt = t; tt->entry != NULL; tt = tt->entry)
		;
	tt->entry = t6;
	return t;
}

/*
 *  call the dns process and have it try to translate a name
 */
struct ndbtuple *dnsiplookup(char *host, struct ndbs *s)
{
	char buf[Maxreply];
	struct ndbtuple *t;

	spinlock_unlock(&dblock);

	/* save the name */
	snprintf(buf, sizeof(buf), "%s", host);

	if (strcmp(ipattr(buf), "ip") == 0)
		t = dnsquery(mntpt, buf, "ptr");
	else {
		t = dnsquery(mntpt, buf, "ip");
		/* special case: query ipv6 (AAAA dns RR) too */
		if (ipv6lookups)
			t = dnsip6lookup(mntpt, buf, t);
	}
	s->t = t;

	if (t == NULL) {
		snprintf(buf, sizeof buf, "%r");
		if (strstr(buf, "exist"))
			werrstr("can't translate address: %s", buf);
		else if (strstr(buf, "dns failure"))
			werrstr("temporary problem: %s", buf);
	}

	spinlock_lock(&dblock);
	return t;
}

int qmatch(struct ndbtuple *t, char **attr, char **val, int n)
{
	int i, found;
	struct ndbtuple *nt;

	for (i = 1; i < n; i++) {
		found = 0;
		for (nt = t; nt; nt = nt->entry)
			if (strcmp(attr[i], nt->attr) == 0)
				if (strcmp(val[i], "*") == 0 || strcmp(val[i], nt->val) == 0) {
					found = 1;
					break;
				}
		if (found == 0)
			break;
	}
	return i == n;
}

/* this is awful but I don't want to bring in libstring just for this.
 * you want real strings don't use C
 */
void qreply(Mfile *mf, struct ndbtuple *t)
{
	struct ndbtuple *nt;
	char *s, *cur;
	int len, amt;

	s = malloc(4096);
	cur = s;
	len = 4096;

	for (nt = t; mf->nreply < Nreply && nt; nt = nt->entry) {
		amt = snprintf(cur, len, "%s=%s", nt->attr, nt->val);

		if (amt < 0)
			len = 0;
		else {
			len -= amt;
			cur += amt;
		}

		if (nt->line != nt->entry) {
			mf->replylen[mf->nreply] = strlen(s);
			mf->reply[mf->nreply++] = strdup(s);
			cur = s;
			len = 4096;
		} else {
			amt = snprintf(cur, len, " ");
			if (amt < 0)
				len = 0;
			else {
				len -= amt;
				cur += amt;
			}
		}
	}
	free(s);
}

enum {
	Maxattr = 32,
};

/*
 *  generic query lookup.  The query is of one of the following
 *  forms:
 *
 *  attr1=val1 attr2=val2 attr3=val3 ...
 *
 *  returns the matching tuple
 *
 *  ipinfo attr=val attr1 attr2 attr3 ...
 *
 *  is like ipinfo and returns the attr{1-n}
 *  associated with the ip address.
 */
char *genquery(Mfile *mf, char *query)
{
	int i, n;
	char *p;
	char *attr[Maxattr];
	char *val[Maxattr];
	struct ndbtuple *t;
	struct ndbs s;

	n = getfields(query, attr, COUNT_OF(attr), 1, " ");
	if (n == 0)
		return "bad query";

	if (strcmp(attr[0], "ipinfo") == 0)
		return ipinfoquery(mf, attr, n);

	/* parse pairs */
	for (i = 0; i < n; i++) {
		p = strchr(attr[i], '=');
		if (p == 0)
			return "bad query";
		*p++ = 0;
		val[i] = p;
	}

	/* give dns a chance */
	if ((strcmp(attr[0], "dom") == 0 || strcmp(attr[0], "ip") == 0) && val[0]) {
		t = dnsiplookup(val[0], &s);
		if (t) {
			if (qmatch(t, attr, val, n)) {
				qreply(mf, t);
				ndbfree(t);
				return 0;
			}
			ndbfree(t);
		}
	}

	/* first pair is always the key.  It can't be a '*' */
	t = ndbsearch(db, &s, attr[0], val[0]);

	/* search is the and of all the pairs */
	while (t) {
		if (qmatch(t, attr, val, n)) {
			qreply(mf, t);
			ndbfree(t);
			return 0;
		}

		ndbfree(t);
		t = ndbsnext(&s, attr[0], val[0]);
	}

	return "no match";
}

/*
 *  resolve an ip address
 */
static struct ndbtuple *ipresolve(char *attr, char *host)
{
	struct ndbtuple *t, *nt, **l;

	t = iplookup(&network[Ntcp], host, "*", 0);
	for (l = &t; *l != NULL;) {
		nt = *l;
		if (strcmp(nt->attr, "ip") != 0) {
			*l = nt->entry;
			nt->entry = NULL;
			ndbfree(nt);
			continue;
		}
		strcpy(nt->attr, attr);
		l = &nt->entry;
	}
	return t;
}

char *ipinfoquery(Mfile *mf, char **list, int n)
{
	int i, nresolve;
	int resolve[Maxattr];
	struct ndbtuple *t, *nt, **l;
	char *attr, *val;

	/* skip 'ipinfo' */
	list++;
	n--;

	if (n < 1)
		return "bad query";

	/* get search attribute=value, or assume ip=myipaddr */
	attr = *list;
	if ((val = strchr(attr, '=')) != NULL) {
		*val++ = 0;
		list++;
		n--;
	} else {
		attr = "ip";
		val = ipaddr;
	}

	if (n < 1)
		return "bad query";

	/*
	 *  don't let ndbipinfo resolve the addresses, we're
	 *  better at it.
	 */
	nresolve = 0;
	for (i = 0; i < n; i++)
		if (*list[i] == '@') { /* @attr=val ? */
			list[i]++;
			resolve[i] = 1; /* we'll resolve it */
			nresolve++;
		} else
			resolve[i] = 0;

	t = ndbipinfo(db, attr, val, list, n);
	if (t == NULL)
		return "no match";

	if (nresolve != 0) {
		for (l = &t; *l != NULL;) {
			nt = *l;

			/* already an address? */
			if (strcmp(ipattr(nt->val), "ip") == 0) {
				l = &(*l)->entry;
				continue;
			}

			/* user wants it resolved? */
			for (i = 0; i < n; i++)
				if (strcmp(list[i], nt->attr) == 0)
					break;
			if (i >= n || resolve[i] == 0) {
				l = &(*l)->entry;
				continue;
			}

			/* resolve address and replace entry */
			*l = ipresolve(nt->attr, nt->val);
			while (*l != NULL)
				l = &(*l)->entry;
			*l = nt->entry;

			nt->entry = NULL;
			ndbfree(nt);
		}
	}

	/* make it all one line */
	for (nt = t; nt != NULL; nt = nt->entry) {
		if (nt->entry == NULL)
			nt->line = t;
		else
			nt->line = nt->entry;
	}

	qreply(mf, t);

	return NULL;
}

void *emalloc(int size)
{
	void *x;

	x = calloc(size, 1);
	if (x == NULL)
		abort();
	memset(x, 0, size);
	return x;
}

char *estrdup(char *s)
{
	int size;
	char *p;

	size = strlen(s) + 1;
	p = calloc(size, 1);
	if (p == NULL)
		abort();
	memmove(p, s, size);
	return p;
}
