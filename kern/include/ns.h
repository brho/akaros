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


#pragma once

#include <err.h>
#include <rendez.h>
#include <rwlock.h>
#include <linker_func.h>
#include <fdtap.h>
#include <ros/fs.h>
#include <bitmask.h>
#include <mm.h>
#include <sys/uio.h>
#include <time.h>

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
	NUMSIZE32 = 10,	/* max size of formatted 32 bit number (hex or decimal) */
	NUMSIZE64 = 20,	/* max size of formatted 64 bit number (hex or decimal) */
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

extern int getfields(char *unused_char_p_t, char **unused_char_pp_t,
					 int unused_int, int, char *);
extern int tokenize(char *unused_char_p_t, char **unused_char_pp_t, int);
extern int dec64(uint8_t * unused_uint8_p_t, int unused_int,
				 char *unused_char_p_t, int);
extern void qsort(void *, long, long, int (*)(void *, void *));

extern int toupper(int);
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
#define QTSYMLINK	0x02	/* type bit for symlinks */
#define QTFILE		0x01	/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMWRITABLE	0x08000000	/* non-standard, for select() */
#define DMREADABLE	0x04000000	/* non-standard, for select() */
#define DMSYMLINK	0x02000000	/* symlink -- from 9p2000.u */
/* The lower parts of dir.mode are the three rwx perms (S_PMASK) */
#define DMMODE_BITS (DMDIR | DMAPPEND | DMEXCL | DMMOUNT | DMWRITABLE \
                     | DMREADABLE | DMSYMLINK)

struct qid {
	uint64_t path;
	uint32_t vers;
	uint8_t type;
};

struct dir {
	/* system-modified data */
	uint16_t type;				/* server type */
	uint32_t dev;			/* server subtype */
	/* file data */
	struct qid qid;				/* unique id from server */
	uint32_t mode;				/* permissions */
	/* 9p stat has u32 atime (seconds) here */
	/* 9p stat has u32 mtime (seconds) here */
	uint64_t length;			/* file length: see <u.h> */
	char *name;					/* last element of path */
	char *uid;					/* owner name */
	char *gid;					/* group name */
	char *muid;					/* last modifier name */
	char *ext;					/* extensions for special files (symlinks) */
	uint32_t n_uid;				/* numeric owner uid */
	uint32_t n_gid;				/* numeric group id */
	uint32_t n_muid;			/* numeric last modifier id */
	struct timespec atime;		/* last access time */
	struct timespec btime;		/* file creation time */
	struct timespec ctime;		/* last attribute change time */
	struct timespec mtime;		/* last data modification time */
};

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

/* The 9p STATFIXLENs include the leading 16-bit count.  The count, however,
 * excludes itself; total size is BIT16SZ + count.  This is the amount of fixed
 * length data in a stat buffer.  This does not include the strings, but it
 * includes the string counts (u16s)
 *
 * STAT_FIX_LEN_9P is the original 9p stat message: type to length, including
 * u32 atime and u32 mtime.  This is the bare minimum for a stat that we
 * receive.  We check in e.g. convM2D for any extra fields.
 *
 * STAT_FIX_LEN_AK is the stat message used by Akaros, which includes Eric VH's
 * extensions and full timespecs.  It is analogous to struct dir, including the
 * u32s for the legacy atime/mtime.  We always send stats of this size, e.g. in
 * convD2M.
 *
 * Note that the extended stat message has fixed data after the strings, but to
 * get to this data, you have to jump through the string and their counts
 * (u16s).  The counts are part of the fixed length, but not the strings.  Also
 * note that the _AK version has an extra string. */
#define STAT_NR_STRINGS_9P 4
#define STAT_NR_STRINGS_AK 5
#define STAT_FIX_LEN_9P (BIT16SZ +                      /* size */             \
                         BIT16SZ +                      /* type */             \
                         BIT32SZ +                      /* dev */              \
                         QIDSZ +                        /* qid */              \
                         BIT32SZ +                      /* mode */             \
                         BIT32SZ +                      /* atime u32 */        \
                         BIT32SZ +                      /* mtime u32 */        \
                         BIT64SZ +                      /* length */           \
                         STAT_NR_STRINGS_9P * BIT16SZ + /* string counts */    \
						 0)
#define __STAT_FIX_LEN_AK_NONSTRING (                                          \
                         BIT32SZ +                      /* n_uid */            \
                         BIT32SZ +                      /* n_gid */            \
                         BIT32SZ +                      /* n_muid */           \
                         2 * BIT64SZ +                  /* atime */            \
                         2 * BIT64SZ +                  /* btime */            \
                         2 * BIT64SZ +                  /* ctime */            \
                         2 * BIT64SZ +                  /* mtime */            \
						 0)
#define STAT_FIX_LEN_AK (STAT_FIX_LEN_9P +                                     \
                         (STAT_NR_STRINGS_AK - STAT_NR_STRINGS_9P) * BIT16SZ + \
                         __STAT_FIX_LEN_AK_NONSTRING +                         \
						 0)

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

void init_empty_dir(struct dir *d);
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
	Arename,					/* new_path of a rename */

	/* internal chan flags, used by the kernel only */
	COPEN = 		0x0001,	/* for i/o */
	CMSG = 			0x0002,	/* the message channel for a mount */
	CFREE = 		0x0004,	/* not in use */
	CCACHE = 		0x0008,	/* client cache */
	CINTERNAL_FLAGS = (COPEN | CMSG | CFREE | CCACHE),

	/* chan/file flags, getable via fcntl/getfl and setably via open and
	 * sometimes fcntl/setfl.  those that can't be set cause an error() in
	 * fd_setfl. */
	CEXTERNAL_FLAGS = (
	    O_CLOEXEC      | /* (prob should be on the FD, 9ns has it here) */
	    O_REMCLO       | /* remove on close (also, maybe should be on FD) */
	    O_APPEND       | /* append on write */
	    O_NONBLOCK     | /* don't block, can't be set via setfl */
	    O_PATH         | /* path open, just the name, no I/O */
	    0),
};

#define NS_IPCK_SHIFT  2
#define NS_UDPCK_SHIFT 3
#define NS_TCPCK_SHIFT 4
#define NS_PKTCK_SHIFT 5
#define NS_TSO_SHIFT 6
#define NS_SHIFT_MAX 6

enum {
	BFREE = (1 << 1),
	Bipck = (1 << NS_IPCK_SHIFT),	/* ip checksum (rx) */
	Budpck = (1 << NS_UDPCK_SHIFT),	/* udp checksum (rx), needed (tx) */
	Btcpck = (1 << NS_TCPCK_SHIFT),	/* tcp checksum (rx), needed (tx) */
	Bpktck = (1 << NS_PKTCK_SHIFT),	/* packet checksum (rx, maybe) */
	Btso = (1 << NS_TSO_SHIFT),		/* TSO desired (tx) */
};
#define BLOCK_META_FLAGS (Bipck | Budpck | Btcpck | Bpktck | Btso)
#define BLOCK_TRANS_TX_CSUM (Budpck | Btcpck)
#define BLOCK_RX_CSUM (Bipck | Budpck | Btcpck)

struct extra_bdata {
	uintptr_t base;
	/* using u32s for packing reasons.  this means no extras > 4GB */
	uint32_t off;
	uint32_t len;
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
	uint16_t mss;               /* TCP MSS for TSO */
	uint16_t network_offset;	/* offset from start */
	uint16_t transport_offset;	/* offset from start */
	uint16_t tx_csum_offset;	/* offset from tx_offset to store csum */
	/* might want something to track the next free extra_data slot */
	size_t extra_len;
	unsigned int nr_extra_bufs;
	struct extra_bdata *extra_data;
};
#define BLEN(s)	((s)->wp - (s)->rp + (s)->extra_len)
#define BHLEN(s) ((s)->wp - (s)->rp)
#define BALLOC(s) ((s)->lim - (s)->base + (s)->extra_len)

struct chan {
	spinlock_t lock;
	struct kref ref;
	struct chan *next;			/* allocation */
	struct chan *link;
	int64_t offset;				/* in file */
	int type;
	uint32_t dev;
	uint16_t mode;				/* read/write */
	int flag;
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
	/* mountpoint, as discovered during walk.
	 * Used for rename at present.
	 */
	struct chan *mountpoint;
	struct chan *mchan;			/* channel to mounted server */
	struct qid mqid;			/* qid of root of mount point */
	struct cname *name;
	/* hack for dir reads to try to get them right. */
	int ateof;
	void *buf;
	int bufused;
	/* A lot of synthetic files need something generated at open time, which the
	 * user can read from (including offsets) while the underlying file changes.
	 * Hang that buffer here. */
	void *synth_buf;
};

extern struct chan *kern_slash;

struct cname {
	struct kref ref;
	int alen;					/* allocated length */
	int len;					/* strlen(s) */
	char *s;
};

struct fs_file;

struct dev {
	char *name;

	void (*reset)(void);
	void (*init)(void);
	void (*shutdown)(void);
	struct chan *(*attach)(char *muxattach);
	struct walkqid *(*walk)(struct chan *, struct chan *, char **name,
	                        unsigned int);
	size_t (*stat)(struct chan *, uint8_t *, size_t);
	struct chan *(*open)(struct chan *, int);
	void (*create)(struct chan *, char *, int, uint32_t, char *);
	void (*close)(struct chan *);
	size_t (*read)(struct chan *, void *, size_t, off64_t);
	struct block *(*bread)(struct chan *, size_t, off64_t);
	size_t (*write)(struct chan *, void *, size_t, off64_t);
	size_t (*bwrite)(struct chan *, struct block *, off64_t);
	void (*remove)(struct chan *);
	void (*rename)(struct chan *, struct chan *, const char *, int);
	size_t (*wstat)(struct chan *, uint8_t *, size_t);
	void (*power)(int);		/* power mgt: power(1) → on, power (0) → off */
//  int (*config)( int unused_int, char *unused_char_p_t, DevConf*);
	char *(*chaninfo)(struct chan *, char *, size_t);
	int (*tapfd)(struct chan *, struct fd_tap *, int);
	int (*chan_ctl)(struct chan *, int);
	struct fs_file *(*mmap)(struct chan *, struct vm_region *, int, int);
	/* we need to be aligned to 64 bytes for the linker tables. */
} __attribute__ ((aligned(64)));

struct dirtab {
	char name[KNAMELEN];
	struct qid qid;
	int64_t length;
	int perm;
	/* we need to be aligned to 64 bytes for the linker tables. */
} __attribute__ ((aligned(64)));

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
	int nodevs;
	int pin;
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
	READSTR = 2000,	/* temporary buffer size for device reads */
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

/* queue state bits, all can be set in qopen (Qstarve is always set) */
enum {
	Qmsg			= (1 << 1),	/* message stream */
	Qclosed			= (1 << 2),	/* queue has been closed/hungup */
	Qcoalesce		= (1 << 3),	/* coalesce empty packets on read */
	Qkick			= (1 << 4),	/* always call the kick routine after qwrite */
	Qdropoverflow	= (1 << 5),	/* writes that would block will be dropped */
};

/* Per-process structs */
#define NR_OPEN_FILES_DEFAULT 32
#define NR_FILE_DESC_DEFAULT 32

/* Bitmask for file descriptors, big for when we exceed the initial small.  We
 * could just use the fd_array to check for openness instead of the bitmask,
 * but eventually we might want to use the bitmasks for other things (like
 * which files are close_on_exec. */

typedef struct fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_MAX)];
} fd_set;


struct small_fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_DEFAULT)];
};

/* Helper macros to manage fd_sets */
#define FD_SET(n, p)	((p)->fds_bits[(n) / 8] |=  (1 << ((n) & 7)))
#define FD_CLR(n, p)	((p)->fds_bits[(n) / 8] &= ~(1 << ((n) & 7)))
#define FD_ISSET(n, p)	((p)->fds_bits[(n) / 8] &   (1 << ((n) & 7)))
#define FD_ZERO(p)		memset((void*)(p), 0, sizeof(*(p)))

/* Describes an open file.  We need this, since the FD flags are supposed to be
 * per file descriptor, not per file (like the file status flags). */
struct file_desc {
	struct chan					*fd_chan;
	unsigned int				fd_flags;
	struct fd_tap				*fd_tap;
};

/* All open files for a process */
struct fd_table {
	spinlock_t					lock;
	bool						closed;
	int							max_files;		/* max files ptd to by fd */
	int							max_fdset;		/* max of the current fd_set */
	int							hint_min_fd;	/* <= min available fd */
	struct file_desc			*fd;			/* initially pts to fd_array */
	struct fd_set				*open_fds;		/* init, pts to open_fds_init */
	struct small_fd_set			open_fds_init;
	struct file_desc			fd_array[NR_OPEN_FILES_DEFAULT];
};

ssize_t kread_file(struct file_or_chan *file, void *buf, size_t sz);
void *kread_whole_file(struct file_or_chan *file);

/* Process-related File management functions */
void *lookup_fd(struct fd_table *fdt, int fd, bool incref);
int insert_obj_fdt(struct fd_table *fdt, void *obj, int low_fd, int fd_flags,
                   bool must_use_low);
bool close_fd(struct fd_table *fdt, int fd);
void close_fdt(struct fd_table *open_files, bool cloexec);
void clone_fdt(struct fd_table *src, struct fd_table *dst);

#define DEVDOTDOT -1

typedef int Devgen(struct chan *, char *unused_char_p_t, struct dirtab *,
				   int unused_int, int, struct dir *);

/* inferno portfns.h. Not all these are needed. */
#define		FPinit() fpinit()	/* remove this if math lib is linked */
void FPrestore(void *);
void FPsave(void *);
struct cname *addelem(struct cname *, char *unused_char_p_t);
void addprog(struct proc *);
void addrootfile(char *unused_char_p_t, uint8_t * unused_uint8_p_t, uint32_t);
struct block *adjustblock(struct block *, int);
struct block *block_alloc(size_t, int);
int block_add_extd(struct block *b, unsigned int nr_bufs, int mem_flags);
int block_append_extra(struct block *b, uintptr_t base, uint32_t off,
                       uint32_t len, int mem_flags);
void block_copy_metadata(struct block *new_b, struct block *old_b);
void block_reset_metadata(struct block *b);
int anyhigher(void);
int anyready(void);
void _assert(char *unused_char_p_t);
struct block *bl2mem(uint8_t * unused_uint8_p_t, struct block *, int);
int blocklen(struct block *);
char *channame(struct chan *);
void cclose(struct chan *);
void chan_incref(struct chan *);
void chandevinit(void);
void chandevreset(void);
void chandevshutdown(void);
void chanfree(struct chan *);
void chanrec(struct mnt *);
void checkalarms(void);
void checkb(struct block *, char *unused_char_p_t);
struct chan *cclone(struct chan *);
void cclose(struct chan *);
void closeegrp(struct egrp *);
void closemount(struct mount *);
void closepgrp(struct pgrp *);
void closesigs(struct skeyset *);
void debugcmd(struct cmdbuf *cb);
struct mhead *newmhead(struct chan *from);
int cmount(struct chan *, struct chan *, int unused_int, char *unused_char_p_t);
void cnameclose(struct cname *);
struct block *concatblock(struct block *);
struct block *linearizeblock(struct block *b);
void confinit(void);
void cons_add_char(char c);
struct block *copyblock(struct block *b, int mem_flags);
struct chan *cunique(struct chan *);
struct chan *createdir(struct chan *, struct mhead *);
void cunmount(struct chan *, struct chan *);
void cursorenable(void);
void cursordisable(void);
int cursoron(int);
void cursoroff(int);
struct chan *devattach(const char *name, char *spec);
struct block *devbread(struct chan *, size_t, off64_t);
size_t devbwrite(struct chan *, struct block *, off64_t);
struct chan *devclone(struct chan *);
void devcreate(struct chan *, char *name, int mode, uint32_t perm, char *ext);
void devdir(struct chan *, struct qid, char *, int64_t, char *, long,
			struct dir *);
long devdirread(struct chan *, char *, long, struct dirtab *, int, Devgen *);
Devgen devgen;
void devinit(void);
int devno(const char *name, int user);
void devpower(int);
struct dev *devbyname(char *unused_char_p_t);
struct chan *devopen(struct chan *, int unused_int,
					 struct dirtab *, int unused_int2, Devgen *);
void devpermcheck(char *unused_char_p_t, uint32_t, int);
void devremove(struct chan *);
void devreset(void);
void devshutdown(void);
size_t dev_make_stat(struct chan *c, struct dir *dir, uint8_t *dp, size_t n);
size_t devstat(struct chan *, uint8_t *db, size_t n, struct dirtab *,
               int ntab, Devgen *);
struct walkqid *devwalk(struct chan *,
						struct chan *, char **unused_char_pp_t, int unused_int,
						struct dirtab *, int unused_intw, Devgen *);
size_t devwstat(struct chan *, uint8_t *, size_t);
char *devchaninfo(struct chan *chan, char *ret, size_t ret_l);
void disinit(void *);
void disfault(void *, char *unused_char_p_t);
int domount(struct chan **, struct mhead **);
void drawactive(int);
void drawcmap(void);
void dumpstack(void);
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
int findmount(struct chan **, struct mhead **, int unused_int, int, struct qid);
void free_block_extra(struct block *);
size_t freeb(struct block *b);
size_t freeblist(struct block *b);
void freeskey(struct signerkey *);
void getcolor(uint32_t, uint32_t *, uint32_t *, uint32_t *);
uint32_t getmalloctag(void *);
uint32_t getrealloctag(void *);
void printblock(struct block *b);
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
void kprocchild(struct proc *, void (*)(void *), void *);
void (*kproftick) (uint32_t);
void ksetenv(char *unused_char_p_t, char *, int);
void kstrdup(char **cp, char *name);

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
struct chan *namec(char *unused_char_p_t, int unused_int, int, uint32_t,
                   void *ext);
struct chan *namec_from(struct chan *c, char *name, int amode, int omode,
                        uint32_t perm, void *ext);
struct chan *newchan(void);
struct egrp *newegrp(void);
struct mount *newmount(struct mhead *, struct chan *, int unused_int,
					   char *unused_char_p_t);
struct pgrp *newpgrp(void);
struct proc *newproc(void);
char *nextelem(char *unused_char_p_t, char *);

struct cname *newcname(char *unused_char_p_t);
void notkilled(void);
uint32_t random_read(void *xp, uint32_t n);
uint32_t urandom_read(void *xp, uint32_t n);
uint64_t ns2fastticks(uint64_t);
int okaddr(uint32_t, uint32_t, int);
int omode_to_rwx(int);
int omode_to_9p_accmode(int open_flags);
int access_bits_to_omode(int access_bits);
struct block *packblock(struct block *);
struct block *padblock(struct block *, int);

void pgrpcpy(struct pgrp *, struct pgrp *);

int progfdprint(struct chan *, int unused_int, int, char *unused_char_p_t,
				int i);
int pullblock(struct block **, int);
struct block *pullupblock(struct block *, int);
struct block *pullupqueue(struct queue *, int);
void putmhead(struct mhead *);
void putstrn(char *unused_char_p_t, int);
void qaddlist(struct queue *, struct block *);
struct block *qbread(struct queue *q, size_t len);
struct block *qbread_nonblock(struct queue *q, size_t len);
ssize_t qbwrite(struct queue *, struct block *);
ssize_t qbwrite_nonblock(struct queue *, struct block *);
ssize_t qibwrite(struct queue *q, struct block *b);
struct queue *qbypass(void (*)(void *, struct block *), void *);
int qcanread(struct queue *);
void qclose(struct queue *);
struct block *qcopy(struct queue *, int unused_int, uint32_t);
struct block *qclone(struct queue *q, int header_len, int len,
                     uint32_t offset);
struct block *blist_clone(struct block *blist, int header_len, int len,
                          uint32_t offset);
size_t qdiscard(struct queue *q, size_t len);
void qflush(struct queue *);
void qfree(struct queue *);
int qfull(struct queue *);
struct block *qget(struct queue *);
void qhangup(struct queue *, char *unused_char_p_t);
int qisclosed(struct queue *);
ssize_t qiwrite(struct queue *, void *, int);
int qlen(struct queue *);
size_t q_bytes_read(struct queue *q);
void qdropoverflow(struct queue *, bool);
void q_toggle_qmsg(struct queue *q, bool onoff);
void q_toggle_qcoalesce(struct queue *q, bool onoff);
struct queue *qopen(int unused_int, int, void (*)(void *), void *);
ssize_t qpass(struct queue *, struct block *);
ssize_t qpassnolim(struct queue *, struct block *);
void qputback(struct queue *, struct block *);
size_t qread(struct queue *q, void *va, size_t len);
size_t qread_nonblock(struct queue *q, void *va, size_t len);
void qreopen(struct queue *);
void qsetlimit(struct queue *, size_t);
size_t qgetlimit(struct queue *);
int qwindow(struct queue *);
ssize_t qwrite(struct queue *, void *, int);
ssize_t qwrite_nonblock(struct queue *, void *, int);
typedef void (*qio_wake_cb_t)(struct queue *q, void *data, int filter);
void qio_set_wake_cb(struct queue *q, qio_wake_cb_t func, void *data);
bool qreadable(struct queue *q);
bool qwritable(struct queue *q);

void *realloc(void *, uint32_t);
int readmem(unsigned long offset, char *buf, unsigned long n,
			const void *mem, size_t mem_len);
int readnum(unsigned long off, char *buf, unsigned long n, unsigned long val,
			size_t size);
int readnum_hex(unsigned long off, char *buf, unsigned long n,
                unsigned long val, size_t size);
int readstr(unsigned long offset, char *buf, unsigned long n, const char *str);
int readnum_int64_t(uint32_t, char *unused_char_p_t, uint32_t, int64_t, int);
unsigned long strtoul_from_ubuf(void *ubuf, size_t count, int base);
void ready(struct proc *);
void renameproguser(char *unused_char_p_t, char *);
void renameuser(char *unused_char_p_t, char *);
void resrcwait(char *unused_char_p_t);
struct proc *runproc(void);
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
void unlock(spinlock_t *);
void userinit(void);
uint32_t userpc(void);
void validname(char *, int);
void validwstatname(char *);
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
int newfd(struct chan *c, int low_fd, int oflags, bool must_use_low);
struct chan *fdtochan(struct fd_table *fdt, int fd, int mode, int chkmnt,
                      int iref);
long kchanio(void *vc, void *buf, int n, int mode);
int openmode(uint32_t o);
void fdclose(struct fd_table *fdt, int fd);
int syschdir(char *path);
int sysfchdir(int fd);
int grpclose(struct fd_table *fdt, int fd);
int sysclose(int fd);
int syscreate(char *path, int mode, uint32_t perm);
int sysdup(int old, int low_fd, bool must_use_low);
int sys_dup_to(struct proc *from_proc, unsigned int from_fd,
               struct proc *to_proc, unsigned int to_fd);
int sysfstat(int fd, uint8_t*, int n);
int sysfstatakaros(int fd, struct kstat *);
char *sysfd2path(int fd);
char *sysgetcwd(void);
int sysfauth(int fd, char *aname);
int sysfversion(int fd, unsigned int msize, char *vers, unsigned int arglen);
int sysfwstat(int fd, uint8_t * buf, int n);
long bindmount(struct chan *c, char *old, int flag, char *spec);
int sysbind(char *new, char *old, int flags);
int syssymlink(char *new_path, char *old_path);
int sysmount(int fd, int afd, char *old, int flags, char *spec);
int sysunmount(char *old, char *new);
int sysopenat(int dirfd, char *path, int vfs_flags);
int sysopen(char *path, int vfs_flags);
long unionread(struct chan *c, void *va, long n);
void read_exactly_n(struct chan *c, void *vp, long n);
long sysread(int fd, void *va, long n);
long syspread(int fd, void *va, long n, int64_t off);
int sysremove(char *path);
int sysrename(char *from_path, char *to_path);
int64_t sysseek(int fd, int64_t off, int whence);
void validstat(uint8_t * s, int n, int slashok);
int sysstat(char *path, uint8_t*, int n);
int syslstat(char *path, uint8_t*, int n);
int sysstatakaros(char *path, struct kstat *, int flags);
long syswrite(int fd, void *va, long n);
long syspwrite(int fd, void *va, long n, int64_t off);
int syswstat(char *path, uint8_t * buf, int n);
struct dir *chandirstat(struct chan *c);
struct dir *sysdirstat(char *name);
struct dir *sysdirlstat(char *name);
struct dir *sysdirfstat(int fd);
int sysdirwstat(char *name, struct dir *dir);
int sysdirfwstat(int fd, struct dir *dir);
long sysdirread(int fd, struct kdirent **d);
int sysiounit(int fd);
void print_chaninfo(struct chan *ch);
int plan9setup(struct proc *new_proc, struct proc *parent, int flags);
int iseve(void);
int fd_getfl(int fd);
int fd_setfl(int fd, int flags);

/* kern/drivers/dev/srv.c */
char *srvname(struct chan *c);

/* kern/src/eipconv.c. Put them here or face real include hell. */
void printqid(void (*putch) (int, void **), void **putdat, struct qid *q);
void printcname(void (*putch) (int, void **), void **putdat, struct cname *c);
void printchan(void (*putch) (int, void **), void **putdat, struct chan *c);

/* kern/src/ns/util.c */
bool caller_is_username(char *uid);
bool caller_has_perms(char *fileuid, uint32_t perm, int omode);
bool caller_has_dir_perms(struct dir *dir, int omode);
void dir_perm_check(struct dir *dir, int omode);

static inline int abs(int a)
{
	if (a < 0)
		return -a;
	return a;
}

extern struct username eve;
extern unsigned int qiomaxatomic;

/* special sections */
#define __devtab  __attribute__((__section__(".devtab")))

#define DEVVARS_ENTRY(name, fmt)                                               \
struct dirtab __attribute__((__section__("devvars"))) __devvars_##name =       \
              {#name "!" fmt,                                                  \
               {(uint64_t)&(name), 0, QTFILE},                                 \
               sizeof((name)),                                                 \
               0444}
