/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */

#define	VERSION9P	"9P2000"

#define	MAXWELEM	16

typedef
	struct Fcall {
	uint8_t type;
	uint32_t fid;
	uint16_t tag;
	union {
		struct {
			uint32_t msize;		/* Tversion, Rversion */
			char *version;		/* Tversion, Rversion */
		};
		struct {
			uint16_t oldtag;	/* Tflush */
		};
		struct {
			char *ename;		/* Rerror */
		};
		struct {
			struct qid qid;		/* Rattach, Ropen, Rcreate */
			uint32_t iounit;	/* Ropen, Rcreate */
		};
		struct {
			struct qid aqid;	/* Rauth */
		};
		struct {
			uint32_t afid;		/* Tauth, Tattach */
			char *uname;		/* Tauth, Tattach */
			char *aname;		/* Tauth, Tattach */
		};
		struct {
			uint32_t perm;		/* Tcreate */
			char *name;			/* Tcreate */
			uint8_t mode;		/* Tcreate, Topen */
		};
		struct {
			uint32_t newfid;	/* Twalk */
			uint16_t nwname;	/* Twalk */
			char *wname[MAXWELEM];	/* Twalk */
		};
		struct {
			uint16_t nwqid;		/* Rwalk */
			struct qid wqid[MAXWELEM];	/* Rwalk */
		};
		struct {
			int64_t offset;		/* Tread, Twrite */
			uint32_t count;		/* Tread, Twrite, Rread */
			char *data;			/* Twrite, Rread */
		};
		struct {
			uint16_t nstat;		/* Twstat, Rstat */
			uint8_t *stat;		/* Twstat, Rstat */
		};
	};
} Fcall;

#define	GBIT8(p)	((p)[0])
#define	GBIT16(p)	((p)[0]|((p)[1]<<8))
#define	GBIT32(p)	((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
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

unsigned int convM2S(uint8_t *, unsigned int, Fcall *);
unsigned int convS2M(Fcall *, uint8_t *, unsigned int);
unsigned int sizeS2M(Fcall *);

int statcheck(uint8_t * abuf, unsigned int nbuf);
unsigned int convM2D(uint8_t *, unsigned int, struct dir *, char *);
unsigned int convM2kdirent(uint8_t * buf, unsigned int nbuf,
						   struct kdirent *kd);
unsigned int convM2kstat(uint8_t * buf, unsigned int nbuf, struct kstat *ks);
unsigned int convD2M(struct dir *, uint8_t *, unsigned int);
unsigned int sizeD2M(struct dir *);

/*
 * at some point we want the plan 9 print. It's handy for
 * stuff like this.
 *
int	fcallfmt(Fmt*);
int	dirfmt(Fmt*);
int	dirmodefmt(Fmt*);
*/
int read9pmsg(int, void *, unsigned int);
