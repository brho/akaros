/*
 * Storage Device.
 * From Interno.
 */

struct sdperm {
	char*	name;
	char*	user;
	uint32_t	perm;
};

struct sdpart {
	uint32_t	start;
	uint32_t	end;
	struct sdperm sdperm;
	int	valid;
	uint32_t	vers;
};

struct sdunit {
	struct sdev*	dev;
	int	subno;
	uint8_t	inquiry[256];		/* format follows SCSI spec */
	struct sdperm sdperm;

	qlock_t	ctl;
	uint32_t	sectors;
	uint32_t	secsize;
	struct sdpart*	part;			/* NULL or array of size npart */
	int	npart;
	uint32_t	vers;
	struct sdperm	ctlperm;

	qlock_t	raw;			/* raw read or write in progress */
	uint32_t	rawinuse;		/* really just a test-and-set */
	int	state;
	struct sdreq*	req;
	struct sdperm	rawperm;
};

/* 
 * Each controller is represented by a SDev.
 * Each controller is responsible for allocating its unit structures.
 * Each controller has at least one unit.
 */ 
struct sdev {
	struct kref	r;			/* Number of callers using device */
	struct sdifc*	ifc;			/* pnp/legacy */
	void*	ctlr;
	int	idno;
	char*	name;
	struct sdev*	next;

	qlock_t qlock;				/* enable/disable */
	int	enabled;
	int	nunit;			/* Number of units */
	qlock_t	unitlock;		/* `Loading' of units */
	int*	unitflg;		/* Unit flags */
	struct sdunit**unit;
};

struct sdifc {
	char*	name;

	struct sdev*	(*pnp)(void);
	struct sdev*	(*legacy)( int unused_int, int);
	struct sdev*	(*id)(struct sdev*);
	int	(*enable)(struct sdev*);
	int	(*disable)(struct sdev*);

	int	(*verify)(struct sdunit*);
	int	(*online)(struct sdunit*);
	int	(*rio)(struct sdreq*);
	int	(*rctl)(struct sdunit*, char *unused_char_p_t, int);
	int	(*wctl)(struct sdunit*, struct cmdbuf*);

	long	(*bio)(struct sdunit*, int unused_int, int, void*, long, long);
	struct sdev*	(*probe)(struct DevConf*);
	void	(*clear)(struct sdev*);
	char*	(*stat)(struct sdev*, char *unused_char_p_t, char*);
};

struct sdreq {
	struct sdunit*	unit;
	int	lun;
	int	write;
	uint8_t	cmd[16];
	int	clen;
	void*	data;
	int	dlen;

	int	flags;

	int	status;
	long	rlen;
	uint8_t	sense[256];
};

enum {
	SDnosense	= 0x00000001,
	SDvalidsense	= 0x00010000,
};

enum {
	SDretry		= -5,		/* internal to controllers */
	SDmalloc	= -4,
	SDeio		= -3,
	SDtimeout	= -2,
	SDnostatus	= -1,

	SDok		= 0,

	SDcheck		= 0x02,		/* check condition */
	SDbusy		= 0x08,		/* busy */

	SDmaxio		= 2048*1024,
	SDnpart		= 16,
};

#define sdmalloc(n)	kzmalloc(n, 0)
#define sdfree(p)	kfree(p)

/* sdscsi.c */
extern int scsiverify(struct sdunit*);
extern int scsionline(struct sdunit*);
extern long scsibio(struct sdunit*, int unused_int, int, void*, long, long);
extern struct sdev* scsiid(struct sdev*, struct sdifc*);
