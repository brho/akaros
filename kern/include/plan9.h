/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */

#ifndef ROS_KERN_PLAN9_H
#define ROS_KERN_PLAN9_H

#include <setjmp.h>
#include <atomic.h>
#include <apipe.h>
#include <rwlock.h>
#include <rendez.h>
#include <kthread.h>

/* The lock issue is still undecided. For now, we preserve the lock type
 * and plan to revisit the issue when it makes sense.
 */

/* qlocks are plan9's binary sempahore */
typedef struct semaphore qlock_t;
#define qlock_init(x) sem_init((x), 1)
#define qlock(x) sem_down(x)
#define qunlock(x) sem_up(x)
#define canqlock(x) sem_trydown(x)

/* ilock is a lock that occurs during interrupts. */
#define ilock(x) spin_lock_irqsave(x)
#define iunlock(x) spin_unlock_irqsave(x)

/* command tables for drivers. */
enum
{
	NCMDFIELD = 128
};

struct cmdbuf {
	char *buf;
	char **f;
	int nf;
};

struct cmdtab {
	int index;					/* used by client to switch on result */
	char *cmd;					/* command name */
	int narg;					/* expected #args; 0 ==> variadic */
};

/* UTF support is removed. 
 */
enum {
	UTFmax = 3,					/* maximum bytes per rune */
	Runesync = 0x80,	/* cannot represent part of a UTF sequence */
	Runeself = 0x80,	/* rune and UTF sequences are the same (<) */
	Runeerror = 0xFFFD,	/* decoding error in UTF */
};

/* defines for queue state -- currently used in qio.c */
/* queue state bits,  Qmsg, Qcoalesce, and Qkick can be set in qopen */
enum {
	/* Queue.state */
	Qstarve = (1 << 0),			/* consumer starved */
	Qmsg = (1 << 1),	/* message stream */
	Qclosed = (1 << 2),	/* queue has been closed/hungup */
	Qflow = (1 << 3),	/* producer flow controlled */
	Qcoalesce = (1 << 4),	/* coallesce packets on read */
	Qkick = (1 << 5),	/* always call the kick routine after qwrite */
};

/* stuff which has no equal. */
#define DEVDOTDOT -1
#define NUMSIZE 12	/* size of formatted number */
#define READSTR 4000	/* temporary buffer size for device reads */

/*
 * Syscall data structures
 */
#define MORDER	0x0003	/* mask for bits defining order of mounting */
#define MREPL	0x0000	/* mount replaces object */
#define MBEFORE	0x0001	/* mount goes before others in union directory */
#define MAFTER	0x0002	/* mount goes after others in union directory */
#define MCREATE	0x0004	/* permit creation in mounted directory */
#define MCACHE	0x0010	/* cache some data */
#define MMASK	0x0017	/* all bits on */

#define OREAD	0	/* open for read */
#define OWRITE	1	/* write */
#define ORDWR	2	/* read and write */
#define OEXEC	3	/* execute, == read but check execute permission */
#define OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define OCEXEC	32	/* or'ed in, close on exec */
#define ORCLOSE	64	/* or'ed in, remove on close */
#define OEXCL   0x1000	/* or'ed in, exclusive create */

#define NCONT	0	/* continue after note */
#define NDFLT	1	/* terminate after note */
#define NSAVE	2	/* clear note but hold state */
#define NRSTR	3	/* restore saved state */

#define ERRMAX		128	/* max length of error string */
#define KNAMELEN	28	/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80	/* type bit for directories */
#define QTAPPEND	0x40	/* type bit for append only files */
#define QTEXCL		0x20	/* type bit for exclusive use files */
#define QTMOUNT		0x10	/* type bit for mounted channel */
#define QTAUTH		0x08	/* type bit for authentication file */
#define QTFILE		0x00	/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMREAD		0x4	/* mode bit for read permission */
#define DMWRITE		0x2	/* mode bit for write permission */
#define DMEXEC		0x1	/* mode bit for execute permission */

enum {
	DELTAFD = 20				/* incremental increase in Fgrp.fd's */
};

/*
 * Access types in namec & channel flags
 */
enum {
	Aaccess,					/* as in stat, wstat */
	Abind,						/* for left-hand-side of bind */
	Atodir,						/* as in chdir */
	Aopen,						/* for i/o */
	Amount,						/* to be mounted or mounted upon */
	Acreate,					/* is to be created */
	Aremove,					/* will be removed by caller */

	COPEN = 0x0001,				/* for i/o */
	CMSG = 0x0002,	/* the message channel for a mount */
/*rsc	CCREATE	= 0x0004,		* permits creation if c->mnt */
	CCEXEC = 0x0008,	/* close on exec */
	CFREE = 0x0010,	/* not in use */
	CRCLOSE = 0x0020,	/* remove on close */
	CCACHE = 0x0080,	/* client cache */
};

/* struct block flag values */
enum {
	BINTR = (1 << 0),			/* allocated in interrupt mode. */

	Bipck = (1 << 2),	/* ip checksum */
	Budpck = (1 << 3),	/* udp checksum */
	Btcpck = (1 << 4),	/* tcp checksum */
	Bpktck = (1 << 5),	/* packet checksum */
};

struct path path;
struct chan chan;

/* Plan 9 Block. */
struct block {
	struct block *next;
	struct block *list;
	uint8_t *rp;				/* first unconsumed byte */
	uint8_t *wp;				/* first empty byte */
	uint8_t *lim;				/* 1 past the end of the buffer */
	uint8_t *base;				/* start of the buffer */
	void (*free) (struct block *);
	uint16_t flag;
	uint16_t checksum;			/* IP checksum of complete packet (minus media header) */
};
#define BLEN(s)	((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base)

/* linux has qstr, plan 9 has path. Path *might* need to go away
 * but there's a lot of fish to try here; maybe qstr should go away
 * instead? Let's not fret just now. The plan 9 one is a tad more
 * capable. 
 */
struct path {
	struct kref ref;
	char *s;
	struct chan **mtpt;			/* mtpt history */
	int len;					/* strlen(s) */
	int alen;					/* allocated length of s */
	int mlen;					/* number of path elements */
	int malen;					/* allocated length of mtpt */
};

struct qid {
	uint64_t path;
	unsigned long vers;
	uint8_t type;
};

struct walkqid {
	struct chan *clone;
	int nqid;
	struct qid qid[0];
};

struct dir {
	/* system-modified data */
	uint16_t type;				/* server type */
	unsigned int dev;			/* server subtype */
	/* file data */
	struct qid qid;				/* unique id from server */
	unsigned long mode;			/* permissions */
	unsigned long atime;		/* last read time */
	unsigned long mtime;		/* last write time */
	unsigned long long length;	/* file length: see <u.h> */
	char *name;					/* last element of path */
	char *uid;					/* owner name */
	char *gid;					/* group name */
	char *muid;					/* last modifier name */
};

struct dirtab {
	char name[128];
	struct qid qid;
	unsigned long long length;
	long perm;
};

struct errbuf {
	struct jmpbuf jmpbuf;
};

struct dev {
	int dc;
	char *name;

	void (*reset) ();
	void (*init) ();
	void (*shutdown) ();
	struct chan *(*attach) (char *path);
	struct walkqid *(*walk) (struct chan * from, struct chan * to, char **paths,
							 int npath);
	long (*stat) (struct chan * dir, uint8_t * path, long l);
	struct chan *(*open) (struct chan * file, int mode);
	void (*create) (struct chan * dir, char *path, int mode, int perm);
	void (*close) (struct chan * chan);
	long (*read) (struct chan * chan, void *p, long len, int64_t off);
	struct block *(*bread) (struct chan * chan, long len, int64_t off);
	long (*write) (struct chan * chan, void *p, long len, int64_t off);
	long (*bwrite) (struct chan * chan, struct block * b, int64_t off);
	void (*remove) (struct chan * chan);
	long (*wstat) (struct chan * chan, uint8_t * new, long size);
	void (*power) (int control);	/* power mgt: power(1) => on, power (0) => off */
	int (*config) (int opts, char *command, void *conf);	/* returns 0 on error */
	char *(*chaninfo) (struct chan *chan, char *ret, size_t ret_l);
};
enum {
	NSMAX = 1000,
	NSLOG = 7,
	NSCACHE = (1 << NSLOG),
};

struct mntwalk {				/* state for /proc/#/ns */
	int cddone;
	struct mhead *mh;
	struct mount *cm;
};

struct mnt
{
	spinlock_t lock;
	/* references are counted using c->ref; channels on this mount point incref(c->mchan) == Mnt.c */
	struct chan	*c;		/* Channel to file service */
	struct proc	*rip;		/* Reader in progress */
	struct mntrpc	*queue;		/* Queue of pending requests on this channel */
	unsigned int	id;		/* Multiplexer id for channel check */
	struct mnt	*list;		/* Free list */
	int	flags;		/* cache */
	int	msize;		/* data + IOHDRSZ */
	char	*version;	/* 9P version */
	struct queue	*q;		/* input queue */
};


struct mount {
	int mountid;
	struct mount *next;
	struct mhead *head;
	struct mount *copy;
	struct mount *order;
	struct chan *to;			/* channel replacing channel (mounted on in linux parlance) */
	int mflag;
	char *spec;
};

struct mhead {
	struct kref ref;
	rwlock_t lock;
	struct chan *from;			/* channel mounted upon */
	struct mount *mount;		/* what's mounted upon it */
	struct mhead *hash;			/* Hash chain */
};

/* pgrps are only used at this point to share name spaces.
 */
enum {
	MNTLOG = 5,
	MNTHASH = 1 << MNTLOG,	/* Hash to walk mount table */
	NFD = 100,	/* per process file descriptors */
};

#define MOUNTH(p,qid)	((p)->mnthash[(qid).path&((1<<MNTLOG)-1)])

struct pgrp {
	struct kref ref;			/* also used as a lock when mounting */
	int noattach;
	unsigned long pgrpid;
	spinlock_t debug;			/* single access via devproc.c */
	rwlock_t ns;				/* Namespace n read/one write lock */
	struct mhead *mnthash[MNTHASH];
};

struct chan {
	struct kref ref;
	spinlock_t lock;
	struct chan *next;			/* allocation */
	struct chan *link;
	int64_t offset;				/* in fd */
	int64_t devoffset;			/* in underlying device; see read */
	struct dev *dev;
	unsigned int devno;
	uint16_t mode;				/* read/write */
	uint16_t flag;
	struct qid qid;
	int fid;					/* for devmnt */
	unsigned long iounit;		/* chunk size for i/o; 0==default */
	struct mhead *umh;			/* mount point that derived struct chan; used in unionread */
	struct chan *umc;			/* channel in union; held for union read */
	// need a queued lock not a spin lock. QLock    umqlock;        /* serialize unionreads */
	qlock_t umqlock;
	int uri;					/* union read index */
	int dri;					/* devdirread index */
	unsigned char *dirrock;		/* directory entry rock for translations */
	int nrock;
	int mrock;
	qlock_t rockqlock;

	int ismtpt;
	int mc;
	//Mntcache*mc;            /* Mount cache pointer */
	struct mnt *mux;			/* Mnt for clients using me for messages */
	union {
		void *aux;
		struct qid pgrpid;		/* for #p/notepg */
		unsigned long mid;		/* for ns in devproc */
	};
	struct chan *mchan;			/* channel to mounted server */
	struct qid mqid;			/* qid of root of mount point */
	struct path *path;
};

struct fgrp {
	spinlock_t lock;
	int mountid;
	struct kref ref;
	struct chan **fd;
	int nfd;					/* number allocated */
	int maxfd;					/* highest fd in use */
	int exceed;					/* debugging */
};

/*
 *  IO queues
 */
#if 1
struct queue
{
	spinlock_t lock;

	struct block*	bfirst;		/* buffer */
	struct block*	blast;

	int	len;		/* bytes allocated to queue */
	int	dlen;		/* data bytes in queue */
	int	limit;		/* max bytes in queue */
	int	inilim;		/* initial limit */
	int	state;
	int	noblock;	/* true if writes return immediately when q full */
	int	eof;		/* number of eofs read by user */

	void	(*kick)(void*);	/* restart output */
	void	(*bypass)(void*, struct block*);	/* bypass queue altogether */
	void*	arg;		/* argument to kick */

        qlock_t	rlock;		/* mutex for reading processes */
	struct rendez rr;		/* process waiting to read */
	qlock_t	wlock;		/* mutex for writing processes */
	struct rendez	wr;		/* process waiting to write */

	char	err[ERRMAX];
};

#else
struct queue {
	spinlock_t lock;
	/* short name to make debugging simpler. Leave empty if you wish. */
	char name[32];
	struct atomic_pipe pipe;
	/* we need this placeholder which we manipulate
	 * to concat/pullup blocks.
	 */
	struct block *head;
	int len;
	int limit;
	int inilim;

	int dlen;					/* data bytes in queue */
	int state;
	int noblock;				/* true if writes return immediately when q full */
	int eof;					/* number of eofs read by user */

	void (*kick) (void *);		/* restart output */
	void (*bypass) (void *, struct block *);	/* bypass queue altogether */
	void *arg;					/* argument to kick */
	qlock_t rlock;				/* mutex for reading processes */
	/* how do we do this Rendez rr;     / * process waiting to read */
	qlock_t wlock;				/* mutex for writing processes */

	char err[ERRMAX];
};
#endif
extern unsigned int qiomaxatomic;

typedef int devgen_t(struct chan *c, char *name, struct dirtab *dirtab, int,
					 int, struct dir *);

extern char Enoerror[];			/* no error */
extern char Emount[];			/* inconsistent mount */
extern char Eunmount[];			/* not mounted */
extern char Eismtpt[];			/* is a mount point */
extern char Eunion[];			/* not in union */
extern char Emountrpc[];		/* mount rpc error */
extern char Eshutdown[];		/* device shut down */
extern char Enocreate[];		/* mounted directory forbids creation */
extern char Enonexist[];		/* file does not exist */
extern char Eexist[];			/* file already exists */
extern char Ebadsharp[];		/* unknown device in # filename */
extern char Enotdir[];			/* not a directory */
extern char Eisdir[];			/* file is a directory */
extern char Ebadchar[];			/* bad character in file name */
extern char Efilename[];		/* file name syntax */
extern char Eperm[];			/* permission denied */
extern char Ebadusefd[];		/* inappropriate use of fd */
extern char Ebadarg[];			/* bad arg in system call */
extern char Einuse[];			/* device or object already in use */
extern char Eio[];				/* i/o error */
extern char Etoobig[];			/* read or write too large */
extern char Etoosmall[];		/* read or write too small */
extern char Enoport[];			/* network port not available */
extern char Ehungup[];			/* i/o on hungup channel */
extern char Ebadctl[];			/* bad process or channel control request */
extern char Enodev[];			/* no free devices */
extern char Eprocdied[];		/* process exited */
extern char Enochild[];			/* no living children */
extern char Eioload[];			/* i/o error in demand load */
extern char Enovmem[];			/* virtual memory allocation failed */
extern char Ebadfd[];			/* fd out of range or not open */
extern char Enofd[];			/* no free file descriptors */
extern char Eisstream[];		/* seek on a stream */
extern char Ebadexec[];			/* exec header invalid */
extern char Etimedout[];		/* connection timed out */
extern char Econrefused[];		/* connection refused */
extern char Econinuse[];		/* connection in use */
extern char Eintr[];			/* interrupted */
extern char Enomem[];			/* kernel allocate failed */
extern char Enoswap[];			/* swap space full */
extern char Esoverlap[];		/* segments overlap */
extern char Eshort[];			/* i/o count too small */
extern char Egreg[];			/* ken has left the building */
extern char Ebadspec[];			/* bad attach specifier */
extern char Enoreg[];			/* process has no saved registers */
extern char Enoattach[];		/* mount/attach disallowed */
extern char Eshortstat[];		/* stat buffer too small */
extern char Ebadstat[];			/* malformed stat buffer */
extern char Enegoff[];			/* negative i/o offset */
extern char Ecmdargs[];			/* wrong #args in control message */
extern char Ebadip[];			/* bad ip address syntax */
extern char Edirseek[];			/* seek in directory */

/* add 1 in case they forget they need an entry for the passed-in errbuf */
#define ERRSTACK(x) struct errbuf *prev_errbuf; struct errbuf errstack[(x)];   \
                    int curindex = 0;
#define waserror() (errpush(errstack, ARRAY_SIZE(errstack), &curindex,         \
                            &prev_errbuf) ||                                   \
                    setjmp(&(get_cur_errbuf()->jmpbuf)))
#define error(x,...) {set_errstr(x, ##__VA_ARGS__);                            \
                      longjmp(&get_cur_errbuf()->jmpbuf, (void *)x);}
#define nexterror() {errpop(errstack, ARRAY_SIZE(errstack), &curindex,         \
                            prev_errbuf);                                      \
                     longjmp(&(get_cur_errbuf())->jmpbuf, (void *)1);}
#define poperror() {errpop(errstack, ARRAY_SIZE(errstack), &curindex,          \
                           prev_errbuf);}

/* this would be useful at some point ... */
static inline uintptr_t getcallerpc(void *unused)
{
	return 0;
}

void kstrcpy(char *s, char *t, int ns);

/* kern/src/chan.c */
int findmount(struct chan **cp, struct mhead **mp, int dc, unsigned int devno,
			  struct qid qid);
int eqchanddq(struct chan *c, int dc, unsigned int devno, struct qid qid,
			  int skipvers);
char *chanpath(struct chan *c);
int isdotdot(char *p);
int emptystr(char *s);
void kstrdup(char **p, char *s);
struct chan *newchan(void);
struct path *newpath(char *s);
void pathclose(struct path *p);
void chanfree(struct chan *c);
void cclose(struct chan *c);
void ccloseq(struct chan *c);
struct chan *cunique(struct chan *c);
int eqqid(struct qid a, struct qid b);
struct mhead *newmhead(struct chan *from);
int cmount(struct chan **newp, struct chan *old, int flag, char *spec);
void cunmount(struct chan *mnt, struct chan *mounted);
struct chan *cclone(struct chan *c);
int walk(struct chan **cp, char **names, int nnames, int nomount, int *nerror);
struct chan *createdir(struct chan *c, struct mhead *mh);
struct chan *namec(char *aname, int amode, int omode, int perm);
char *skipslash(char *name);
void validname(char *aname, int slashok);
char *validnamedup(char *aname, int slashok);
void isdir(struct chan *c);
void putmhead(struct mhead *mh);

/* plan9.c */
char *validname0(char *aname, int slashok, int dup, uintptr_t pc);

/* cleanname.c */
char *cleanname(char *name);

/* kern/src/pgrp.c */
struct fgrp *dupfgrp(struct fgrp *f);
void pgrpinsert(struct mount **order, struct mount *m);
void forceclosefgrp(void);
struct mount *newmount(struct mhead *mh, struct chan *to, int flag, char *spec);
void mountfree(struct mount *m);
void resrcwait(char *reason);
struct pgrp *newpgrp(void);

/* kern/src/devtabc */
void devtabreset();
void devtabinit();
void devtabshutdown();
struct dev *devtabget(int dc, int user);
long devtabread(struct chan *, void *buf, long n, int64_t off);

/* kern/src/allocplan9block.c */
struct block *allocb(int size);
struct block *iallocb(int size);
void freeb(struct block *b);
void checkb(struct block *b, char *msg);
void iallocsummary(void);

/* kern/src/dev.c */
int devgen(struct chan *c, char *name, struct dirtab *tab, int ntab, int i,
		   struct dir *dp);
void mkqid(struct qid *q, int64_t path, uint32_t vers, int type);
void devdir(struct chan *c, struct qid qid, char *n, int64_t length, char *user,
			long perm, struct dir *db);
void devreset();
void devinit();
void devshutdown();
struct chan *devattach(int dc, char *spec);
struct chan *devclone(struct chan *c);
struct walkqid *devwalk(struct chan *c, struct chan *nc, char **name, int nname,
						struct dirtab *tab, int ntab, devgen_t * gen);
long devstat(struct chan *c, uint8_t * db, long n, struct dirtab *tab, int ntab,
			 devgen_t * gen);
long devdirread(struct chan *c, char *d, long n, struct dirtab *tab, int ntab,
				devgen_t * gen);
void devpermcheck(char *fileuid, int perm, int omode);
struct chan *devopen(struct chan *c, int omode, struct dirtab *tab, int ntab,
					 devgen_t * gen);
void devcreate(struct chan *a, char *b, int c, int d);
struct block *devbread(struct chan *c, long n, int64_t offset);
long devbwrite(struct chan *c, struct block *bp, int64_t offset);
void devremove(struct chan *c);
long devwstat(struct chan *c, uint8_t * a, long b);
void devpower(int onoff);
int devconfig(int a, char *b, void *v);
char *devchaninfo(struct chan *chan, char *ret, size_t ret_l);

/* kern/src/plan9file.c */
struct chan *fdtochan(int fd, int mode, int chkmnt, int iref);
void validstat(uint8_t *s, unsigned long n);
int openmode(int omode);
long sysread(int fd, void *p, size_t n, off_t off);
long syswrite(int fd, void *p, size_t n, off_t off);
int sysstat(char *name, uint8_t * statbuf, int len);
int sysfstat(int fd, uint8_t * statbuf, int len);
int sysopen(char *name, int omode);
int sysclose(int fd);
int sysdup(int ofd, int nfd);
int bindmount(int ismount, int fd, int afd,
	      char* arg0, char* arg1, int flag, char* spec);
int sysunmount(char *name, char *old);

int plan9setup(struct proc *new_proc, struct proc *parent);
long readstr(long offset, char *buf, long n, char *str);
int readnum(unsigned long off, char *buf, unsigned long n, unsigned long val,
			int size);
void print_9ns_files(struct proc *p);

/* ker/src/err.c */
int errpush(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf **prev_errbuf);
void errpop(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf *prev_errbuf);
/* kern/src/qio.c */
void ixsummary(void);
void freeblist(struct block *b);
struct block *padblock(struct block *bp, int size);
int blocklen(struct block *bp);
int blockalloclen(struct block *bp);
struct block *concatblock(struct block *bp);
struct block *pullupblock(struct block *bp, int n);
struct block *pullupqueue(struct queue *q, int n);
struct block *trimblock(struct block *bp, int offset, int len);
struct block *copyblock(struct block *bp, int count);
struct block *adjustblock(struct block *bp, int len);
int pullblock(struct block **bph, int count);
struct block *qhead(struct queue *q);
struct block *qget(struct queue *q);
int qdiscard(struct queue *q, int len);
int qconsume(struct queue *q, void *vp, int len);
int qpass(struct queue *q, struct block *b);
int qpassnolim(struct queue *q, struct block *b);
struct block *packblock(struct block *bp);
int qproduce(struct queue *q, void *vp, int len);
struct block *qcopy(struct queue *q, int len, uint32_t offset);
struct queue *qopen(int nblock, int msg, void (*kick) (void *), void *arg);
struct queue *qbypass(void (*bypass) (void *, struct block *), void *arg);
void qaddlist(struct queue *q, struct block *b);
struct block *qremove(struct queue *q);
struct block *bl2mem(uint8_t * p, struct block *b, int n);
struct block *mem2bl(uint8_t * p, int len);
void qputback(struct queue *q, struct block *b);
struct block *qbread(struct queue *q, int len);
long qread(struct queue *q, void *vp, int len);
long qbwrite(struct queue *q, struct block *b);
int qwrite(struct queue *q, void *vp, int len);
int qiwrite(struct queue *q, void *vp, int len);
void qclose(struct queue *q);
void qfree(struct queue *q);
void qhangup(struct queue *q, char *msg);
int qisclosed(struct queue *q);
void qreopen(struct queue *q);
int qlen(struct queue *q);
int qwindow(struct queue *q);
int qcanread(struct queue *q);
void qsetlimit(struct queue *q, int limit);
void qnoblock(struct queue *q, int onoff);
void qflush(struct queue *q);
int qfull(struct queue *q);
int qstate(struct queue *q);

/* xdr */
unsigned int convM2D(uint8_t * buf, unsigned int nbuf, struct dir *d,
					 char *strs);
unsigned int sizeD2M(struct dir *d);
unsigned int convD2M(struct dir *d, uint8_t * buf, unsigned int nbuf);

/* kern/src/ns/parse.c */
struct cmdbuf *parsecmd(char *p, int n);
void cmderror(struct cmdbuf *cb, char *s);
struct cmdtab *lookupcmd(struct cmdbuf *cb, struct cmdtab *ctab, int nctab);
/* kern/src/ns/tokenize.c */
int gettokens(char *s, char **args, int maxargs, char *sep);
int tokenize(char *s, char **args, int maxargs);
/* ker/src/ns/getfields.c */
int getfields(char *str, char **args, int max, int mflag, char *unused_set);

/* plan 9 has a concept of a hostowner, which is a name. For now, we got with a define. */
#define eve "eve"
#define iseve() (1)

/* functions we need to do something with someday */
#define postnote(...)
#define pexit(...)

/* kern/drivers/dev/misc.c */
int nrand(int n);
/* kern/drivers/dev/srv.c */
char*srvname(struct chan *c);
/* kern/drivers/dev/proc.c */
void int2flag(int flag, char *s);

/* kern/drivers/dev/random.c */
uint32_t randomread(void *xp, uint32_t n);

/* include for now.
 */

#include <ip.h>

/* kernel-level IP stuff. */
struct fragment4 {
	struct block *blist;
	struct fragment4 *next;
	uint32_t src;
	uint32_t dst;
	uint16_t id;
	uint32_t age;
};

struct fragment6 {
	struct block *blist;
	struct fragment6 *next;
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	unsigned int id;
	uint32_t age;
};

struct Ipfrag {
	uint16_t foff;
	uint16_t flen;

	uint8_t payload[];
};

#define IPFRAGSZ offsetof(struct Ipfrag, payload[0])
#define CLASS(p) ((*(uint8_t*)(p))>>6)

/* an instance of struct IP */
struct IP {
	uint64_t stats[Nipstats];

	qlock_t fraglock4;
	struct fragment4 *flisthead4;
	struct fragment4 *fragfree4;
	struct kref id4;

	qlock_t fraglock6;
	struct fragment6 *flisthead6;
	struct fragment6 *fragfree6;
	struct kref id6;

	int iprouting;				/* true if we route like a gateway */
};

/* on the wire packet header */
struct Ip4hdr {
	uint8_t vihl;				/* Version and header length */
	uint8_t tos;				/* Type of service */
	uint8_t length[2];			/* packet length */
	uint8_t id[2];				/* ip->identification */
	uint8_t frag[2];			/* Fragment information */
	uint8_t ttl;				/* Time to live */
	uint8_t proto;				/* struct protocol */
	uint8_t cksum[2];			/* Header checksum */
	uint8_t src[4];				/* struct IP source */
	uint8_t dst[4];				/* struct IP destination */
};

/*
 *  one per conversation directory
 */
struct conv {
	qlock_t qlock;

	int x;						/* conversation index */
	struct proto *p;

	int restricted;				/* remote port is restricted */
	unsigned int ttl;			/* max time to live */
	unsigned int tos;			/* type of service */
	int ignoreadvice;			/* don't terminate connection on icmp errors */

	uint8_t ipversion;
	uint8_t laddr[IPaddrlen];	/* local struct struct IP address */
	uint8_t raddr[IPaddrlen];	/* remote struct struct IP address */
	uint16_t lport;				/* local port number */
	uint16_t rport;				/* remote port number */

	char *owner;				/* protections */
	int perm;
	int inuse;					/* opens of listen/data/ctl */
	int length;
	int state;

	int maxfragsize;			/* If set, used for fragmentation */

	/* udp specific */
	int headers;				/* data src/dst headers in udp */
	int reliable;				/* true if reliable udp */

	struct conv *incall;		/* calls waiting to be listened for */
	struct conv *next;

	struct queue *rq;			/* queued data waiting to be read */
	struct queue *wq;			/* queued data waiting to be written */
	struct queue *eq;			/* returned error packets */
	struct queue *sq;			/* snooping queue */
	struct kref snoopers;		/* number of processes with snoop open */

	qlock_t car;
	struct rendez cr;			/* looks to be unused... */
	char cerr[ERRMAX];

	qlock_t listenq;
	struct rendez listenr;

	struct Ipmulti *multi;		/* multicast bindings for this interface */

	void *ptcl;					/* protocol specific stuff */

	struct route *r;			/* last route used */
	uint32_t rgen;				/* routetable generation for *r */
};

struct medium {
	char *name;
	int hsize;					/* medium header size */
	int mintu;					/* default min mtu */
	int maxtu;					/* default max mtu */
	int maclen;					/* mac address length */
	void (*bind) (struct ipifc *, int, char **);
	void (*unbind) (struct ipifc *);
	void (*bwrite) (struct ipifc * ifc, struct block * b, int version,
					uint8_t * ip);

	/* for arming interfaces to receive multicast */
	void (*addmulti) (struct ipifc * ifc, uint8_t * a, uint8_t * ia);
	void (*remmulti) (struct ipifc * ifc, uint8_t * a, uint8_t * ia);

	/* process packets written to 'data' */
	void (*pktin) (struct fs * f, struct ipifc * ifc, struct block * bp);

	/* routes for router boards */
	void (*addroute) (struct ipifc * ifc, int, uint8_t *, uint8_t *, uint8_t *,
					  int);
	void (*remroute) (struct ipifc * ifc, int, uint8_t *, uint8_t *);
	void (*flushroutes) (struct ipifc * ifc);

	/* for routing multicast groups */
	void (*joinmulti) (struct ipifc * ifc, uint8_t * a, uint8_t * ia);
	void (*leavemulti) (struct ipifc * ifc, uint8_t * a, uint8_t * ia);

	/* address resolution */
	void (*ares) (struct fs *, int, uint8_t *, uint8_t *, int, int);	/* resolve */
	void (*areg) (struct ipifc *, uint8_t *);	/* register */

	/* v6 address generation */
	void (*pref2addr) (uint8_t * pref, uint8_t * ea);

	int unbindonclose;			/* if non-zero, unbind on last close */
};

struct ipifc {
	spinlock_t lock;
	rwlock_t rwlock;

	struct conv *conv;			/* link to its conversation structure */
	char dev[64];				/* device we're attached to */
	struct medium *medium;		/* Media pointer */
	int maxtu;					/* Maximum transfer unit */
	int mintu;					/* Minumum tranfer unit */
	int mbps;					/* megabits per second */
	void *arg;					/* medium specific */
	int reassemble;				/* reassemble struct IP packets before forwarding */

	/* these are used so that we can unbind on the fly */
	spinlock_t idlock;
	uint8_t ifcid;				/* incremented each 'bind/unbind/add/remove' */
	int ref;					/* number of proc's using this ipifc */
	//Rendez    wait;       /* where unbinder waits for ref == 0 */
	int unbinding;

	uint8_t mac[MAClen];		/* MAC address */

	struct iplifc *lifc;		/* logical interfaces on this physical one */

	uint32_t in, out;			/* message statistics */
	uint32_t inerr, outerr;		/* ... */

	uint8_t sendra6;			/* flag: send router advs on this ifc */
	uint8_t recvra6;			/* flag: recv router advs on this ifc */
	struct routerparams rp;		/* router parameters as in RFC 2461, pp.40—43.
								   used only if node is router */
};

/* logical interface associated with a physical one */
struct iplifc {
	uint8_t local[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t remote[IPaddrlen];
	uint8_t net[IPaddrlen];
	uint8_t tentative;			/* =1 => v6 dup disc on, =0 => confirmed unique */
	uint8_t onlink;				/* =1 => onlink, =0 offlink. */
	uint8_t autoflag;			/* v6 autonomous flag */
	long validlt;				/* v6 valid lifetime */
	long preflt;				/* v6 preferred lifetime */
	long origint;				/* time when addr was added */
	struct Iplink *link;		/* addresses linked to this lifc */
	struct iplifc *next;
};

/* binding twixt struct Ipself and struct iplifc */
struct Iplink {
	struct Ipself *self;
	struct iplifc *lifc;
	struct Iplink *selflink;	/* next link for this local address */
	struct Iplink *lifclink;	/* next link for this ifc */
	uint32_t expire;
	struct Iplink *next;		/* free list */
	struct kref ref;
};

/*
 *  one per multicast-lifc pair used by a struct conv
 */
struct Ipmulti {
	uint8_t ma[IPaddrlen];
	uint8_t ia[IPaddrlen];
	struct Ipmulti *next;
};

/*
 *  hash table for 2 ip addresses + 2 ports
 */
enum {
	Nipht = 521,				/* convenient prime */

	IPmatchexact = 0,	/* match on 4 tuple */
	IPmatchany,	/* *!* */
	IPmatchport,	/* *!port */
	IPmatchaddr,	/* addr!* */
	IPmatchpa,	/* addr!port */
};

struct iphash {
	struct iphash *next;
	struct conv *c;
	int match;
};

struct Ipht {
	spinlock_t lock;
	rwlock_t rwlock;
	struct iphash *tab[Nipht];
};
void iphtadd(struct Ipht *, struct conv *);
void iphtrem(struct Ipht *, struct conv *);
struct conv *iphtlook(struct Ipht *ht, uint8_t * sa, uint16_t sp, uint8_t * da,
					  uint16_t dp);

/*
 *  one per multiplexed protocol
 */
struct proto {
	qlock_t qlock;
	char *name;					/* protocol name */
	int x;						/* protocol index */
	int ipproto;				/* ip protocol type */

	char *(*connect) (struct conv *, char **, int);
	char *(*announce) (struct conv *, char **, int);
	char *(*bind) (struct conv *, char **, int);
	int (*state) (struct conv *, char *, int);
	void (*create) (struct conv *);
	void (*close) (struct conv *);
	void (*rcv) (struct proto *, struct ipifc *, struct block *);
	char *(*ctl) (struct conv *, char **, int);
	void (*advise) (struct proto *, struct block *, char *);
	int (*stats) (struct proto *, char *, int);
	int (*local) (struct conv *, char *, int);
	int (*remote) (struct conv *, char *, int);
	int (*inuse) (struct conv *);
	int (*gc) (struct proto *);	/* returns true if any conversations are freed */
	void (*newconv) (struct proto *, struct conv *);

	struct fs *f;				/* file system this proto is part of */
	struct conv **conv;			/* array of conversations */
	int ptclsize;				/* size of per protocol ctl block */
	int nc;						/* number of conversations */
	int ac;
	struct qid qid;				/* qid for protocol directory */
	uint16_t nextrport;

	void *priv;
};

/*
 *  one per struct IP protocol stack
 */
struct fs {
	rwlock_t rwlock;
	int dev;

	int np;
	struct proto *p[Maxproto + 1];	/* list of supported protocols */
	struct proto *t2p[256];		/* vector of all protocols */
	struct proto *ipifc;		/* kludge for ipifcremroute & ipifcaddroute */
	struct proto *ipmux;		/* kludge for finding an ip multiplexor */

	struct IP *ip;
	struct Ipselftab *self;
	struct arp *arp;
	struct v6params *v6p;

	struct route *v4root[1 << Lroot];	/* v4 routing forest */
	struct route *v6root[1 << Lroot];	/* v6 routing forest */
	struct route *queue;		/* used as temp when reinjecting routes */

	struct netlog *alog;

	char ndb[1024];				/* an ndb entry for this interface */
	int ndbvers;
	long ndbmtime;
};

/* one per default router known to host */
struct v6router {
	uint8_t inuse;
	struct ipifc *ifc;
	int ifcid;
	uint8_t routeraddr[IPaddrlen];
	long ltorigin;
	struct routerparams rp;
};

struct v6params {
	struct routerparams rp;		/* v6 params, one copy per node now */
	struct hostparams hp;
	struct v6router v6rlist[3];	/* max 3 default routers, currently */
	int cdrouter;				/* uses only v6rlist[cdrouter] if */
	/* cdrouter >= 0. */
};

/*
 *  logging
 */
enum {
	Logip = 1 << 1,
	Logtcp = 1 << 2,
	Logfs = 1 << 3,
	Logicmp = 1 << 5,
	Logudp = 1 << 6,
	Logcompress = 1 << 7,
	Loggre = 1 << 9,
	Logppp = 1 << 10,
	Logtcprxmt = 1 << 11,
	Logigmp = 1 << 12,
	Logudpmsg = 1 << 13,
	Logipmsg = 1 << 14,
	Logrudp = 1 << 15,
	Logrudpmsg = 1 << 16,
	Logesp = 1 << 17,
	Logtcpwin = 1 << 18,
};

/*
 *  iproute.c
 */

enum {

	/* type bits */
	Rv4 = (1 << 0),				/* this is a version 4 route */
	Rifc = (1 << 1),	/* this route is a directly connected interface */
	Rptpt = (1 << 2),	/* this route is a pt to pt interface */
	Runi = (1 << 3),	/* a unicast self address */
	Rbcast = (1 << 4),	/* a broadcast self address */
	Rmulti = (1 << 5),	/* a multicast self address */
	Rproxy = (1 << 6),	/* this route should be proxied */
};

struct routewalk {
	int o;
	int h;
	char *p;
	char *e;
	void *state;
	void (*walk) (struct route *, struct routewalk *);
};

struct routeTree {
	struct route *right;
	struct route *left;
	struct route *mid;
	uint8_t depth;
	uint8_t type;
	uint8_t ifcid;				/* must match ifc->id */
	struct ipifc *ifc;
	char tag[4];
	struct kref ref;
};

struct V4route {
	uint32_t address;
	uint32_t endaddress;
	uint8_t gate[IPv4addrlen];
};

struct V6route {
	uint32_t address[IPllen];
	uint32_t endaddress[IPllen];
	uint8_t gate[IPaddrlen];
};

struct route {
	struct routeTree routeTree;

	union {
		struct V6route v6;
		struct V4route v4;
	};
};

/*
 *  Hanging off every ip channel's ->aux is the following structure.
 *  It maintains the state used by devip and iproute.
 */
struct IPaux {
	char *owner;				/* the user that did the attach */
	char tag[4];
};

extern struct IPaux *newipaux(char *, char *);

/*
 *  arp.c
 */
struct arpent {
	uint8_t ip[IPaddrlen];
	uint8_t mac[MAClen];
	struct medium *type;		/* media type */
	struct arpent *hash;
	struct block *hold;
	struct block *last;
	unsigned int ctime;			/* time entry was created or refreshed */
	unsigned int utime;			/* time entry was last used */
	uint8_t state;
	struct arpent *nextrxt;		/* re-transmit chain */
	unsigned int rtime;			/* time for next retransmission */
	uint8_t rxtsrem;
	struct ipifc *ifc;
	uint8_t ifcid;				/* must match ifc->id */
};

#define	ipmove(x, y) memmove(x, y, IPaddrlen)
#define	ipcmp(x, y) ( (x)[IPaddrlen-1] != (y)[IPaddrlen-1] || memcmp(x, y, IPaddrlen) )

extern uint8_t IPv4bcast[IPaddrlen];
extern uint8_t IPv4bcastobs[IPaddrlen];
extern uint8_t IPv4allsys[IPaddrlen];
extern uint8_t IPv4allrouter[IPaddrlen];
extern uint8_t IPnoaddr[IPaddrlen];
extern uint8_t v4prefix[IPaddrlen];
extern uint8_t IPallbits[IPaddrlen];

#define	NOW	tsc2msec(read_tsc_serialized())
#define	seconds() tsc2sec(read_tsc_serialized())

/*
 *  media
 */
extern struct medium ethermedium;
extern struct medium nullmedium;
extern struct medium pktmedium;

extern uint8_t v6allnodesN[IPaddrlen];
extern uint8_t v6allnodesL[IPaddrlen];
extern uint8_t v6allroutersN[IPaddrlen];
extern uint8_t v6allroutersL[IPaddrlen];
extern uint8_t v6allnodesNmask[IPaddrlen];
extern uint8_t v6allnodesLmask[IPaddrlen];
extern uint8_t v6solicitednode[IPaddrlen];
extern uint8_t v6solicitednodemask[IPaddrlen];
extern uint8_t v6Unspecified[IPaddrlen];
extern uint8_t v6loopback[IPaddrlen];
extern uint8_t v6loopbackmask[IPaddrlen];
extern uint8_t v6linklocal[IPaddrlen];
extern uint8_t v6linklocalmask[IPaddrlen];
extern uint8_t v6multicast[IPaddrlen];
extern uint8_t v6multicastmask[IPaddrlen];

extern int v6llpreflen;
extern int v6mcpreflen;
extern int v6snpreflen;
extern int v6aNpreflen;
extern int v6aLpreflen;

extern int ReTransTimer;

void ipv62smcast(uint8_t *, uint8_t *);
void icmpns(struct fs *f, uint8_t* src, int suni, uint8_t* targ, int tuni,
	    uint8_t* mac);
void icmpna(struct fs *f, uint8_t* src, uint8_t* dst, uint8_t* targ, uint8_t* mac,
	    uint8_t flags);
void icmpttlexceeded6(struct fs *f, struct ipifc *ifc, struct block *bp);
void icmppkttoobig6(struct fs *f, struct ipifc *ifc, struct block *bp);
void icmphostunr(struct fs *f, struct ipifc *ifc,
		 struct block *bp, int code, int free);

static inline int abs(int x)
{
	if (x < 0)
		return -x;
	return x;
}

/* kern/src/net/nixip/arp.c */
void arpinit(struct fs *f);
void cleanarpent(struct arp *arp, struct arpent *a);
struct arpent *arpget(struct arp *arp, struct block *bp, int version, struct ipifc *ifc, uint8_t *ip, uint8_t *mac);
void arprelease(struct arp *arp, struct arpent *arpent);
struct block *arpresolve(struct arp *arp, struct arpent *a, struct medium *type, uint8_t *mac);
void arpenter(struct fs *fs, int version, uint8_t *ip, uint8_t *mac, int n, int refresh);
int arpwrite(struct fs *fs, char *s, int len);
int arpread(struct arp *arp, char *p, uint32_t offset, int len);
int rxmitsols(struct arp *arp);
/* kern/src/net/nixip/chandial.c */
struct chan *chandial(char *dest, char *local, char *dir, struct chan **ctlp);
/* kern/src/net/nixip/devip.c */
struct IPaux *newipaux(char *owner, char *tag);
void closeconv(struct conv *cv);
char *setluniqueport(struct conv *c, int lport);
char *setlport(struct conv *c);
char *setladdrport(struct conv *c, char *str, int announcing);
char *Fsstdconnect(struct conv *c, char *argv[], int argc);
char *Fsstdannounce(struct conv *c, char *argv[], int argc);
char *Fsstdbind(struct conv *c, char *argv[], int argc);
int Fsproto(struct fs *f, struct proto *p);
int Fsbuiltinproto(struct fs *f, uint8_t proto);
struct conv *Fsprotoclone(struct proto *p, char *user);
int Fsconnected(struct conv *c, char *msg);
struct proto *Fsrcvpcol(struct fs *f, uint8_t proto);
struct proto *Fsrcvpcolx(struct fs *f, uint8_t proto);
struct conv *Fsnewcall(struct conv *c, uint8_t *raddr, uint16_t rport, uint8_t *laddr, uint16_t lport, uint8_t version);
long ndbwrite(struct fs *f, char *a, uint32_t off, int n);
uint32_t scalednconv(void);
/* kern/src/net/nixip/ethermedium.c */
void ethermediumlink(void);
/* kern/src/net/nixip/icmp6.c */
void icmpadvise6(struct proto *icmp, struct block *bp, char *msg);
char *icmpctl6(struct conv *c, char **argv, int argc);
void icmpns(struct fs *f, uint8_t *src, int suni, uint8_t *targ, int tuni, uint8_t *mac);
void icmpna(struct fs *f, uint8_t *src, uint8_t *dst, uint8_t *targ, uint8_t *mac, uint8_t flags);
void icmphostunr(struct fs *f, struct ipifc *ifc, struct block *bp, int code, int free);
void icmpttlexceeded6(struct fs *f, struct ipifc *ifc, struct block *bp);
void icmppkttoobig6(struct fs *f, struct ipifc *ifc, struct block *bp);
int icmpstats6(struct proto *icmp6, char *buf, int len);
void icmp6init(struct fs *fs);
/* kern/src/net/nixip/icmp.c */
char *icmpconnect(struct conv *c, char **argv, int argc);
int icmpstate(struct conv *c, char *state, int n);
char *icmpannounce(struct conv *c, char **argv, int argc);
void icmpclose(struct conv *c);
void icmpttlexceeded(struct fs *f, uint8_t *ia, struct block *bp);
void icmpnoconv(struct fs *f, struct block *bp);
void icmpcantfrag(struct fs *f, struct block *bp, int mtu);
void icmpadvise(struct proto *icmp, struct block *bp, char *msg);
int icmpstats(struct proto *icmp, char *buf, int len);
void icmpinit(struct fs *fs);
/* kern/src/net/nixip/ipaux.c */
uint16_t ptclcsum(struct block *bp, int offset, int len);
void ipv62smcast(uint8_t *smcast, uint8_t *a);
int parsemac(uint8_t *to, char *from, int len);
uint32_t iphash(uint8_t *sa, uint16_t sp, uint8_t *da, uint16_t dp);
void iphtadd(struct Ipht *ht, struct conv *c);
void iphtrem(struct Ipht *ht, struct conv *c);
struct conv *iphtlook(struct Ipht *ht, uint8_t *sa, uint16_t sp, uint8_t *da, uint16_t dp);
/* kern/src/net/nixip/ip.c */
void ip_init_6(struct fs *f);
void initfrag(struct IP *ip, int size);
void ip_init(struct fs *f);
void iprouting(struct fs *f, int on);
int ipoput4(struct fs *f, struct block *bp, int gating, int ttl, int tos, struct conv *c);
void ipiput4(struct fs *f, struct ipifc *ifc, struct block *bp);
int ipstats(struct fs *f, char *buf, int len);
struct block *ip4reassemble(struct IP *ip, int offset, struct block *bp, struct Ip4hdr *ih);
void ipfragfree4(struct IP *ip, struct fragment4 *frag);
struct fragment4 *ipfragallo4(struct IP *ip);
uint16_t ipcsum(uint8_t *addr);
/* kern/src/net/nixip/ipifc.c */
void addipmedium(struct medium *med);
struct medium *ipfindmedium(char *name);
char *ipifcsetmtu(struct ipifc *ifc, char **argv, int argc);
char *ipifcadd(struct ipifc *ifc, char **argv, int argc, int tentative, struct iplifc *lifcp);
char *ipifcrem(struct ipifc *ifc, char **argv, int argc);
void ipifcaddroute(struct fs *f, int vers, uint8_t *addr, uint8_t *mask, uint8_t *gate, int type);
void ipifcremroute(struct fs *f, int vers, uint8_t *addr, uint8_t *mask);
char *ipifcra6(struct ipifc *ifc, char **argv, int argc);
int ipifcstats(struct proto *ipifc, char *buf, int len);
void ipifcinit(struct fs *f);
long ipselftabread(struct fs *f, char *cp, uint32_t offset, int n);
int iptentative(struct fs *f, uint8_t *addr);
int ipforme(struct fs *f, uint8_t *addr);
struct ipifc *findipifc(struct fs *f, uint8_t *remote, int type);
int v6addrtype(uint8_t *addr);
void findlocalip(struct fs *f, uint8_t *local, uint8_t *remote);
int ipv4local(struct ipifc *ifc, uint8_t *addr);
int ipv6local(struct ipifc *ifc, uint8_t *addr);
int ipv6anylocal(struct ipifc *ifc, uint8_t *addr);
struct iplifc *iplocalonifc(struct ipifc *ifc, uint8_t *ip);
int ipproxyifc(struct fs *f, struct ipifc *ifc, uint8_t *ip);
int ipismulticast(uint8_t *ip);
int ipisbm(uint8_t *ip);
void ipifcaddmulti(struct conv *c, uint8_t *ma, uint8_t *ia);
void ipifcremmulti(struct conv *c, uint8_t *ma, uint8_t *ia);
char *ipifcadd6(struct ipifc *ifc, char **argv, int argc);
/* kern/src/net/nixip/iproute.c */
void v4addroute(struct fs *f, char *tag, uint8_t *a, uint8_t *mask, uint8_t *gate, int type);
void v6addroute(struct fs *f, char *tag, uint8_t *a, uint8_t *mask, uint8_t *gate, int type);
struct route **looknode(struct route **cur, struct route *r);
void v4delroute(struct fs *f, uint8_t *a, uint8_t *mask, int dolock);
void v6delroute(struct fs *f, uint8_t *a, uint8_t *mask, int dolock);
struct route *v4lookup(struct fs *f, uint8_t *a, struct conv *c);
struct route *v6lookup(struct fs *f, uint8_t *a, struct conv *c);
void routetype(int type, char *p);
void convroute(struct route *r, uint8_t *addr, uint8_t *mask, uint8_t *gate, char *t, int *nifc);
void ipwalkroutes(struct fs *f, struct routewalk *rw);
long routeread(struct fs *f, char *p, uint32_t offset, int n);
void delroute(struct fs *f, struct route *r, int dolock);
int routeflush(struct fs *f, struct route *r, char *tag);
struct route *iproute(struct fs *fs, uint8_t *ip);
long routewrite(struct fs *f, struct chan *c, char *p, int n);
/* kern/src/net/nixip/ipv6.c */
int ipoput6(struct fs *f, struct block *bp, int gating, int ttl, int tos, struct conv *c);
void ipiput6(struct fs *f, struct ipifc *ifc, struct block *bp);
void ipfragfree6(struct IP *ip, struct fragment6 *frag);
struct fragment6 *ipfragallo6(struct IP *ip);
int unfraglen(struct block *bp, uint8_t *nexthdr, int setfh);
struct block *procopts(struct block *bp);
struct block *ip6reassemble(struct IP *ip, int uflen, struct block *bp, struct ip6hdr *ih);
/* kern/src/net/nixip/loopbackmedium.c */
void loopbackmediumlink(void);
/* kern/src/net/nixip/netdevmedium.c */
void netdevmediumlink(void);
/* kern/src/net/nixip/netlog.c */
void netloginit(struct fs *f);
void netlogopen(struct fs *f);
void netlogclose(struct fs *f);
long netlogread(struct fs *f, void *a, uint32_t unused_len, long n);
void netlogctl(struct fs *f, char *s, int n);
void netlog(struct fs *f, int mask, char *fmt, ...);
/* kern/src/net/nixip/nullmedium.c */
void nullmediumlink(void);
/* kern/src/net/nixip/pktmedium.c */
void pktmediumlink(void);
/* kern/src/net/nixip/ptclbsum.c */
uint16_t ptclbsum(uint8_t *addr, int len);
/* kern/src/net/nixip/tcp.c */
void tcpinit(struct fs *fs);
/* kern/src/net/nixip/udp.c */
void udpkick(void *x, struct block *bp);
void udpiput(struct proto *udp, struct ipifc *ifc, struct block *bp);
char *udpctl(struct conv *c, char **f, int n);
void udpadvise(struct proto *udp, struct block *bp, char *msg);
int udpstats(struct proto *udp, char *buf, int len);
void udpinit(struct fs *fs);

/* IP library */

/* kern/src/libip/bo.c */
void hnputv(void *p, uint64_t v);
void hnputl(void *p, unsigned int v);
void hnputs(void *p, uint16_t v);
uint64_t nhgetv(void *p);
unsigned int nhgetl(void *p);
uint16_t nhgets(void *p);
/* kern/src/libip/classmask.c */
uint8_t *defmask(uint8_t *ip);
void maskip(uint8_t *from, uint8_t *mask, uint8_t *to);
/* kern/src/libip/eipfmt.c */
int eipfmt(void *);
/* kern/src/libip/equivip.c */
int equivip4(uint8_t *a, uint8_t *b);
int equivip6(uint8_t *a, uint8_t *b);
/* kern/src/libip/ipaux.c */
int isv4(uint8_t *ip);
void v4tov6(uint8_t *v6, uint8_t *v4);
int v6tov4(uint8_t *v4, uint8_t *v6);
/* kern/src/libip/myetheraddr.c */
int myetheraddr(uint8_t *to, char *dev);
/* kern/src/libip/myipaddr.c */
int myipaddr(uint8_t *ip, char *net);
/* kern/src/libip/parseether.c */
int parseether(uint8_t *to, char *from);
/* kern/src/libip/parseip.c */
char *v4parseip(uint8_t *to, char *from);
int64_t parseip(uint8_t *to, char *from);
int64_t parseipmask(uint8_t *to, char *from);
char *v4parsecidr(uint8_t *addr, uint8_t *mask, char *from);
/* kern/src/libip/ptclbsum.c */
uint16_t ptclbsum(uint8_t *addr, int len);
/* kern/src/libip/readipifc.c */
struct ipifc *readipifc(char *net, struct ipifc *ifc, int index);

#endif /* ROS_KERN_PLAN9_H */
