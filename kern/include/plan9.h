/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
#include <setjmp.h>
#include <atomic.h>


/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */
#define KiB		1024u			/* Kibi 0x0000000000000400 */
#define MiB		1048576u		/* Mebi 0x0000000000100000 */
#define GiB		1073741824u		/* Gibi 000000000040000000 */
#define TiB		1099511627776ull	/* Tebi 0x0000010000000000 */
#define PiB		1125899906842624ull	/* Pebi 0x0004000000000000 */
#define EiB		1152921504606846976ull	/* Exbi 0x1000000000000000 */

#define ALIGNED(p, a)	(!(((uintptr)(p)) & ((a)-1)))

/* UTF support is removed. 
 */
enum
{
	UTFmax		= 3,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
};

/* stuff which has no equal. */
#define DEVDOTDOT -1

#define	MORDER	0x0003	/* mask for bits defining order of mounting */
#define	MREPL	0x0000	/* mount replaces object */
#define	MBEFORE	0x0001	/* mount goes before others in union directory */
#define	MAFTER	0x0002	/* mount goes after others in union directory */
#define	MCREATE	0x0004	/* permit creation in mounted directory */
#define	MCACHE	0x0010	/* cache some data */
#define	MMASK	0x0017	/* all bits on */

#define	OREAD	0	/* open for read */
#define	OWRITE	1	/* write */
#define	ORDWR	2	/* read and write */
#define	OEXEC	3	/* execute, == read but check execute permission */
#define	OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define	OCEXEC	32	/* or'ed in, close on exec */
#define	ORCLOSE	64	/* or'ed in, remove on close */
#define OEXCL   0x1000	/* or'ed in, exclusive create */

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

#define ERRMAX		128		/* max length of error string */
#define KNAMELEN	28		/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTAUTH		0x08		/* type bit for authentication file */
#define QTFILE		0x00		/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMREAD		0x4		/* mode bit for read permission */
#define DMWRITE		0x2		/* mode bit for write permission */
#define DMEXEC		0x1		/* mode bit for execute permission */

enum
{
	DELTAFD	= 20		/* incremental increase in Fgrp.fd's */
};

/*
 * Access types in namec & channel flags
 */
enum
{
	Aaccess,			/* as in stat, wstat */
	Abind,				/* for left-hand-side of bind */
	Atodir,				/* as in chdir */
	Aopen,				/* for i/o */
	Amount,				/* to be mounted or mounted upon */
	Acreate,			/* is to be created */
	Aremove,			/* will be removed by caller */

	COPEN	= 0x0001,		/* for i/o */
	CMSG	= 0x0002,		/* the message channel for a mount */
/*rsc	CCREATE	= 0x0004,		* permits creation if c->mnt */
	CCEXEC	= 0x0008,		/* close on exec */
	CFREE	= 0x0010,		/* not in use */
	CRCLOSE	= 0x0020,		/* remove on close */
	CCACHE	= 0x0080,		/* client cache */
};

/* struct block flag values */
enum
{
    BINTR	=	(1<<0), /* allocated in interrupt mode. */
    
    Bipck	=	(1<<2),		/* ip checksum */
    Budpck	=	(1<<3),		/* udp checksum */
    Btcpck	=	(1<<4),		/* tcp checksum */
    Bpktck	=	(1<<5),		/* packet checksum */
};



struct path path;
struct chan chan;

/* Plan 9 Block. Not used, may never be used, but who knows? */
struct block
{
	struct block*	next;
	struct block*	list;
	uint8_t *	rp;			/* first unconsumed byte */
	uint8_t *	wp;			/* first empty byte */
	uint8_t *	lim;			/* 1 past the end of the buffer */
	uint8_t *	base;			/* start of the buffer */
	void	(*free)(struct block*);
	uint16_t	flag;
	uint16_t	checksum;		/* IP checksum of complete packet (minus media header) */
};
#define BLEN(s)	((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base)

/* linux has qstr, plan 9 has path. Path *might* need to go away
 * but there's a lot of fish to try here; maybe qstr should go away
 * instead? Let's not fret just now. The plan 9 one is a tad more
 * capable. 
 */
struct path
{
    struct kref ref;
    char*	s;
    struct chan**	mtpt;			/* mtpt history */
    int	len;			/* strlen(s) */
    int	alen;			/* allocated length of s */
    int	mlen;			/* number of path elements */
    int	malen;			/* allocated length of mtpt */
};


struct qid
{
    uint64_t  path;
    unsigned long   vers;
    uint8_t   type;
};

struct walkqid
{
    struct chan	*clone;
    int	nqid;
    struct qid	qid[0];
};

struct dir {
	/* system-modified data */
	uint16_t	type;	/* server type */
	unsigned int	dev;	/* server subtype */
	/* file data */
	struct qid	qid;	/* unique id from server */
	unsigned long	mode;	/* permissions */
	unsigned long	atime;	/* last read time */
	unsigned long	mtime;	/* last write time */
	unsigned long long	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
};


struct dirtab
{
    char	name[128];
    struct qid	qid;
    unsigned long long	length;
    long	perm;
};

struct errbuf {
	struct jmpbuf jmp_buf;
};

struct dev
{
    int	dc;
    char*	name;

    void	(*reset)(struct proc *up);
    void	(*init)(struct proc *up);
    void	(*shutdown)(struct proc *up);
    struct chan*	(*attach)(struct proc *up, char*path, struct errbuf *e);
    struct walkqid*(*walk)(struct proc *up, struct chan*from, struct chan*to, char**paths, int npath, struct errbuf *e);
    long	(*stat)(struct chan*dir, uint8_t*path, long l, struct errbuf *e);
    struct chan*	(*open)(struct proc *up, struct chan*file, int mode, struct errbuf *e);
    void	(*create)(struct proc *up, struct chan*dir, char*path, int mode, int perm, struct errbuf *e);
    void	(*close)(struct chan*chan, struct errbuf*e);
    long	(*read)(struct chan*chan, void*p, long len, int64_t off, struct errbuf *e);
    struct block *(*bread)(struct chan*chan, long len, int64_t off, struct errbuf *e);
    long	(*write)(struct chan*chan, void*p, long len, int64_t off, struct errbuf *e);
    long	(*bwrite)(struct chan*chan, struct block *b, int64_t off, struct errbuf *e);
    void	(*remove)(struct chan*chan, struct errbuf *e);
    long	(*wstat)(struct proc *up, struct chan*chan, uint8_t*new, long size, struct errbuf *e);
  void	(*power)(struct proc *up, int control);	/* power mgt: power(1) => on, power (0) => off */
    int	(*config)(int opts, char*command, void* conf);	/* returns 0 on error */
};
enum
{
	NSMAX	=	1000,
	NSLOG	=	7,
	NSCACHE	=	(1<<NSLOG),
};

struct mntwalk				/* state for /proc/#/ns */
{
	int	cddone;
	struct head*	mh;
	struct mount*	cm;
};

struct mount
{
    int mountid;
    struct mount*	next;
    struct mhead*	head;
    struct mount*	copy;
    struct mount*	order;
    struct chan*	to;			/* channel replacing channel (mounted on in linux parlance) */
    int	mflag;
    char	*spec;
};

struct mhead
{
    struct kref ref;
    spinlock_t	lock;
    struct chan*	from;			/* channel mounted upon */
    struct mount*	mount;			/* what's mounted upon it */
    struct mhead*	hash;			/* Hash chain */
};

/* pgrps are only used at this point to share name spaces.
 */
enum
{
	MNTLOG	=	5,
	MNTHASH =	1<<MNTLOG,	/* Hash to walk mount table */
	NFD =		100,		/* per process file descriptors */
};

#define MOUNTH(p,qid)	((p)->mnthash[(qid).path&((1<<MNTLOG)-1)])

struct pgrp
{
  struct kref ref;	/* also used as a lock when mounting */
	int	noattach;
	unsigned long	pgrpid;
  spinlock_t	debug;			/* single access via devproc.c */
  // not supported yet.	RWlock	ns;	/* Namespace n read/one write lock */
  spinlock_t	ns;			/* Namespace n read/one write lock */
	struct mhead	*mnthash[MNTHASH];
};

struct chan
{
    struct kref					ref;
    spinlock_t					lock;
    struct chan*	next;			/* allocation */
    struct chan*	link;
    int64_t	offset;			/* in fd */
    int64_t	devoffset;		/* in underlying device; see read */
    struct dev*	dev;
    unsigned int	devno;
    uint16_t	mode;			/* read/write */
    uint16_t	flag;
    struct qid	qid;
    int	fid;			/* for devmnt */
    unsigned long	iounit;			/* chunk size for i/o; 0==default */
    struct mhead*	umh;			/* mount point that derived struct chan; used in unionread */
    struct chan*	umc;			/* channel in union; held for union read */
    // need a queued lock not a spin lock. QLock	umqlock;		/* serialize unionreads */
    spinlock_t umqlock;
    int	uri;			/* union read index */
    int	dri;			/* devdirread index */
    unsigned char*	dirrock;		/* directory entry rock for translations */
    int	nrock;
    int	mrock;
    spinlock_t	rockqlock;

    int	ismtpt;
  int mc;
  //Mntcache*mc;			/* Mount cache pointer */
  struct mnt*	mux;			/* Mnt for clients using me for messages */
    union {
	void*	aux;
	struct qid	pgrpid;		/* for #p/notepg */
	unsigned long	mid;		/* for ns in devproc */
    };
    struct chan*	mchan;			/* channel to mounted server */
    struct qid	mqid;			/* qid of root of mount point */
    struct path	*path;
};

struct fgrp
{
    spinlock_t lock;
    int mountid;
    struct kref					ref;
    struct chan	**fd;
    int	nfd;			/* number allocated */
    int	maxfd;			/* highest fd in use */
    int	exceed;			/* debugging */
};

typedef int    devgen_t(struct chan*c, char*name, struct dirtab*dirtab, int, int, struct dir*);

extern char Enoerror[];		/* no error */
extern char Emount[];		/* inconsistent mount */
extern char Eunmount[];		/* not mounted */
extern char Eismtpt[];		/* is a mount point */
extern char Eunion[];		/* not in union */
extern char Emountrpc[];	/* mount rpc error */
extern char Eshutdown[];	/* device shut down */
extern char Enocreate[];	/* mounted directory forbids creation */
extern char Enonexist[];	/* file does not exist */
extern char Eexist[];		/* file already exists */
extern char Ebadsharp[];	/* unknown device in # filename */
extern char Enotdir[];		/* not a directory */
extern char Eisdir[];		/* file is a directory */
extern char Ebadchar[];		/* bad character in file name */
extern char Efilename[];	/* file name syntax */
extern char Eperm[];		/* permission denied */
extern char Ebadusefd[];	/* inappropriate use of fd */
extern char Ebadarg[];		/* bad arg in system call */
extern char Einuse[];		/* device or object already in use */
extern char Eio[];		/* i/o error */
extern char Etoobig[];		/* read or write too large */
extern char Etoosmall[];	/* read or write too small */
extern char Enoport[];		/* network port not available */
extern char Ehungup[];		/* i/o on hungup channel */
extern char Ebadctl[];		/* bad process or channel control request */
extern char Enodev[];		/* no free devices */
extern char Eprocdied[];	/* process exited */
extern char Enochild[];		/* no living children */
extern char Eioload[];		/* i/o error in demand load */
extern char Enovmem[];		/* virtual memory allocation failed */
extern char Ebadfd[];		/* fd out of range or not open */
extern char Enofd[];		/* no free file descriptors */
extern char Eisstream[];	/* seek on a stream */
extern char Ebadexec[];		/* exec header invalid */
extern char Etimedout[];	/* connection timed out */
extern char Econrefused[];	/* connection refused */
extern char Econinuse[];	/* connection in use */
extern char Eintr[];		/* interrupted */
extern char Enomem[];		/* kernel allocate failed */
extern char Enoswap[];		/* swap space full */
extern char Esoverlap[];	/* segments overlap */
extern char Eshort[];		/* i/o count too small */
extern char Egreg[];		/* ken has left the building */
extern char Ebadspec[];		/* bad attach specifier */
extern char Enoreg[];		/* process has no saved registers */
extern char Enoattach[];	/* mount/attach disallowed */
extern char Eshortstat[];	/* stat buffer too small */
extern char Ebadstat[];		/* malformed stat buffer */
extern char Enegoff[];		/* negative i/o offset */
extern char Ecmdargs[];		/* wrong #args in control message */
extern char Ebadip[];		/* bad ip address syntax */
extern char Edirseek[];		/* seek in directory */

#define PERRBUF struct errbuf *perrbuf = NULL;
#define nel(x) (sizeof(x)/sizeof(x[0]))
#define ERRSTACK(x) struct errbuf errstack[(x)]; int curindex = 0;
#define waserror() (pusherror(errstack, nel(errstack), &curindex, &perrbuf) || setjmp(&(perrbuf->jmp_buf)))
#define error(x) {set_errstr(x); longjmp(&perrbuf->jmp_buf, (void *)x);}
#define nexterror() {poperror(errstack, nel(errstack), &curindex, &perrbuf); longjmp(&perrbuf->jmp_buf, (void *)1);}

/* this would be useful at some point ... */
static inline uintptr_t getcallerpc(void *unused)
{
    return 0;
}

void kstrcpy(char *s, char *t, int ns);

/* kern/src/chan.c */
int findmount(struct proc *up, struct chan **cp, struct mhead **mp, int dc, unsigned int devno, struct qid qid, struct errbuf *perrbuf);
int eqchanddq(struct chan *c, int dc, unsigned int devno, struct qid qid, int skipvers, struct errbuf *perrbuf);
char *chanpath(struct chan *c, struct errbuf *perrbuf);
int isdotdot(char *p, struct errbuf *perrbuf);
int emptystr(char *s, struct errbuf *perrbuf);
void kstrdup(char **p, char *s, struct errbuf *perrbuf);
struct chan *newchan(void);
struct path *newpath(char *s, struct errbuf *perrbuf);
void pathclose(struct path *p, struct errbuf *perrbuf);
void chanfree(struct chan *c, struct errbuf *perrbuf);
void cclose(struct chan *c, struct errbuf *perrbuf);
void ccloseq(struct chan *c, struct errbuf *perrbuf);
struct chan *cunique(struct proc *up, struct chan *c, struct errbuf *perrbuf);
int eqqid(struct qid a, struct qid b, struct errbuf *perrbuf);
struct mhead *newmhead(struct chan *from, struct errbuf *perrbuf);
int cmount(struct proc *up, struct chan **newp, struct chan *old, int flag, char *spec, struct errbuf *perrbuf);
void cunmount(struct proc *up, struct chan *mnt, struct chan *mounted, struct errbuf *perrbuf);
struct chan *cclone(struct proc *up, struct chan *c, struct errbuf *perrbuf);
int walk(struct proc *up, struct chan **cp, char **names, int nnames, int nomount, int *nerror, struct errbuf *perrbuf);
struct chan *createdir(struct proc *up, struct chan *c, struct mhead *mh, struct errbuf *perrbuf);
void nameerror(struct proc *up, char *name, char *err);
struct chan *namec(struct proc *up, char *aname, int amode, int omode, int perm, struct errbuf *perrbuf);
char *skipslash(char *name);
void validname(char *aname, int slashok, struct errbuf *perrbuf);
char *validnamedup(char *aname, int slashok, struct errbuf *perrbuf);
void isdir(struct chan *c, struct errbuf *perrbuf);
void putmhead(struct mhead *mh, struct errbuf *perrbuf);

/* plan9.c */
char*validname0(char *aname, int slashok, int dup, uintptr_t pc, struct errbuf *perrbuf);

/* cleanname.c */
char * cleanname(char *name);

/* kern/src/pgrp.c */
struct fgrp*dupfgrp(struct fgrp *f, struct errbuf *perrbuf);
void pgrpinsert(struct mount **order, struct mount *m, struct errbuf *perrbuf);
void forceclosefgrp(struct proc *up, struct errbuf *perrbuf);
struct mount *newmount(struct mhead *mh, struct chan *to, int flag, char *spec, struct errbuf *perrbuf);
void mountfree(struct mount *m, struct errbuf *perrbuf);
void resrcwait(struct proc *up, char *reason, struct errbuf *perrbuf);
struct pgrp *newpgrp(void);


/* kern/src/devtab.c */
void devtabreset(struct proc *up);
void devtabinit(struct proc *up);
void devtabshutdown(struct proc *up);
struct dev *devtabget(int dc, int user, struct errbuf *perrbuf);
long devtabread(struct proc *up, struct chan *, void *buf, long n, int64_t off, struct errbuf *perrbuf);

/* kern/src/allocplan9block.c */
struct block *allocb(int size);
struct block *iallocb(int size);
void freeb(struct block *b);
void checkb(struct block *b, char *msg);
void iallocsummary(void);

/* kern/src/dev.c */
int devgen(struct chan *c, char *name, struct dirtab *tab, int ntab, int i, struct dir *dp);
void mkqid(struct qid *q, int64_t path, uint32_t vers, int type, struct errbuf *perrbuf);
void devdir(struct chan *c, struct qid qid, char *n, int64_t length, char *user, long perm, struct dir *db);
void devreset(struct proc *up);
void devinit(struct proc *up);
void devshutdown(struct proc *up);
struct chan *devattach(int dc, char *spec, struct errbuf *perrbuf);
struct chan *devclone(struct chan *c, struct errbuf *perrbuf);
struct walkqid *devwalk(struct proc *up, struct chan *c, struct chan *nc, char **name, int nname, struct dirtab *tab, int ntab, devgen_t *gen, struct errbuf *perrbuf);
long devstat(struct chan *c, uint8_t *db, long n, struct dirtab *tab, int ntab, devgen_t *gen, struct errbuf *perrbuf);
long devdirread(struct chan *c, char *d, long n, struct dirtab *tab, int ntab, devgen_t *gen, struct errbuf *perrbuf);
void devpermcheck(struct proc *up, char *fileuid, int perm, int omode, struct errbuf *perrbuf);
struct chan *devopen(struct proc *up, struct chan *c, int omode, struct dirtab *tab, int ntab, devgen_t *gen, struct errbuf *perrbuf);
void devcreate(struct proc *up, struct chan *a, char *b, int c, int d, struct errbuf *perrbuf);
struct block *devbread(struct chan *c, long n, int64_t offset, struct errbuf *perrbuf);
long devbwrite(struct chan *c, struct block *bp, int64_t offset, struct errbuf *perrbuf);
void devremove(struct chan *c, struct errbuf *perrbuf);
long devwstat(struct proc *up, struct chan *c, uint8_t *a, long b, struct errbuf *perrbuf);
void devpower(int onoff, struct errbuf *perrbuf);
int devconfig(int a, char *b, void *v, struct errbuf *perrbuf);

/* kern/src/plan9file.c */
int openmode(int omode, struct errbuf *e);
long sysread(struct proc *up, int fd, void *p, size_t n, off_t off);
int sysopen(struct proc *up, char *name, int omode);


/* ker/src/err.c */
int pusherror(struct errbuf *errstack, int stacksize,
	      int *curindex, struct errbuf  **perrbuf);
struct errbuf *poperror(struct errbuf *errstack, int stacksize,
			int *curindex, struct errbuf  **perrbuf);


/* no support for reader/writer locks yet. */
#define rlock(x) spin_lock(x)
#define runlock(x) spin_unlock(x)
#define wlock(x) spin_lock(x)
#define wunlock(x) spin_unlock(x)

/* qlocks are somewhat special, so leave the calls the same for now. */
#define qlock(x) spin_lock(x)
#define qunlock(x) spin_unlock(x)

/* plan 9 has a concept of a hostowner, which is a name. For now, we got with a define. */
#define eve "eve"
