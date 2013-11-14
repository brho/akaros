#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <error.h>
#include <nixip.h>
#include <dir.h>
#include <ndb.h>
#include <fcall.h>
#include <printf-ext.h>

#warning "you really need to fix the include directory tree structure!"
#define DMDIR              0x80000000	/* mode bit for directories */
#define QTDIR              0x80	/* type bit for directories */

/*
 * Rather than reading /adm/users, which is a lot of work for
 * a toy program, we assume all groups have the form
 *	NNN:user:user:
 * meaning that each user is the leader of his own group.
 */

enum {
	OPERM = 0x3,				/* mask of all permission types in open mode */
	Nram = 4096,
	Maxsize = 768 * 1024 * 1024,
	Maxfdata = 8192,
	Maxulong = (1ULL << 32) - 1,
};

typedef struct Fid Fid;
typedef struct Ram Ram;

struct Fid {
	short busy;
	short open;
	short rclose;
	int fid;
	Fid *next;
	char *user;
	Ram *ram;
};

struct Ram {
	short busy;
	short open;
	long parent;				/* index in Ram array */
	struct qid qid;
	long perm;
	char *name;
	uint32_t atime;
	uint32_t mtime;
	char *user;
	char *group;
	char *muid;
	char *data;
	long ndata;
};

enum {
	Pexec = 1,
	Pwrite = 2,
	Pread = 4,
	Pother = 1,
	Pgroup = 8,
	Powner = 64,
};

uint32_t path;					/* incremented for each new file */
Fid *fids;
Ram ram[Nram];
int nram;
int mfd[2];
char *user;
uint8_t mdata[IOHDRSZ + Maxfdata];
uint8_t rdata[Maxfdata];		/* buffer for data in reply */
#define STATMAX 65536
uint8_t statbuf[STATMAX];
struct fcall thdr;
struct fcall rhdr;
int messagesize = sizeof mdata;

Fid *newfid(int);
unsigned int ramstat(Ram *, uint8_t *, unsigned int);

void io(void);
void *erealloc(void *, uint32_t);
void *emalloc(uint32_t);
char *estrdup(char *);
void usage(void);
int perm(Fid *, Ram *, int);

char *rflush(Fid *), *rversion(Fid *), *rauth(Fid *),
	*rattach(Fid *), *rwalk(Fid *),
	*ropen(Fid *), *rcreate(Fid *),
	*rread(Fid *), *rwrite(Fid *), *rclunk(Fid *),
	*rremove(Fid *), *rstat(Fid *), *rwstat(Fid *);

int needfid[] = {
	[Tversion] 0,
	[Tflush] 0,
	[Tauth] 0,
	[Tattach] 0,
	[Twalk] 1,
	[Topen] 1,
	[Tcreate] 1,
	[Tread] 1,
	[Twrite] 1,
	[Tclunk] 1,
	[Tremove] 1,
	[Tstat] 1,
	[Twstat] 1,
};

char *(*fcalls[]) (Fid *) = {
[Tversion] rversion,
		[Tflush] rflush,
		[Tauth] rauth,
		[Tattach] rattach,
		[Twalk] rwalk,
		[Topen] ropen,
		[Tcreate] rcreate,
		[Tread] rread,
		[Twrite] rwrite,
		[Tclunk] rclunk,[Tremove] rremove,[Tstat] rstat,[Twstat] rwstat,};

char Eperm[] = "permission denied";
char Enotdir[] = "not a directory";
char Enoauth[] = "ramfs: authentication not required";
char Enotexist[] = "file does not exist";
char Einuse[] = "file in use";
char Eexist[] = "file exists";
char Eisdir[] = "file is a directory";
char Enotowner[] = "not owner";
char Eisopen[] = "file already open for I/O";
char Excl[] = "exclusive use file already open";
char Ename[] = "illegal name";
char Eversion[] = "unknown 9P version";
char Enotempty[] = "directory not empty";

int debug = 1;
int private;

static int memlim = 1;

#warning "need signal handling in ramfs"
#if 0
void notifyf(void *a, char *s)
{
	if (strncmp(s, "interrupt", 9) == 0) {

		ignore(NCONT);
		fprintf(stderr, "noted\n");
		exit(1);
	}
	ignore(NDFLT);
	fprintf(stderr, "noted\n");
	exit(1);
}
#endif
char *argv0;
#define	ARGBEGIN	for((argv0||(argv0=*argv)),argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt;\
				char _argc;		\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				_argc = _args[0];\
				while(*_args && _args++)\
					switch(_argc)
#define	ARGEND		/*SET(_argt);USED(_argt,_argc,_args);}USED(argv, argc);*/}
#define	ARGF()		(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): 0))
#define	EARGF(x)	(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): ((x), abort(), (char*)0)))

#define	ARGC()		_argc

void main(int argc, char *argv[])
{
	Ram *r;
	char *defmnt, *service;
	int p[2];
	int fd;
	int stdio = 0;
	int ret;

	service = "ramfs";
	defmnt = NULL;	//"/tmp";
	/* blame lindent. */
	ARGBEGIN {
case 'i':
		defmnt = 0;
		stdio = 1;
		mfd[0] = 0;
		mfd[1] = 1;
		break;
case 'm':
		defmnt = EARGF(usage());
		break;
case 'p':
		private++;
		break;
case 's':
		defmnt = 0;
		break;
case 'u':
		memlim = 0;	/* unlimited memory consumption */
		break;
case 'D':
		debug = 1;
		break;
case 'S':
		defmnt = 0;
		service = EARGF(usage());
		break;
default:
		usage();
	}
	ARGEND ret = syscall(SYS_npipe, (unsigned long)p);
	if (ret < 0) {
		error(1, 0, "pipe: %r");
		exit(1);
	}

	if (!stdio) {
		mfd[0] = p[0];
		mfd[1] = p[0];
		if (defmnt == 0) {
			char buf[64];
			snprintf(buf, sizeof buf, "#s/%s", service);
#warning "would be nice to have remove on close"
			fd = open(buf, O_WRONLY | O_CREAT /*ORCLOSE*/, 0666);
			if (fd < 0)
				error(1, 0, "create failed: %r");
			snprintf(buf, sizeof(buf), "%d", p[1]);
			if (write(fd, buf, strlen(buf)) < 0)
				error(1, 0, "writing service file: %r");
		}
	}

	user = "eve";	//getuser();
	//notify(notifyf);
	nram = 1;
	r = &ram[0];
	r->busy = 1;
	r->data = 0;
	r->ndata = 0;
	r->perm = DMDIR | 0775;
	r->qid.type = QTDIR;
	r->qid.path = 0LL;
	r->qid.vers = 0;
	r->parent = 0;
	r->user = user;
	r->group = user;
	r->muid = user;
	r->atime = 0;	//time(0);
	r->mtime = r->atime;
	r->name = estrdup(".");

	if (debug) {
int printf_fcall(FILE *stream, const struct printf_info *info,
		 const void *const *args);
int printf_fcall_info(const struct printf_info* info, size_t n, int *argtypes,
		      int *size);
int printf_dir(FILE *stream, const struct printf_info *info,
		 const void *const *args);
int printf_dir_info(const struct printf_info* info, size_t n, int *argtypes,
		      int *size);


		register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);
		register_printf_specifier('F', printf_fcall, printf_fcall_info);
		register_printf_specifier('M', printf_dir, printf_dir_info);
	}
#if 0
	switch (rfork(RFFDG | RFPROC | RFNAMEG | RFNOTEG)) {
		case -1:
			error("fork");
		case 0:
#endif
			close(p[1]);
			io();
#if 0
			break;
		default:
			close(p[0]);	/* don't deadlock if child fails */
			if (defmnt && mount(p[1], -1, defmnt, MREPL | MCREATE, "") < 0)
				error("mount failed");
	}
	exits(0);
#endif
}

char *rversion(Fid * unused)
{
	Fid *f;

	for (f = fids; f; f = f->next)
		if (f->busy)
			rclunk(f);
	if (thdr.msize > sizeof mdata)
		rhdr.msize = sizeof mdata;
	else
		rhdr.msize = thdr.msize;
	messagesize = rhdr.msize;
	if (strncmp(thdr.version, "9P2000", 6) != 0)
		return Eversion;
	rhdr.version = "9P2000";
	return 0;
}

char *rauth(Fid * unused)
{
	return "ramfs: no authentication required";
}

char *rflush(Fid * f)
{
	return 0;
}

char *rattach(Fid * f)
{
	/* no authentication! */
	f->busy = 1;
	f->rclose = 0;
	f->ram = &ram[0];
	rhdr.qid = f->ram->qid;
	if (thdr.uname[0])
		f->user = estrdup(thdr.uname);
	else
		f->user = "none";
	if (strcmp(user, "none") == 0)
		user = f->user;
	return 0;
}

char *clone(Fid * f, Fid ** nf)
{
	if (f->open)
		return Eisopen;
	if (f->ram->busy == 0)
		return Enotexist;
	*nf = newfid(thdr.newfid);
	(*nf)->busy = 1;
	(*nf)->open = 0;
	(*nf)->rclose = 0;
	(*nf)->ram = f->ram;
	(*nf)->user = f->user;	/* no ref count; the leakage is minor */
	return 0;
}

char *rwalk(Fid * f)
{
	Ram *r, *fram;
	char *name;
	Ram *parent;
	Fid *nf;
	char *err;
	uint32_t t;
	int i;

	err = NULL;
	nf = NULL;
	rhdr.nwqid = 0;
	if (thdr.newfid != thdr.fid) {
		err = clone(f, &nf);
		if (err)
			return err;
		f = nf;	/* walk the new fid */
	}
	fram = f->ram;
	if (thdr.nwname > 0) {
		t = 0;	//time(0);
		for (i = 0; i < thdr.nwname && i < MAXWELEM; i++) {
			if ((fram->qid.type & QTDIR) == 0) {
				err = Enotdir;
				break;
			}
			if (fram->busy == 0) {
				err = Enotexist;
				break;
			}
			fram->atime = t;
			name = thdr.wname[i];
			if (strcmp(name, ".") == 0) {
Found:
				rhdr.nwqid++;
				rhdr.wqid[i] = fram->qid;
				continue;
			}
			parent = &ram[fram->parent];
			if (!perm(f, parent, Pexec)) {
				err = Eperm;
				break;
			}
			if (strcmp(name, "..") == 0) {
				fram = parent;
				goto Found;
			}
			for (r = ram; r < &ram[nram]; r++)
				if (r->busy && r->parent == fram - ram
					&& strcmp(name, r->name) == 0) {
					fram = r;
					goto Found;
				}
			break;
		}
		if (i == 0 && err == NULL)
			err = Enotexist;
	}
	if (nf != NULL && (err != NULL || rhdr.nwqid < thdr.nwname)) {
		/* clunk the new fid, which is the one we walked */
		f->busy = 0;
		f->ram = NULL;
	}
	if (rhdr.nwqid > 0)
		err = NULL;	/* didn't get everything in 9P2000 right! */
	if (rhdr.nwqid == thdr.nwname)	/* update the fid after a successful walk */
		f->ram = fram;
	return err;
}

char *ropen(Fid * f)
{
	Ram *r;
	int mode, trunc;

	if (f->open)
		return Eisopen;
	r = f->ram;
	if (r->busy == 0)
		return Enotexist;
#warning "DMEXCL would be nice"
#if 0
	if (r->perm & DMEXCL)
		if (r->open)
			return Excl;
#endif
	mode = thdr.mode;
	if (r->qid.type & QTDIR) {
		if (mode != O_RDONLY)
			return Eperm;
		rhdr.qid = r->qid;
		return 0;
	}
#if 0
	if (mode & ORCLOSE) {
		/* can't remove root; must be able to write parent */
		if (r->qid.path == 0 || !perm(f, &ram[r->parent], Pwrite))
			return Eperm;
		f->rclose = 1;
	}
#endif
	trunc = mode & O_TRUNC;
	mode &= 0777;	//OPERM;
	if (mode == O_WRONLY || mode == O_RDWR || trunc)
		if (!perm(f, r, Pwrite))
			return Eperm;
	if (mode == O_RDONLY || mode == O_RDWR)
		if (!perm(f, r, Pread))
			return Eperm;
#warning "O_EXEC?"
#if 0
	if (mode == O_EXEC)
		if (!perm(f, r, Pexec))
			return Eperm;
#endif
	if (trunc /*&& (r->perm&DMAPPEND)==0 */ ) {
		r->ndata = 0;
		if (r->data)
			free(r->data);
		r->data = 0;
		r->qid.vers++;
	}
	rhdr.qid = r->qid;
	rhdr.iounit = messagesize - IOHDRSZ;
	f->open = 1;
	r->open++;
	return 0;
}

char *rcreate(Fid * f)
{
	Ram *r;
	char *name;
	long parent, prm;
	char *ret;

	if (debug) printf("%s\n", __func__);
	if (f->open) {
		ret = Eisopen;
		goto bad;
	}
	if (f->ram->busy == 0) {
		ret = Enotexist;
		goto bad;
	}
	parent = f->ram - ram;
	if ((f->ram->qid.type & QTDIR) == 0) {
		ret = Enotdir;
		goto bad;
	}
	/* must be able to write parent */
	if (!perm(f, f->ram, Pwrite)) {
		ret = Eperm;
		goto bad;
	}
	prm = thdr.perm;
	name = thdr.name;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		ret = Ename;
		goto bad;
	}
	for (r = ram; r < &ram[nram]; r++)
		if (r->busy && parent == r->parent)
			if (strcmp((char *)name, r->name) == 0) {
				ret = Einuse;
				goto bad;
			}
	for (r = ram; r->busy; r++)
		if (r == &ram[Nram - 1]) {
			ret = "no free ram resources";
			goto bad;
		}
	printf("%s: ready to do it\n", __func__);
	r->busy = 1;
	r->qid.path = ++path;
	r->qid.vers = 0;
	if (prm & DMDIR)
		r->qid.type |= QTDIR;
	r->parent = parent;
	free(r->name);
	r->name = estrdup(name);
	r->user = f->user;
	r->group = f->ram->group;
	r->muid = f->ram->muid;
	if (prm & DMDIR)
		prm = (prm & ~0777) | (f->ram->perm & prm & 0777);
	else
		prm = (prm & (~0777 | 0111)) | (f->ram->perm & prm & 0666);
	r->perm = prm;
	r->ndata = 0;
	if (r - ram >= nram)
		nram = r - ram + 1;
	r->atime = 0;	//time(0);
	r->mtime = r->atime;
	f->ram->mtime = r->atime;
	f->ram = r;
	rhdr.qid = r->qid;
	rhdr.iounit = messagesize - IOHDRSZ;
	f->open = 1;
#if 0
	if (thdr.mode & ORCLOSE)
		f->rclose = 1;
#endif
	r->open++;
bad:
	if (debug && ret)
		printf("%s: returning :%s:\n", __func__, ret);
	return ret;
}

char *rread(Fid * f)
{
	Ram *r;
	uint8_t *buf;
	int64_t off;
	int n, m, cnt;

	if (f->ram->busy == 0)
		return Enotexist;
	n = 0;
	rhdr.count = 0;
	rhdr.data = (char *)rdata;
	if (thdr.offset < 0)
		return "negative seek offset";
	off = thdr.offset;
	buf = rdata;
	cnt = thdr.count;
	if (cnt > messagesize)	/* shouldn't happen, anyway */
		cnt = messagesize;
	if (cnt < 0)
		return "negative read count";
	if (f->ram->qid.type & QTDIR) {
		for (r = ram + 1; off > 0; r++) {
			if (r->busy && r->parent == f->ram - ram)
				off -= ramstat(r, statbuf, sizeof statbuf);
			if (r == &ram[nram - 1])
				return 0;
		}
		for (; r < &ram[nram] && n < cnt; r++) {
			if (!r->busy || r->parent != f->ram - ram)
				continue;
			m = ramstat(r, buf + n, cnt - n);
			if (m == 0)
				break;
			n += m;
		}
		rhdr.data = (char *)rdata;
		rhdr.count = n;
		return 0;
	}
	r = f->ram;
	if (off >= r->ndata)
		return 0;
	r->atime = 0;	//time(0);
	n = cnt;
	if (off + n > r->ndata)
		n = r->ndata - off;
	rhdr.data = r->data + off;
	rhdr.count = n;
	return 0;
}

char *rwrite(Fid * f)
{
	Ram *r;
	int64_t off;
	int cnt;

	r = f->ram;
	rhdr.count = 0;
	if (r->busy == 0)
		return Enotexist;
	if (thdr.offset < 0)
		return "negative seek offset";
	off = thdr.offset;
#if 0
	if (r->perm & DMAPPEND)
		off = r->ndata;
#endif
	cnt = thdr.count;
	if (cnt < 0)
		return "negative write count";
	if (r->qid.type & QTDIR)
		return Eisdir;
	if (memlim && off + cnt >= Maxsize)	/* sanity check */
		return "write too big";
	if (off + cnt > r->ndata)
		r->data = erealloc(r->data, off + cnt);
	if (off > r->ndata)
		memset(r->data + r->ndata, 0, off - r->ndata);
	if (off + cnt > r->ndata)
		r->ndata = off + cnt;
	memmove(r->data + off, thdr.data, cnt);
	r->qid.vers++;
	r->mtime = time(0);
	rhdr.count = cnt;
	return 0;
}

static int emptydir(Ram * dr)
{
	long didx = dr - ram;
	Ram *r;

	for (r = ram; r < &ram[nram]; r++)
		if (r->busy && didx == r->parent)
			return 0;
	return 1;
}

char *realremove(Ram * r)
{
	if (r->qid.type & QTDIR && !emptydir(r))
		return Enotempty;
	r->ndata = 0;
	if (r->data)
		free(r->data);
	r->data = 0;
	r->parent = 0;
	memset(&r->qid, 0, sizeof r->qid);
	free(r->name);
	r->name = NULL;
	r->busy = 0;
	return NULL;
}

char *rclunk(Fid * f)
{
	char *e = NULL;

	if (f->open)
		f->ram->open--;
	if (f->rclose)
		e = realremove(f->ram);
	f->busy = 0;
	f->open = 0;
	f->ram = 0;
	return e;
}

char *rremove(Fid * f)
{
	Ram *r;

	if (f->open)
		f->ram->open--;
	f->busy = 0;
	f->open = 0;
	r = f->ram;
	f->ram = 0;
	if (r->qid.path == 0 || !perm(f, &ram[r->parent], Pwrite))
		return Eperm;
	ram[r->parent].mtime = time(0);
	return realremove(r);
}

char *rstat(Fid * f)
{
	if (f->ram->busy == 0)
		return Enotexist;
	rhdr.nstat = ramstat(f->ram, statbuf, sizeof statbuf);
	rhdr.stat = statbuf;
	return 0;
}

char *rwstat(Fid * f)
{
	Ram *r, *s;
	struct dir dir;

	if (f->ram->busy == 0)
		return Enotexist;
	convM2D(thdr.stat, thdr.nstat, &dir, (char *)statbuf);
	r = f->ram;

	/*
	 * To change length, must have write permission on file.
	 */
	if (dir.length != ~0 && dir.length != r->ndata) {
		if (!perm(f, r, Pwrite))
			return Eperm;
	}

	/*
	 * To change name, must have write permission in parent
	 * and name must be unique.
	 */
	if (dir.name[0] != '\0' && strcmp(dir.name, r->name) != 0) {
		if (!perm(f, &ram[r->parent], Pwrite))
			return Eperm;
		for (s = ram; s < &ram[nram]; s++)
			if (s->busy && s->parent == r->parent)
				if (strcmp(dir.name, s->name) == 0)
					return Eexist;
	}

	/*
	 * To change mode, must be owner or group leader.
	 * Because of lack of users file, leader=>group itself.
	 */
	if (dir.mode != ~0 && r->perm != dir.mode) {
		if (strcmp(f->user, r->user) != 0)
			if (strcmp(f->user, r->group) != 0)
				return Enotowner;
	}

	/*
	 * To change group, must be owner and member of new group,
	 * or leader of current group and leader of new group.
	 * Second case cannot happen, but we check anyway.
	 */
	if (dir.gid[0] != '\0' && strcmp(r->group, dir.gid) != 0) {
		if (strcmp(f->user, r->user) == 0)
			//  if(strcmp(f->user, dir.gid) == 0)
			goto ok;
		if (strcmp(f->user, r->group) == 0)
			if (strcmp(f->user, dir.gid) == 0)
				goto ok;
		return Enotowner;
ok:	;
	}

	/* all ok; do it */
	if (dir.mode != ~0) {
		dir.mode &= ~DMDIR;	/* cannot change dir bit */
		dir.mode |= r->perm & DMDIR;
		r->perm = dir.mode;
	}
	if (dir.name[0] != '\0') {
		free(r->name);
		r->name = estrdup(dir.name);
	}
	if (dir.gid[0] != '\0')
		r->group = estrdup(dir.gid);
	if (dir.length != ~0 && dir.length != r->ndata) {
		r->data = erealloc(r->data, dir.length);
		if (r->ndata < dir.length)
			memset(r->data + r->ndata, 0, dir.length - r->ndata);
		r->ndata = dir.length;
	}
	ram[r->parent].mtime = time(0);
	return 0;
}

unsigned int ramstat(Ram * r, uint8_t * buf, unsigned int nbuf)
{
	int n;
	struct dir dir;

	dir.name = r->name;
	dir.qid = r->qid;
	dir.mode = r->perm;
	dir.length = r->ndata;
	dir.uid = r->user;
	dir.gid = r->group;
	dir.muid = r->muid;
	dir.atime = r->atime;
	dir.mtime = r->mtime;
	n = convD2M(&dir, buf, nbuf);
	if (n > 2)
		return n;
	return 0;
}

Fid *newfid(int fid)
{
	Fid *f, *ff;

	ff = 0;
	for (f = fids; f; f = f->next)
		if (f->fid == fid)
			return f;
		else if (!ff && !f->busy)
			ff = f;
	if (ff) {
		ff->fid = fid;
		return ff;
	}
	f = emalloc(sizeof *f);
	f->ram = NULL;
	f->fid = fid;
	f->next = fids;
	fids = f;
	return f;
}

void io(void)
{
	char *err, buf[40];
	int n, pid, ctl;
	Fid *fid;

	pid = getpid();
	if (private) {
		snprintf(buf, sizeof buf, "/proc/%d/ctl", pid);
		ctl = open(buf, O_WRONLY);
		if (ctl < 0) {
			fprintf(stderr, "can't protect ramfs\n");
		} else {
			fprintf(stderr, "noswap\n");
			fprintf(stderr, "private\n");
			close(ctl);
		}
	}

	for (;;) {
		/*
		 * reading from a pipe or a network device
		 * will give an error after a few eof reads.
		 * however, we cannot tell the difference
		 * between a zero-length read and an interrupt
		 * on the processes writing to us,
		 * so we wait for the error.
		 */
		n = read9pmsg(mfd[0], mdata, messagesize);
		if (n < 0) {
#warning "not handling hungup case on pipe"
#if 0
			rerrstr(buf, sizeof buf);
			if (buf[0] == '\0' || strstr(buf, "hungup")) {

				fprintf(stderr, "");
				exit(1);
			}
#endif
			error(1, 0, "mount read: %r");
		}
		if (n == 0)
			continue;
		if (convM2S(mdata, n, &thdr) == 0)
			continue;

		if (debug)
			fprintf(stderr, "ramfs %d:<-%F\n", pid, &thdr);

		if (thdr.type < 0 || thdr.type >= ARRAY_SIZE(fcalls)
			|| !fcalls[thdr.type])
			err = "bad fcall type";
		else if (((fid = newfid(thdr.fid)) == NULL || !fid->ram)
				 && needfid[thdr.type])
			err = "fid not in use";
		else
			err = (*fcalls[thdr.type]) (fid);
		if (err) {
			rhdr.type = Rerror;
			rhdr.ename = err;
		} else {
			rhdr.type = thdr.type + 1;
			rhdr.fid = thdr.fid;
		}
		rhdr.tag = thdr.tag;
		if (debug)
			fprintf(stderr, "ramfs %d:->%F\n", pid, &rhdr);
		/**/ n = convS2M(&rhdr, mdata, messagesize);
		if (n == 0)
			error(1, 0, "convS2M error on write: %r");
		if (write(mfd[1], mdata, n) != n)
			error(1, 0, "mount write: %r");
	}
}

int perm(Fid * f, Ram * r, int p)
{
	if ((p * Pother) & r->perm)
		return 1;
	if (strcmp(f->user, r->group) == 0 && ((p * Pgroup) & r->perm))
		return 1;
	if (strcmp(f->user, r->user) == 0 && ((p * Powner) & r->perm))
		return 1;
	return 0;
}

void *emalloc(uint32_t n)
{
	void *p;

	p = calloc(n, 1);
	if (!p)
		error(1, 0, "out of memory: %r");
	memset(p, 0, n);
	return p;
}

void *erealloc(void *p, uint32_t n)
{
	p = realloc(p, n);
	if (!p)
		error(1, 0, "out of memory: %r");
	return p;
}

char *estrdup(char *q)
{
	char *p;
	int n;

	n = strlen(q) + 1;
	p = calloc(n, 1);
	if (!p)
		error(1, 0, "out of memory: %r");
	memmove(p, q, n);
	return p;
}

void usage(void)
{
	fprintf(stderr, "usage: %s [-Dipsu] [-m mountpoint] [-S srvname]\n", argv0);
	fprintf(stderr, "usage");
	exit(1);
}
