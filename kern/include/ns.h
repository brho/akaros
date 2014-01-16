//INFERNO
#pragma src "/usr/inferno/lib9"
#pragma	lib	"libc.a"

#define	VERSION9P	"9P2000"

#define	MAXWELEM	16

typedef
struct	Fcall
{
	uchar	type;
	u32int	fid;
	ushort	tag;
	/* union { */
		/* struct { */
			u32int	msize;		/* Tversion, Rversion */
			char	*version;	/* Tversion, Rversion */
		/* }; */
		/* struct { */
			ushort	oldtag;		/* Tflush */
		/* }; */
		/* struct { */
			char	*ename;		/* Rerror */
		/* }; */
		/* struct { */
			Qid	qid;		/* Rattach, Ropen, Rcreate */
			u32int	iounit;		/* Ropen, Rcreate */
		/* }; */
		/* struct { */
			Qid	aqid;		/* Rauth */
		/* }; */
		/* struct { */
			u32int	afid;		/* Tauth, Tattach */
			char	*uname;		/* Tauth, Tattach */
			char	*aname;		/* Tauth, Tattach */
		/* }; */
		/* struct { */
			u32int	perm;		/* Tcreate */ 
			char	*name;		/* Tcreate */
			uchar	mode;		/* Tcreate, Topen */
		/* }; */
		/* struct { */
			u32int	newfid;		/* Twalk */
			ushort	nwname;		/* Twalk */
			char	*wname[MAXWELEM];	/* Twalk */
		/* }; */
		/* struct { */
			ushort	nwqid;		/* Rwalk */
			Qid	wqid[MAXWELEM];		/* Rwalk */
		/* }; */
		/* struct { */
			vlong	offset;		/* Tread, Twrite */
			u32int	count;		/* Tread, Twrite, Rread */
			char	*data;		/* Twrite, Rread */
		/* }; */
		/* struct { */
			ushort	nstat;		/* Twstat, Rstat */
			uchar	*stat;		/* Twstat, Rstat */
		/* }; */
	/* }; */
} Fcall;


#define	GBIT8(p)	((p)[0])
#define	GBIT16(p)	((p)[0]|((p)[1]<<8))
#define	GBIT32(p)	((u32int)((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24)))
#define	GBIT64(p)	((u32int)((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24)) |\
				((vlong)((p)[4]|((p)[5]<<8)|((p)[6]<<16)|((p)[7]<<24)) << 32))

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

#define	NOTAG		(ushort)~0U	/* Dummy tag */
#define	NOFID		(u32int)~0U	/* Dummy fid */
#define	IOHDRSZ		24	/* ample room for Twrite/Rread header (iounit) */

enum
{
	Tversion =	100,
	Rversion,
	Tauth =	102,
	Rauth,
	Tattach =	104,
	Rattach,
	Terror =	106,	/* illegal */
	Rerror,
	Tflush =	108,
	Rflush,
	Twalk =		110,
	Rwalk,
	Topen =		112,
	Ropen,
	Tcreate =	114,
	Rcreate,
	Tread =		116,
	Rread,
	Twrite =	118,
	Rwrite,
	Tclunk =	120,
	Rclunk,
	Tremove =	122,
	Rremove,
	Tstat =		124,
	Rstat,
	Twstat =	126,
	Rwstat,
	Tmax,
};

uint	convM2S(uchar*, uint, Fcall*);
uint	convS2M(Fcall*, uchar*, uint);
uint	sizeS2M(Fcall*);

int	statcheck(uchar *abuf, uint nbuf);
uint	convM2D(uchar*, uint, Dir*, char*);
uint	convD2M(Dir*, uchar*, uint);
uint	sizeD2M(Dir*);

int	fcallfmt(Fmt*);
int	dirfmt(Fmt*);
int	dirmodefmt(Fmt*);

int	read9pmsg(int, void*, uint);

#pragma	varargck	type	"F"	Fcall*
#pragma	varargck	type	"M"	ulong
#pragma	varargck	type	"D"	Dir*

typedef struct Alarms	Alarms;
typedef struct Block	Block;
typedef struct Bkpt Bkpt;
typedef struct BkptCond BkptCond;
typedef struct Chan	Chan;
typedef struct Cmdbuf	Cmdbuf;
typedef struct Cmdtab	Cmdtab;
typedef struct Cname	Cname;
typedef struct Crypt	Crypt;
typedef struct Dev	Dev;
typedef struct DevConf	DevConf;
typedef struct Dirtab	Dirtab;
typedef struct Edf	Edf;
typedef struct Egrp	Egrp;
typedef struct Evalue	Evalue;
typedef struct Fgrp	Fgrp;
typedef struct List	List;
typedef struct Log	Log;
typedef struct Logflag	Logflag;
typedef struct Mntcache Mntcache;
typedef struct Mntparam Mntparam;
typedef struct Mount	Mount;
typedef struct Mntrpc	Mntrpc;
typedef struct Mntwalk	Mntwalk;
typedef struct Mnt	Mnt;
typedef struct Mhead	Mhead;
typedef struct Osenv	Osenv;
typedef struct Pgrp	Pgrp;
typedef struct Proc	Proc;
typedef struct QLock	QLock;
typedef struct Queue	Queue;
typedef struct Ref	Ref;
typedef struct Rendez	Rendez;
typedef struct Rept	Rept;
typedef struct Rootdata	Rootdata;
typedef struct RWlock	RWlock;
typedef struct Signerkey Signerkey;
typedef struct Skeyset	Skeyset;
typedef struct Talarm	Talarm;
typedef struct Timer	Timer;
typedef struct Timers	Timers;
typedef struct Uart	Uart;
typedef struct Walkqid	Walkqid;
typedef int    Devgen(Chan*, char*, Dirtab*, int, int, Dir*);

#pragma incomplete DevConf
#pragma incomplete Edf
#pragma incomplete Mntcache
#pragma incomplete Mntrpc
#pragma incomplete Queue
#pragma incomplete Timers

#include "fcall.h"
#include <pool.h>

struct Ref
{
	Lock	l;
	long	ref;
};

struct Rendez
{
	Lock;
	Proc	*p;
};

struct Rept
{
	Lock	l;
	Rendez	r;
	void	*o;
	int	t;
	int	(*active)(void*);
	int	(*ck)(void*, int);
	void	(*f)(void*);	/* called with VM acquire()'d */
};

struct Osenv
{
	char	*syserrstr;	/* last error from a system call, errbuf0 or 1 */
	char	*errstr;	/* reason we're unwinding the error stack, errbuf1 or 0 */
	char	errbuf0[ERRMAX];
	char	errbuf1[ERRMAX];
	Pgrp*	pgrp;		/* Ref to namespace, working dir and root */
	Fgrp*	fgrp;		/* Ref to file descriptors */
	Egrp*	egrp;	/* Environment vars */
	Skeyset*	sigs;		/* Signed module keys */
	Rendez*	rend;		/* Synchro point */
	Queue*	waitq;		/* Info about dead children */
	Queue*	childq;		/* Info about children for debuggers */
	void*	debug;		/* Debugging master */
	int	uid;		/* Numeric user id for system */
	int	gid;		/* Numeric group id for system */
	char*	user;		/* Inferno user name */
	FPenv	fpu;		/* Floating point thread state */
};

enum
{
	Nopin =	-1
};

struct QLock
{
	Lock	use;			/* to access Qlock structure */
	Proc	*head;			/* next process waiting for object */
	Proc	*tail;			/* last process waiting for object */
	int	locked;			/* flag */
};

struct RWlock
{
	Lock;				/* Lock modify lock */
	QLock	x;			/* Mutual exclusion lock */
	QLock	k;			/* Lock for waiting writers */
	int	readers;		/* Count of readers in lock */
};

struct Talarm
{
	Lock;
	Proc*	list;
};

struct Alarms
{
	QLock;
	Proc*	head;
};

struct Rootdata
{
	int	dotdot;
	void	*ptr;
	int	size;
	int	*sizep;
};

/*
 * Access types in namec & channel flags
 */
enum
{
	Aaccess,			/* as in stat, wstat */
	Abind,			/* for left-hand-side of bind */
	Atodir,				/* as in chdir */
	Aopen,				/* for i/o */
	Amount,				/* to be mounted or mounted upon */
	Acreate,			/* is to be created */
	Aremove,			/* will be removed by caller */

	COPEN	= 0x0001,		/* for i/o */
	CMSG	= 0x0002,		/* the message channel for a mount */
	CCEXEC	= 0x0008,		/* close on exec */
	CFREE	= 0x0010,		/* not in use */
	CRCLOSE	= 0x0020,		/* remove on close */
	CCACHE	= 0x0080,		/* client cache */
};

enum
{
	BINTR		=	(1<<0),
	BFREE		=	(1<<1),
	Bipck	=	(1<<2),		/* ip checksum */
	Budpck	=	(1<<3),		/* udp checksum */
	Btcpck	=	(1<<4),		/* tcp checksum */
	Bpktck	=	(1<<5),		/* packet checksum */
};

struct Block
{
	Block*	next;
	Block*	list;
	uchar*	rp;			/* first unconsumed byte */
	uchar*	wp;			/* first empty byte */
	uchar*	lim;			/* 1 past the end of the buffer */
	uchar*	base;			/* start of the buffer */
	void	(*free)(Block*);
	ushort	flag;
	ushort	checksum;		/* IP checksum of complete packet (minus media header) */
};
#define BLEN(s)	((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base)

struct Chan
{
	Lock;
	Ref;
	Chan*	next;			/* allocation */
	Chan*	link;
	vlong	offset;			/* in file */
	ushort	type;
	ulong	dev;
	ushort	mode;			/* read/write */
	ushort	flag;
	Qid	qid;
	int	fid;			/* for devmnt */
	ulong	iounit;	/* chunk size for i/o; 0==default */
	Mhead*	umh;			/* mount point that derived Chan; used in unionread */
	Chan*	umc;			/* channel in union; held for union read */
	QLock	umqlock;		/* serialize unionreads */
	int	uri;			/* union read index */
	int	dri;			/* devdirread index */
	ulong	mountid;
	Mntcache *mcp;			/* Mount cache pointer */
	Mnt		*mux;		/* Mnt for clients using me for messages */
	union {
		void*	aux;
		char	tag[4];		/* for iproute */
	};
	Chan*	mchan;			/* channel to mounted server */
	Qid	mqid;			/* qid of root of mount point */
	Cname	*name;
};

struct Cname
{
	Ref;
	int	alen;			/* allocated length */
	int	len;			/* strlen(s) */
	char	*s;
};

struct Dev
{
	int	dc;
	char*	name;

	void	(*reset)(void);
	void	(*init)(void);
	void	(*shutdown)(void);
	Chan*	(*attach)(char*);
	Walkqid*	(*walk)(Chan*, Chan*, char**, int);
	int	(*stat)(Chan*, uchar*, int);
	Chan*	(*open)(Chan*, int);
	void	(*create)(Chan*, char*, int, ulong);
	void	(*close)(Chan*);
	long	(*read)(Chan*, void*, long, vlong);
	Block*	(*bread)(Chan*, long, ulong);
	long	(*write)(Chan*, void*, long, vlong);
	long	(*bwrite)(Chan*, Block*, ulong);
	void	(*remove)(Chan*);
	int	(*wstat)(Chan*, uchar*, int);
	void	(*power)(int);	/* power mgt: power(1) → on, power (0) → off */
	int	(*config)(int, char*, DevConf*);
};

struct Dirtab
{
	char	name[KNAMELEN];
	Qid	qid;
	vlong	length;
	long	perm;
};

struct Walkqid
{
	Chan	*clone;
	int	nqid;
	Qid	qid[1];
};

enum
{
	NSMAX	=	1000,
	NSLOG	=	7,
	NSCACHE	=	(1<<NSLOG),
};

struct Mntwalk				/* state for /proc/#/ns */
{
	int		cddone;
	ulong	id;
	Mhead*	mh;
	Mount*	cm;
};

struct Mount
{
	ulong	mountid;
	Mount*	next;
	Mhead*	head;
	Mount*	copy;
	Mount*	order;
	Chan*	to;			/* channel replacing channel */
	int	mflag;
	char	*spec;
};

struct Mhead
{
	Ref;
	RWlock	lock;
	Chan*	from;			/* channel mounted upon */
	Mount*	mount;			/* what's mounted upon it */
	Mhead*	hash;			/* Hash chain */
};

struct Mnt
{
	Lock;
	/* references are counted using c->ref; channels on this mount point incref(c->mchan) == Mnt.c */
	Chan	*c;		/* Channel to file service */
	Proc	*rip;		/* Reader in progress */
	Mntrpc	*queue;		/* Queue of pending requests on this channel */
	ulong	id;		/* Multiplexer id for channel check */
	Mnt	*list;		/* Free list */
	int	flags;		/* cache */
	int	msize;		/* data + IOHDRSZ */
	char	*version;			/* 9P version */
	Queue	*q;		/* input queue */
};

enum
{
	RENDLOG	=	5,
	RENDHASH =	1<<RENDLOG,		/* Hash to lookup rendezvous tags */
	MNTLOG	=	5,
	MNTHASH =	1<<MNTLOG,		/* Hash to walk mount table */
	DELTAFD=		20,		/* allocation quantum for process file descriptors */
	MAXNFD =		4000,		/* max per process file descriptors */
	MAXKEY =		8,	/* keys for signed modules */
};
#define MOUNTH(p,qid)	((p)->mnthash[(qid).path&((1<<MNTLOG)-1)])

struct Mntparam {
	Chan*	chan;
	Chan*	authchan;
	char*	spec;
	int	flags;
};

struct Pgrp
{
	Ref;				/* also used as a lock when mounting */
	ulong	pgrpid;
	QLock	debug;			/* single access via devproc.c */
	RWlock	ns;			/* Namespace n read/one write lock */
	QLock	nsh;
	Mhead*	mnthash[MNTHASH];
	int	progmode;
	Chan*	dot;
	Chan*	slash;
	int	nodevs;
	int	pin;
};

struct Fgrp
{
	Lock;
	Ref;
	Chan**	fd;
	int	nfd;			/* number of fd slots */
	int	maxfd;			/* highest fd in use */
	int	minfd;			/* lower bound on free fd */
};

struct Evalue
{
	char	*var;
	char	*val;
	int	len;
	Qid	qid;
	Evalue	*next;
};

struct Egrp
{
	Ref;
	QLock;
	Evalue	*entries;
	ulong	path;	/* qid.path of next Evalue to be allocated */
	ulong	vers;	/* of Egrp */
};

struct Signerkey
{
	Ref;
	char*	owner;
	ushort	footprint;
	ulong	expires;
	void*	alg;
	void*	pk;
	void	(*pkfree)(void*);
};

struct Skeyset
{
	Ref;
	QLock;
	ulong	flags;
	char*	devs;
	int	nkey;
	Signerkey	*keys[MAXKEY];
};

/*
 * fasttick timer interrupts
 */
enum {
	/* Mode */
	Trelative,	/* timer programmed in ns from now */
	Tabsolute,	/* timer programmed in ns since epoch */
	Tperiodic,	/* periodic timer, period in ns */
};

struct Timer
{
	/* Public interface */
	int	tmode;		/* See above */
	vlong	tns;		/* meaning defined by mode */
	void	(*tf)(Ureg*, Timer*);
	void	*ta;
	/* Internal */
	Lock;
	Timers	*tt;		/* Timers queue this timer runs on */
	vlong	twhen;		/* ns represented in fastticks */
	Timer	*tnext;
};

enum
{
	Dead = 0,		/* Process states */
	Moribund,
	Ready,
	Scheding,
	Running,
	Queueing,
	Wakeme,
	Broken,
	Stopped,
	Rendezvous,
	Waitrelease,

	Proc_stopme = 1, 	/* devproc requests */
	Proc_exitme,
	Proc_traceme,
	Proc_exitbig,

	NERR		= 30,

	Unknown		= 0,
	IdleGC,
	Interp,
	BusyGC,

	PriLock		= 0,	/* Holding Spin lock */
	PriEdf,	/* active edf processes */
	PriRelease,	/* released edf processes */
	PriRealtime,		/* Video telephony */
	PriHicodec,		/* MPEG codec */
	PriLocodec,		/* Audio codec */
	PriHi,			/* Important task */
	PriNormal,
	PriLo,
	PriBackground,
	PriExtra,	/* edf processes we don't care about */
	Nrq
};

struct Proc
{
	Label		sched;		/* known to l.s */
	char*		kstack;		/* known to l.s */
	Mach*		mach;		/* machine running this proc */
	char		text[KNAMELEN];
	Proc*		rnext;		/* next process in run queue */
	Proc*		qnext;		/* next process on queue for a QLock */
	QLock*		qlock;		/* addrof qlock being queued for DEBUG */
	int		state;
	int		type;
	void*		prog;		/* Dummy Prog for interp release */
	void*		iprog;
	Osenv*		env;
	Osenv		defenv;
	int		swipend;	/* software interrupt pending for Prog */
	Lock		sysio;		/* note handler lock */
	char*		psstate;	/* What /proc/#/status reports */
	ulong		pid;
	int		fpstate;
	int		procctl;	/* Control for /proc debugging */
	ulong		pc;		/* DEBUG only */
	Lock	rlock;	/* sync between sleep/swiproc for r */
	Rendez*		r;		/* rendezvous point slept on */
	Rendez		sleep;		/* place for syssleep/debug */
	int		killed;		/* by swiproc */
	int		kp;		/* true if a kernel process */
	ulong		alarm;		/* Time of call */
	int		pri;		/* scheduler priority */
	ulong		twhen;
	Rendez*		trend;
	Proc*		tlink;
	int		(*tfn)(void*);
	void		(*kpfun)(void*);
	void*		arg;
	FPU		fpsave;
	int		scallnr;
	int		nerrlab;
	Label		errlab[NERR];
	char	genbuf[128];	/* buffer used e.g. for last name element from namec */
	Mach*		mp;		/* machine this process last ran on */
	Mach*		wired;
	ulong		movetime;	/* next time process should switch processors */
	ulong		delaysched;
	int			preempted;	/* process yielding in interrupt */
	ulong		qpc;		/* last call that blocked in qlock */
	void*		dbgreg;		/* User registers for devproc */
 	int		dbgstop;		/* don't run this kproc */
	Edf*	edf;	/* if non-null, real-time proc, edf contains scheduling params */
};

enum
{
	/* kproc flags */
	KPDUPPG		= (1<<0),
	KPDUPFDG	= (1<<1),
	KPDUPENVG	= (1<<2),
	KPDUP = KPDUPPG | KPDUPFDG | KPDUPENVG
};

enum {
	BrkSched,
	BrkNoSched,
};

struct BkptCond
{
	uchar op;
	ulong val;
	BkptCond *next;
};

struct Bkpt
{
	int id;
	ulong addr;
	BkptCond *conditions;
	Instr instr;
	void (*handler)(Bkpt*);
	void *aux;
	Bkpt *next;
	Bkpt *link;
};

enum
{
	PRINTSIZE =	256,
	NUMSIZE	=	12,		/* size of formatted number */
	MB =		(1024*1024),
	READSTR =	1000,		/* temporary buffer size for device reads */
};

extern	Conf	conf;
extern	char*	conffile;
extern	int	consoleprint;
extern	Dev*	devtab[];
extern	char*	eve;
extern	int	hwcurs;
extern	FPU	initfp;
extern  Queue	*kbdq;
extern  Queue	*kscanq;
extern  Ref	noteidalloc;
extern  Queue	*printq;
extern	uint	qiomaxatomic;
extern	char*	statename[];
extern	char*	sysname;
extern	Talarm	talarm;

/*
 *  action log
 */
struct Log {
	Lock;
	int	opens;
	char*	buf;
	char	*end;
	char	*rptr;
	int	len;
	int	nlog;
	int	minread;

	int	logmask;	/* mask of things to debug */

	QLock	readq;
	Rendez	readr;
};

struct Logflag {
	char*	name;
	int	mask;
};

struct Cmdbuf
{
	char	*buf;
	char	**f;
	int	nf;
};

struct Cmdtab
{
	int	index;	/* used by client to switch on result */
	char	*cmd;	/* command name */
	int	narg;	/* expected #args; 0 ==> variadic */
};

enum
{
	MAXPOOL		= 8,
};

extern Pool*	mainmem;
extern Pool*	heapmem;
extern Pool*	imagmem;

/* queue state bits,  Qmsg, Qcoalesce, and Qkick can be set in qopen */
enum
{
	/* Queue.state */
	Qstarve		= (1<<0),	/* consumer starved */
	Qmsg		= (1<<1),	/* message stream */
	Qclosed		= (1<<2),	/* queue has been closed/hungup */
	Qflow		= (1<<3),	/* producer flow controlled */
	Qcoalesce	= (1<<4),	/* coallesce packets on read */
	Qkick		= (1<<5),	/* always call the kick routine after qwrite */
};

#define DEVDOTDOT -1

#pragma	varargck	argpos	print	1
#pragma	varargck	argpos	snprint	3
#pragma	varargck	argpos	seprint	3
#pragma	varargck	argpos	sprint	2
#pragma	varargck	argpos	fprint	2
#pragma	varargck	argpos	iprint	1
#pragma	varargck	argpos	panic	1
#pragma	varargck	argpos	kwerrstr	1
#pragma	varargck	argpos	kprint	1

#pragma	varargck	type	"lld"	vlong
#pragma	varargck	type	"llx"	vlong
#pragma	varargck	type	"lld"	uvlong
#pragma	varargck	type	"llx"	uvlong
#pragma	varargck	type	"lx"	void*
#pragma	varargck	type	"ld"	long
#pragma	varargck	type	"lx"	long
#pragma	varargck	type	"ld"	ulong
#pragma	varargck	type	"lx"	ulong
#pragma	varargck	type	"d"	int
#pragma	varargck	type	"x"	int
#pragma	varargck	type	"c"	int
#pragma	varargck	type	"C"	int
#pragma	varargck	type	"d"	uint
#pragma	varargck	type	"x"	uint
#pragma	varargck	type	"c"	uint
#pragma	varargck	type	"C"	uint
#pragma	varargck	type	"f"	double
#pragma	varargck	type	"e"	double
#pragma	varargck	type	"g"	double
#pragma	varargck	type	"s"	char*
#pragma	varargck	type	"S"	Rune*
#pragma	varargck	type	"r"	void
#pragma	varargck	type	"%"	void
#pragma	varargck	type	"I"	uchar*
#pragma	varargck	type	"V"	uchar*
#pragma	varargck	type	"E"	uchar*
#pragma	varargck	type	"M"	uchar*
#pragma	varargck	type	"p"	void*
#pragma	varargck	type	"q"	char*
