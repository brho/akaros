// INFERNO

#ifndef ROS_KERN_NS_H
#define ROS_KERN_NS_H

#include <err.h>
#include <rendez.h>
#include <rwlock.h>
#include <linker_func.h>

/*
 * functions (possibly) linked in, complete, from libc.
 */
enum {
	UTFmax = 4,					/* maximum bytes per rune */
	Runesync = 0x80,	/* cannot represent part of a UTF sequence (<) */
	Runeself = 0x80,	/* rune and UTF sequences are the same (<) */
	Runeerror = 0xFFFD,	/* decoding error in UTF */
	Runemax = 0x10FFFF,	/* 21-bit rune */
	Runemask = 0x1FFFFF,	/* bits used by runes (see grep) */
	NUMSIZE32 = 9,	/* max size of formatted 32 bit number */
	NUMSIZE64 = 20,	/* max size of formatted 64 bit number */
};

/*
 * math
 */
extern int isNaN(double);
extern int isInf(double, int);
extern double floor(double);
extern double frexp(double, int *);
extern double pow10(int);

/*
 * one-of-a-kind
 */
extern char *cleanname(char *unused_char_p_t);
//extern    uint32_t    getcallerpc(void*);
static inline uint32_t getcallerpc(void *v)
{
	return 0;
}

extern char etext[];
extern char edata[];
extern char end[];
extern int getfields(char *unused_char_p_t, char **unused_char_pp_t,
					 int unused_int, int, char *);
extern int tokenize(char *unused_char_p_t, char **unused_char_pp_t, int);
extern int dec64(uint8_t * unused_uint8_p_t, int unused_int,
				 char *unused_char_p_t, int);
extern void qsort(void *, long, long, int (*)(void *, void *));

extern int toupper(int);
extern char *netmkaddr(char *unused_char_p_t, char *, char *);
extern int myetheraddr(uint8_t * unused_uint8_p_t, char *unused_char_p_t);
extern int parseether(uint8_t * unused_uint8_p_t, char *unused_char_p_t);

/*
 * network dialling
 */
#define	NETPATHLEN	40

/*
 * Syscall data structures
 */
#define	MORDER	0x0003	/* mask for bits defining order of mounting */
#define	MREPL	0x0000	/* mount replaces object */
#define	MBEFORE	0x0001	/* mount goes before others in union directory */
#define	MAFTER	0x0002	/* mount goes after others in union directory */
#define	MCREATE	0x0004	/* permit creation in mounted directory */
#define	MCACHE	0x0010	/* cache some data */
#define	MMASK	0x0017	/* all bits on */

#define	NCONT	0	/* continue after note */
#define	NDFLT	1	/* terminate after note */
#define	NSAVE	2	/* clear note but hold state */
#define	NRSTR	3	/* restore saved state */

#define	STATMAX	65535U	/* max length of machine-independent stat structure */
#define	ERRMAX			128	/* max length of error string */
#define	KNAMELEN		28	/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80	/* type bit for directories */
#define QTAPPEND	0x40	/* type bit for append only files */
#define QTEXCL		0x20	/* type bit for exclusive use files */
#define QTMOUNT		0x10	/* type bit for mounted channel */
#define QTAUTH		0x08	/* type bit for authentication file */
#define QTFILE		0x01	/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMSYMLINK	0x02000000	/* symlink -- from 9p2000.u */
#define DMREAD		0x4	/* mode bit for read permission */
#define DMWRITE		0x2	/* mode bit for write permission */
#define DMEXEC		0x1	/* mode bit for execute permission */

struct qid {
	uint64_t path;
	uint32_t vers;
	uint8_t type;
};

struct dir {
	/* system-modified data */
	uint16_t type;				/* server type */
	unsigned int dev;			/* server subtype */
	/* file data */
	struct qid qid;				/* unique id from server */
	uint32_t mode;				/* permissions */
	uint32_t atime;				/* last read time */
	uint32_t mtime;				/* last write time */
	int64_t length;				/* file length: see <u.h> */
	char *name;					/* last element of path */
	char *uid;					/* owner name */
	char *gid;					/* group name */
	char *muid;					/* last modifier name */
};

/* Part of the dirty kdirent hack in sysread.  Used to be 59... */
#define MIN_M_BUF_SZ 52			/* TODO: 53 is the smallest i've seen */

struct waitmsg {
	int pid;					/* of loved one */
	uint32_t time[3];			/* of loved one and descendants */
	char msg[ERRMAX];			/* actually variable-size in user mode */
};

#define	VERSION9P	"9P2000"

#define	MAXWELEM	16

typedef
	struct fcall {
	uint8_t type;
	uint32_t fid;
	uint16_t tag;
	/* union { */
	/* struct { */
	uint32_t msize;				/* Tversion, Rversion */
	char *version;				/* Tversion, Rversion */
	/* }; */
	/* struct { */
	uint16_t oldtag;			/* Tflush */
	/* }; */
	/* struct { */
	char *ename;				/* Rerror */
	/* }; */
	/* struct { */
	struct qid qid;				/* Rattach, Ropen, Rcreate */
	uint32_t iounit;			/* Ropen, Rcreate */
	/* }; */
	/* struct { */
	struct qid aqid;			/* Rauth */
	/* }; */
	/* struct { */
	uint32_t afid;				/* Tauth, Tattach */
	char *uname;				/* Tauth, Tattach */
	char *aname;				/* Tauth, Tattach */
	/* }; */
	/* struct { */
	uint32_t perm;				/* Tcreate */
	char *name;					/* Tcreate */
	uint8_t mode;				/* Tcreate, Topen */
	/* }; */
	/* struct { */
	uint32_t newfid;			/* Twalk */
	uint16_t nwname;			/* Twalk */
	char *wname[MAXWELEM];		/* Twalk */
	/* }; */
	/* struct { */
	uint16_t nwqid;				/* Rwalk */
	struct qid wqid[MAXWELEM];	/* Rwalk */
	/* }; */
	/* struct { */
	int64_t offset;				/* Tread, Twrite */
	uint32_t count;				/* Tread, Twrite, Rread */
	char *data;					/* Twrite, Rread */
	/* }; */
	/* struct { */
	uint16_t nstat;				/* Twstat, Rstat */
	uint8_t *stat;				/* Twstat, Rstat */
	/* }; */
	/* }; */
} fcall;

#define	GBIT8(p)	((p)[0])
#define	GBIT16(p)	((p)[0]|((p)[1]<<8))
#define	GBIT32(p)	((uint32_t)((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24)))
#define	GBIT64(p)	((uint32_t)((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24)) |\
				((int64_t)((p)[4]|((p)[5]<<8)|((p)[6]<<16)|((p)[7]<<24)) << 32))

#define	PBIT8(p,v)	(p)[0]=(v)
#define	PBIT16(p,v)	(p)[0]=(v);(p)[1]=(v)>>8
#define	PBIT32(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24
#define	PBIT64(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24;\
			(p)[4]=(v)>>32;(p)[5]=(v)>>40;(p)[6]=(v)>>48;(p)[7]=(v)>>56

#define	BIT8SZ		1
#define	BIT16SZ		2
#define	BIT32SZ		4
#define	BIT64SZ		8
#define	QIDSZ	(BIT8SZ+BIT32SZ+BIT64SZ)

/* STATFIXLEN includes leading 16-bit count */
/* The count, however, excludes itself; total size is BIT16SZ+count */
#define STATFIXLEN	(BIT16SZ+QIDSZ+5*BIT16SZ+4*BIT32SZ+1*BIT64SZ)	/* amount of fixed length data in a stat buffer */

#define	NOTAG		(uint16_t)~0U	/* Dummy tag */
#define	NOFID		(uint32_t)~0U	/* Dummy fid */
#define	IOHDRSZ		24	/* ample room for Twrite/Rread header (iounit) */

enum {
	Tversion = 100,
	Rversion,
	Tauth = 102,
	Rauth,
	Tattach = 104,
	Rattach,
	Terror = 106,	/* illegal */
	Rerror,
	Tflush = 108,
	Rflush,
	Twalk = 110,
	Rwalk,
	Topen = 112,
	Ropen,
	Tcreate = 114,
	Rcreate,
	Tread = 116,
	Rread,
	Twrite = 118,
	Rwrite,
	Tclunk = 120,
	Rclunk,
	Tremove = 122,
	Rremove,
	Tstat = 124,
	Rstat,
	Twstat = 126,
	Rwstat,
	Tmax,
};

unsigned int convM2S(uint8_t * unused_uint8_p_t, unsigned int unused_int,
					 struct fcall *);
unsigned int convS2M(struct fcall *, uint8_t * unused_uint8_p_t, unsigned int);
unsigned int sizeS2M(struct fcall *);

unsigned int convM2kdirent(uint8_t * buf, unsigned int nbuf, struct kdirent *kd,
						   char *strs);
unsigned int convM2kstat(uint8_t * buf, unsigned int nbuf, struct kstat *ks);

int statcheck(uint8_t * abuf, unsigned int nbuf);
unsigned int convM2D(uint8_t * unused_uint8_p_t, unsigned int unused_int,
					 struct dir *, char *unused_char_p_t);
unsigned int convD2M(struct dir *, uint8_t * unused_uint8_p_t, unsigned int);
unsigned int sizeD2M(struct dir *);

int read9pmsg(int unused_int, void *, unsigned int);

struct ref {
	spinlock_t l;
	long ref;
};

struct rept {
	spinlock_t l;
	struct rendez r;
	void *o;
	int t;
	int (*active) (void *);
	int (*ck) (void *, int);
	void (*f) (void *);			/* called with VM acquire()'d */
};

enum {
	Nopin = -1
};

struct talarm {
	spinlock_t lock;
	struct proc *list;
};

struct alarms {
	qlock_t qlock;
	struct proc *head;
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
	CCEXEC = 0x0008,	/* close on exec */
	CFREE = 0x0010,	/* not in use */
	CRCLOSE = 0x0020,	/* remove on close */
	CCACHE = 0x0080,	/* client cache */
	/* file/chan status flags, affected by setfl and reported in getfl */
	CAPPEND = 0x0100,	/* append on write */
};

enum {
	BINTR = (1 << 0),
	BFREE = (1 << 1),
	Bipck = (1 << 2),	/* ip checksum */
	Budpck = (1 << 3),	/* udp checksum */
	Btcpck = (1 << 4),	/* tcp checksum */
	Bpktck = (1 << 5),	/* packet checksum */
};

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

struct chan {
	spinlock_t lock;
	struct kref ref;
	struct chan *next;			/* allocation */
	struct chan *link;
	int64_t offset;				/* in file */
	int type;
	uint32_t dev;
	uint16_t mode;				/* read/write */
	uint16_t flag;
	struct qid qid;
	int fid;					/* for devmnt */
	uint32_t iounit;			/* chunk size for i/o; 0==default */
	struct mhead *umh;			/* mount point that derived Chan; used in unionread */
	struct chan *umc;			/* channel in union; held for union read */
	qlock_t umqlock;			/* serialize unionreads */
	int uri;					/* union read index */
	int dri;					/* devdirread index */
	uint32_t mountid;
	struct mntcache *mcp;		/* Mount cache pointer */
	struct mnt *mux;			/* Mnt for clients using me for messages */
	union {
		void *aux;
		char tag[4];			/* for iproute */
	};
	struct chan *mchan;			/* channel to mounted server */
	struct qid mqid;			/* qid of root of mount point */
	struct cname *name;
};

struct cname {
	struct kref ref;
	int alen;					/* allocated length */
	int len;					/* strlen(s) */
	char *s;
};

struct dev {
	int dc;
	char *name;

	void (*reset) (void);
	void (*init) (void);
	void (*shutdown) (void);
	struct chan *(*attach) (char *muxattach);
	struct walkqid *(*walk) (struct chan *, struct chan *, char **name, int);
	int (*stat) (struct chan *, uint8_t *, int);
	struct chan *(*open) (struct chan *, int);
	void (*create) (struct chan *, char *, int, uint32_t);
	void (*close) (struct chan *);
	long (*read) (struct chan *, void *, long, int64_t);
	struct block *(*bread) (struct chan *, long, uint32_t);
	long (*write) (struct chan *, void *, long, int64_t);
	long (*bwrite) (struct chan *, struct block *, uint32_t);
	void (*remove) (struct chan *);
	int (*wstat) (struct chan *, uint8_t * unused_uint8_p_t, int);
	void (*power) (int);		/* power mgt: power(1) → on, power (0) → off */
//  int (*config)( int unused_int, char *unused_char_p_t, DevConf*);
	char *(*chaninfo) (struct chan *, char *, size_t);
	/* we need to be aligned, i think to 32 bytes, for the linker tables. */
} __attribute__ ((aligned(32)));

struct dirtab {
	char name[KNAMELEN];
	struct qid qid;
	int64_t length;
	long perm;
};

struct walkqid {
	struct chan *clone;
	int nqid;
	struct qid qid[1];
};

enum {
	NSMAX = 1000,
	NSLOG = 7,
	NSCACHE = (1 << NSLOG),
};

struct mntwalk {				/* state for /proc/#/ns */
	int cddone;
	uint32_t id;
	struct mhead *mh;
	struct mount *cm;
};

struct mount {
	uint32_t mountid;
	struct mount *next;
	struct mhead *head;
	struct mount *copy;
	struct mount *order;
	struct chan *to;			/* channel replacing channel */
	int mflag;
	char *spec;
};

struct mhead {
	struct kref ref;
	struct rwlock lock;
	struct chan *from;			/* channel mounted upon */
	struct mount *mount;		/* what's mounted upon it */
	struct mhead *hash;			/* Hash chain */
};

struct mnt {
	spinlock_t lock;
	/* references are counted using c->ref; channels on this mount point incref(c->mchan) == Mnt.c */
	struct chan *c;				/* Channel to file service */
	struct proc *rip;			/* Reader in progress */
	struct mntrpc *queue;		/* Queue of pending requests on this channel */
	uint32_t id;				/* Multiplexer id for channel check */
	struct mnt *list;			/* Free list */
	int flags;					/* cache */
	int msize;					/* data + IOHDRSZ */
	char *version;				/* 9P version */
	struct queue *q;			/* input queue */
};

enum {
	RENDLOG = 5,
	RENDHASH = 1 << RENDLOG,	/* Hash to lookup rendezvous tags */
	MNTLOG = 5,
	MNTHASH = 1 << MNTLOG,	/* Hash to walk mount table */
	DELTAFD = 20,	/* allocation quantum for process file descriptors */
	MAXNFD = 4000,	/* max per process file descriptors */
	MAXKEY = 8,	/* keys for signed modules */
};
#define MOUNTH(p,qid)	((p)->mnthash[(qid).path&((1<<MNTLOG)-1)])

struct mntparam {
	struct chan *chan;
	struct chan *authchan;
	char *spec;
	int flags;
};

struct pgrp {
	struct kref ref;			/* also used as a lock when mounting */
	uint32_t pgrpid;
	qlock_t debug;				/* single access via devproc.c */
	struct rwlock ns;			/* Namespace n read/one write lock */
	qlock_t nsh;
	struct mhead *mnthash[MNTHASH];
	int progmode;
	struct chan *dot;
	struct chan *slash;
	int nodevs;
	int pin;
};

struct fgrp {
	spinlock_t lock;
	struct kref ref;
	struct chan **fd;
	int nfd;					/* number of fd slots */
	int maxfd;					/* highest fd in use */
	int minfd;					/* lower bound on free fd */
	int closed;
};

struct evalue {
	char *var;
	char *val;
	int len;
	struct qid qid;
	struct evalue *next;
};

struct egrp {
	struct kref ref;
	qlock_t qlock;
	struct evalue *entries;
	uint32_t path;				/* qid.path of next Evalue to be allocated */
	uint32_t vers;				/* of Egrp */
};

struct signerkey {
	struct kref ref;
	char *owner;
	uint16_t footprint;
	uint32_t expires;
	void *alg;
	void *pk;
	void (*pkfree) (void *);
};

struct skeyset {
	struct kref ref;
	qlock_t qlock;
	uint32_t flags;
	char *devs;
	int nkey;
	struct signerkey *keys[MAXKEY];
};

/*
 * fasttick timer interrupts
 */
enum {
	/* Mode */
	Trelative,					/* timer programmed in ns from now */
	Tabsolute,					/* timer programmed in ns since epoch */
	Tperiodic,					/* periodic timer, period in ns */
};

enum {
	PRINTSIZE = 256,
	NUMSIZE = 12,	/* size of formatted number */
	MB = (1024 * 1024),
	READSTR = 1000,	/* temporary buffer size for device reads */
};

extern struct dev devtab[];
extern struct dev __devtabend[];

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

#define DEVDOTDOT -1

typedef int Devgen(struct chan *, char *unused_char_p_t, struct dirtab *,
				   int unused_int, int, struct dir *);

/* inferno portfns.h. Not all these are needed. */
// INFERNO
#define		FPinit() fpinit()	/* remove this if math lib is linked */
void FPrestore(void *);
void FPsave(void *);
struct cname *addelem(struct cname *, char *unused_char_p_t);
void addprog(struct proc *);
void addrootfile(char *unused_char_p_t, uint8_t * unused_uint8_p_t, uint32_t);
struct block *adjustblock(struct block *, int);
struct block *allocb(int);
int anyhigher(void);
int anyready(void);
void _assert(char *unused_char_p_t);
struct block *bl2mem(uint8_t * unused_uint8_p_t, struct block *, int);
int blocklen(struct block *);
char *channame(struct chan *);
void cclose(struct chan *);
void chandevinit(void);
void chandevreset(void);
void chandevshutdown(void);
struct dir *chandirstat(struct chan *);
void chanfree(struct chan *);
void chanrec(struct mnt *);
void checkalarms(void);
void checkb(struct block *, char *unused_char_p_t);
void cinit(void);
struct chan *cclone(struct chan *);
void cclose(struct chan *);
void closeegrp(struct egrp *);
void closefgrp(struct fgrp *);
void closemount(struct mount *);
void closepgrp(struct pgrp *);
void closesigs(struct skeyset *);
void cmderror(struct cmdbuf *, char *unused_char_p_t);
struct mhead *newmhead(struct chan *from);
int cmount(struct chan *, struct chan *, int unused_int, char *unused_char_p_t);
void cnameclose(struct cname *);
struct block *concatblock(struct block *);
void confinit(void);
void copen(struct chan *);
struct block *copyblock(struct block *, int);
int cread(struct chan *, uint8_t * unused_uint8_p_t, int unused_int, int64_t);
struct chan *cunique(struct chan *);
struct chan *createdir(struct chan *, struct mhead *);
void cunmount(struct chan *, struct chan *);
void cupdate(struct chan *, uint8_t * unused_uint8_p_t, int unused_int,
			 int64_t);
void cursorenable(void);
void cursordisable(void);
int cursoron(int);
void cursoroff(int);
void cwrite(struct chan *, uint8_t * unused_uint8_p_t, int unused_int, int64_t);
struct chan *devattach(int unused_int, char *unused_char_p_t);
struct block *devbread(struct chan *, long, uint32_t);
long devbwrite(struct chan *, struct block *, uint32_t);
struct chan *devclone(struct chan *);
void devcreate(struct chan *, char *name, int mode, uint32_t perm);
void devdir(struct chan *, struct qid, char *, int64_t, char *, long,
			struct dir *);
long devdirread(struct chan *, char *, long, struct dirtab *, int, Devgen *);
Devgen devgen;
void devinit(void);
int devno(int unused_int, int);
void devpower(int);
struct dev *devbyname(char *unused_char_p_t);
struct chan *devopen(struct chan *, int unused_int,
					 struct dirtab *, int unused_int2, Devgen *);
void devpermcheck(char *unused_char_p_t, uint32_t, int);
void devremove(struct chan *);
void devreset(void);
void devshutdown(void);
int devstat(struct chan *, uint8_t * unused_uint8_p_t, int unused_int,
			struct dirtab *, int unused_int2, Devgen *);
struct walkqid *devwalk(struct chan *,
						struct chan *, char **unused_char_pp_t, int unused_int,
						struct dirtab *, int unused_intw, Devgen *);
int devwstat(struct chan *, uint8_t * unused_uint8_p_t, int);
char *devchaninfo(struct chan *chan, char *ret, size_t ret_l);
void disinit(void *);
void disfault(void *, char *unused_char_p_t);
int domount(struct chan **, struct mhead **);
void drawactive(int);
void drawcmap(void);
void dumpstack(void);
struct fgrp *dupfgrp(struct proc *, struct fgrp *);
void egrpcpy(struct egrp *, struct egrp *);
int emptystr(char *unused_char_p_t);
int eqchan(struct chan *, struct chan *, int);
int eqqid(struct qid, struct qid);

void errstr(char *unused_char_p_t, int);
void excinit(void);
void exit(int);
void reboot(void);
void halt(void);
int export(int unused_int, char *unused_char_p_t, int);
uint64_t fastticks(uint64_t *);
uint64_t fastticks2ns(uint64_t);
void fdclose(struct fgrp *, int);
struct chan *fdtochan(struct fgrp *, int unused_int, int, int, int);
int findmount(struct chan **, struct mhead **, int unused_int, int, struct qid);
void free(void *);
void freeb(struct block *);
void freeblist(struct block *);
void freeskey(struct signerkey *);
void getcolor(uint32_t, uint32_t *, uint32_t *, uint32_t *);
uint32_t getmalloctag(void *);
uint32_t getrealloctag(void *);
void hnputl(void *, uint32_t);
void hnputs(void *, uint16_t);
struct block *iallocb(int);
void iallocsummary(void);
void ilock(spinlock_t *);
int iprint(char *unused_char_p_t, ...);
void isdir(struct chan *);
int islo(void);
void iunlock(spinlock_t *);
void ixsummary(void);
void kbdclock(void);
int kbdcr2nl(struct queue *, int);
int kbdputc(struct queue *, int);
void kbdrepeat(int);
void kproc(char *unused_char_p_t, void (*)(void *), void *, int);
int fgrpclose(struct fgrp *, int);
void kprocchild(struct proc *, void (*)(void *), void *);
void (*kproftick) (uint32_t);
void ksetenv(char *unused_char_p_t, char *, int);
//void      kstrncpy( char *unused_char_p_t, char*, int unused_int, sizeof(char*, char*));
void kstrdup(char **unused_char_pp_t, char *unused_char_p_t);

struct cmdtab *lookupcmd(struct cmdbuf *, struct cmdtab *, int);
struct block *mem2bl(uint8_t * unused_uint8_p_t, int);
int memusehigh(void);
void microdelay(int);
uint64_t mk64fract(uint64_t, uint64_t);
void mkqid(struct qid *, int64_t, uint32_t, int);
void modinit(void);
struct chan *mntauth(struct chan *, char *unused_char_p_t);
long mntversion(struct chan *, char *unused_char_p_t, int unused_int, int);
void mountfree(struct mount *);
void mousetrack(int unused_int, int, int, int);
uint64_t ms2fastticks(uint32_t);
void mul64fract(uint64_t *, uint64_t, uint64_t);
void muxclose(struct mnt *);
struct chan *namec(char *unused_char_p_t, int unused_int, int, uint32_t);
struct chan *newchan(void);
struct egrp *newegrp(void);
struct fgrp *newfgrp(void);
struct mount *newmount(struct mhead *, struct chan *, int unused_int,
					   char *unused_char_p_t);
struct pgrp *newpgrp(void);
struct proc *newproc(void);
char *nextelem(char *unused_char_p_t, char *);

struct cname *newcname(char *unused_char_p_t);
void notkilled(void);
int nrand(int);
uint64_t ns2fastticks(uint64_t);
int okaddr(uint32_t, uint32_t, int);
int openmode(uint32_t);
struct block *packblock(struct block *);
struct block *padblock(struct block *, int);

struct cmdbuf *parsecmd(char *unused_char_p_t, int);

void pgrpcpy(struct pgrp *, struct pgrp *);

int progfdprint(struct chan *, int unused_int, int, char *unused_char_p_t,
				int i);
int pullblock(struct block **, int);
struct block *pullupblock(struct block *, int);
struct block *pullupqueue(struct queue *, int);
void putmhead(struct mhead *);
void putstrn(char *unused_char_p_t, int);
void qaddlist(struct queue *, struct block *);
struct block *qbread(struct queue *, int);
long qbwrite(struct queue *, struct block *);
struct queue *qbypass(void (*)(void *, struct block *), void *);
int qcanread(struct queue *);
void qclose(struct queue *);
int qconsume(struct queue *, void *, int);
struct block *qcopy(struct queue *, int unused_int, uint32_t);
int qdiscard(struct queue *, int);
void qflush(struct queue *);
void qfree(struct queue *);
int qfull(struct queue *);
struct block *qget(struct queue *);
void qhangup(struct queue *, char *unused_char_p_t);
int qisclosed(struct queue *);
int qiwrite(struct queue *, void *, int);
int qlen(struct queue *);
void qnoblock(struct queue *, int);
struct queue *qopen(int unused_int, int, void (*)(void *), void *);
int qpass(struct queue *, struct block *);
int qpassnolim(struct queue *, struct block *);
int qproduce(struct queue *, void *, int);
void qputback(struct queue *, struct block *);
long qread(struct queue *, void *, int);
struct block *qremove(struct queue *);
void qreopen(struct queue *);
void qsetlimit(struct queue *, int);
int qwindow(struct queue *);
int qwrite(struct queue *, void *, int);
void randominit(void);
uint32_t randomread(void *, uint32_t);
void *realloc(void *, uint32_t);
int readmem(unsigned long offset, char *buf, unsigned long n,
			void *mem, size_t mem_len);
int readnum(unsigned long off, char *buf, unsigned long n, unsigned long val,
			size_t size);
int readstr(unsigned long offset, char *buf, unsigned long n, char *str);
int readnum_int64_t(uint32_t, char *unused_char_p_t, uint32_t, int64_t, int);
void ready(struct proc *);
void renameproguser(char *unused_char_p_t, char *);
void renameuser(char *unused_char_p_t, char *);
void resrcwait(char *unused_char_p_t);
struct proc *runproc(void);
long seconds(void);
void (*serwrite) (char *unused_char_p_t, int);
int setcolor(uint32_t, uint32_t, uint32_t, uint32_t);

void setmalloctag(void *, uint32_t);
int setpri(int);
void setrealloctag(void *, uint32_t);
char *skipslash(char *unused_char_p_t);
void *smalloc(uint32_t);
int splhi(void);
int spllo(void);
void splx(int);
void splxpc(int);
void swiproc(struct proc *, int);
uint32_t _tas(uint32_t *);
uint32_t tk2ms(uint32_t);
#define		TK2MS(x) ((x)*(1000/HZ))
uint64_t tod2fastticks(int64_t);
int64_t todget(int64_t *);
void todfix(void);
void todsetfreq(int64_t);
void todinit(void);
void todset(int64_t, int64_t, int);
int tready(void *);
struct block *trimblock(struct block *, int unused_int, int);
int uartgetc(void);
void uartputc(int);
void uartputs(char *unused_char_p_t, int);
long unionread(struct chan *, void *, long);
void unlock(spinlock_t *);
void userinit(void);
uint32_t userpc(void);
void validname(char *unused_char_p_t, int);
void validstat(uint8_t * unused_uint8_p_t, int);
void validwstatname(char *unused_char_p_t);
int walk(struct chan **, char **unused_char_pp_t, int unused_int, int, int *);
void werrstr(char *unused_char_p_t, ...);
void *xalloc(uint32_t);
void *xallocz(uint32_t, int);
void xfree(void *);
void xhole(uint32_t, uint32_t);
void xinit(void);
int xmerge(void *, void *);
void *xspanalloc(uint32_t, int unused_int, uint32_t);
void xsummary(void);

void validaddr(void *, uint32_t, int);
void *vmemchr(void *, int unused_int, int);
void hnputv(void *, int64_t);
void hnputl(void *, uint32_t);
void hnputs(void *, uint16_t);
int64_t nhgetv(void *);
uint32_t nhgetl(void *);
uint16_t nhgets(void *);

/* error messages, from inferno emu error.h */
extern char Enoerror[];			/* no error */
extern char Emount[];			/* inconsistent mount */
extern char Eunmount[];			/* not mounted */
extern char Eunion[];			/* not in union */
extern char Emountrpc[];		/* mount rpc error */
extern char Eshutdown[];		/* mounted device shut down */
extern char Eowner[];			/* not owner */
extern char Eunknown[];			/* unknown user or group id */
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
extern char Enetaddr[];			/* bad network address */
extern char Emsgsize[];			/* message is too big for protocol */
extern char Enetbusy[];			/* network device is busy or allocated */
extern char Enoproto[];			/* network protocol not supported */
extern char Enoport[];			/* network port not available */
extern char Enoifc[];			/* bad interface or no free interface slots */
extern char Enolisten[];		/* not announced */
extern char Ehungup[];			/* i/o on hungup channel */
extern char Ebadctl[];			/* bad process or channel control request */
extern char Enodev[];			/* no free devices */
extern char Enoenv[];			/* no free environment resources */
extern char Ethread[];			/* thread exited */
extern char Enochild[];			/* no living children */
extern char Eioload[];			/* i/o error in demand load */
extern char Enovmem[];			/* out of memory: virtual memory */
extern char Ebadld[];			/* illegal line discipline */
extern char Ebadfd[];			/* fd out of range or not open */
extern char Eisstream[];		/* seek on a stream */
extern char Ebadexec[];			/* exec header invalid */
extern char Etimedout[];		/* connection timed out */
extern char Econrefused[];		/* connection refused */
extern char Econinuse[];		/* connection in use */
extern char Enetunreach[];		/* network unreachable */
extern char Eintr[];			/* interrupted */
extern char Enomem[];			/* out of memory: kernel */
extern char Esfnotcached[];		/* subfont not cached */
extern char Esoverlap[];		/* segments overlap */
extern char Emouseset[];		/* mouse type already set */
extern char Eshort[];			/* i/o count too small */
extern char Enobitstore[];		/* out of screen memory */
extern char Egreg[];			/* jim'll fix it */
extern char Ebadspec[];			/* bad attach specifier */
extern char Estopped[];			/* thread must be stopped */
extern char Enoattach[];		/* mount/attach disallowed */
extern char Eshortstat[];		/* stat buffer too small */
extern char Enegoff[];			/* negative i/o offset */
extern char Ebadstat[];			/* malformed stat buffer */
extern char Ecmdargs[];			/* wrong #args in control message */
extern char Enofd[];			/* no free file descriptors */
extern char Enoctl[];			/* unknown control request */
extern char Eprocdied[];		/* process died */

/* kern/src/err.c */
int errpush(struct errbuf *errstack, int stacksize, int *curindex,
			struct errbuf **prev_errbuf);
void errpop(struct errbuf *errstack, int stacksize, int *curindex,
			struct errbuf *prev_errbuf);
/* */
char *get_cur_genbuf(void);

/* hack for now. */
#define	NOW	tsc2msec(read_tsc())
#define	seconds() tsc2sec(read_tsc())
#define	milliseconds() tsc2msec(read_tsc())

/* kern/drivers/dev/tab.c */
void devtabinit();
void devtabreset();

/* kern/src/ns/parse.c */
struct cmdbuf *parsecmd(char *p, int n);
void cmderror(struct cmdbuf *cb, char *s);
struct cmdtab *lookupcmd(struct cmdbuf *cb, struct cmdtab *ctab, int nctab);

/* kern/src/ns/sysfile.c */
int newfd(struct chan *c);
struct chan *fdtochan(struct fgrp *f, int fd, int mode, int chkmnt, int iref);
long kchanio(void *vc, void *buf, int n, int mode);
int openmode(uint32_t o);
void fdclose(struct fgrp *f, int fd);
int syschdir(char *path);
int grpclose(struct fgrp *f, int fd);
int sysclose(int fd);
int syscreate(char *path, int mode, uint32_t perm);
int sysdup(int old, int new);
int sysfstat(int fd, uint8_t*, int n);
int sysfstatakaros(int fd, struct kstat *);
char *sysfd2path(int fd);
int sysfauth(int fd, char *aname);
int sysfversion(int fd, unsigned int msize, char *vers, unsigned int arglen);
int syspipe(int fd[2]);
int sysfwstat(int fd, uint8_t * buf, int n);
long bindmount(struct chan *c, char *old, int flag, char *spec);
int sysbind(char *new, char *old, int flags);
int sysmount(int fd, int afd, char *old, int flags, char *spec);
int sysunmount(char *old, char *new);
int sysopen(char *path, int mode);
long unionread(struct chan *c, void *va, long n);
long sysread(int fd, void *va, long n);
long syspread(int fd, void *va, long n, int64_t off);
int sysremove(char *path);
int64_t sysseek(int fd, int64_t off, int whence);
void validstat(uint8_t * s, int n);
int sysstat(char *path, uint8_t*, int n);
int sysstatakaros(char *path, struct kstat *);
long syswrite(int fd, void *va, long n);
long syspwrite(int fd, void *va, long n, int64_t off);
int syswstat(char *path, uint8_t * buf, int n);
struct dir *chandirstat(struct chan *c);
struct dir *sysdirstat(char *name);
struct dir *sysdirfstat(int fd);
int sysdirwstat(char *name, struct dir *dir);
int sysdirfwstat(int fd, struct dir *dir);
long sysdirread(int fd, struct kdirent **d);
int sysiounit(int fd);
void close_9ns_files(struct proc *p, bool only_cloexec);
void print_chaninfo(struct chan *ch);
void print_9ns_files(struct proc *p);
int plan9setup(struct proc *new_proc, struct proc *parent);
int iseve(void);
int fd_getfl(int fd);
int fd_setfl(int fd, int flags);

/* kern/drivers/dev/srv.c */
char *srvname(struct chan *c);

static inline int abs(int a)
{
	if (a < 0)
		return -a;
	return a;
}

extern char *eve;
extern unsigned int qiomaxatomic;

/* special sections */
#define __devtab  __attribute__((__section__(".devtab")))

#endif /* ROS_KERN_NS_H */
