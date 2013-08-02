/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */

struct path path;
struct chan chan;

/* linux has qstr, plan 9 has path. Path will need to go away
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


struct dev
{
    int	dc;
    char*	name;

    void	(*reset)(void);
    void	(*init)(void);
    void	(*shutdown)(void);
    struct chan*	(*attach)(char*path);
    struct walkqid*(*walk)(struct chan*from, struct chan*to, char**paths, int npath);
    long	(*stat)(struct chan*dir, uint8_t*path, long l);
    struct chan*	(*open)(struct chan*file, int mode);
    void	(*create)(struct chan*dir, char*path, int mode, int perm);
    void	(*close)(struct chan*chan);
    long	(*read)(struct chan*chan, void*p, long len, int64_t off);
    long	(*write)(struct chan*chan, void*p, long len, int64_t off);
    void	(*remove)(struct chan*chan);
    long	(*wstat)(struct chan*chan, uint8_t*new, long size);
    void	(*power)(int control);	/* power mgt: power(1) => on, power (0) => off */
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
    int	mountid;
    struct mount*	next;
    struct mhead*	head;
    struct ount*	copy;
    struct mount*	order;
    struct chan*	to;			/* channel replacing channel (mounted on in linux parlance) */
    int	mflag;
    char	*spec;
};

struct mhead
{
    struct kref kref;
    spinlock_t	lock;
    struct chan*	from;			/* channel mounted upon */
    struct mount*	mount;			/* what's mounted upon it */
    struct mhead*	hash;			/* Hash chain */
};

struct chan
{
    struct kref					kref;
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
    // make this a list Mhead*	umh;			/* mount point that derived struct chan; used in unionread */
    struct chan*	umc;			/* channel in union; held for union read */
    // need a queued lock not a spin lock. QLock	umqlock;		/* serialize unionreads */
    int	uri;			/* union read index */
    int	dri;			/* devdirread index */
    int	ismtpt;
    //Mntcache*mc;			/* Mount cache pointer */
    // think about this later. 	Mnt*	mux;			/* Mnt for clients using me for messages */
    union {
	void*	aux;
	struct qid	pgrpid;		/* for #p/notepg */
	unsigned long	mid;		/* for ns in devproc */
    };
    struct chan*	mchan;			/* channel to mounted server */
    struct qid	mqid;			/* qid of root of mount point */
    struct path	path;
};

