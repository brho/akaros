/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * Storage Device.
 */
struct devconf;
struct sdev;
struct sdifc;
struct sdio;
struct sdpart;
struct sdperm;
struct sdreq;
struct sdunit;

struct sdperm {
	char *name;
	char *user;
	uint32_t perm;
};

struct sdpart {
	uint64_t start;
	uint64_t end;
	struct sdperm sdperm;
	int valid;
	uint32_t vers;
};

struct sdunit {
	struct sdev *dev;
	int subno;
	unsigned char inquiry[255]; /* format follows SCSI spec */
	unsigned char sense[18];    /* format follows SCSI spec */
	struct sdperm sdperm;

	qlock_t ctl;
	uint64_t sectors;
	uint32_t secsize;
	struct sdpart *part; /* nil or array of size npart */
	int npart;
	uint32_t vers;
	struct sdperm ctlperm;

	qlock_t raw;       /* raw read or write in progress */
	uint32_t rawinuse; /* really just a test-and-set */
	int state;
	struct sdreq *req;
	struct sdperm rawperm;
};

/*
 * Each controller is represented by a struct sdev.
 */
struct sdev {
	struct kref r;     /* Number of callers using device */
	struct sdifc *ifc; /* pnp/legacy */
	void *ctlr;
	int idno;
	char name[8];
	struct sdev *next;

	qlock_t ql; /* enable/disable */
	int enabled;
	int nunit;        /* Number of units */
	qlock_t unitlock; /* `Loading' of units */
	int *unitflg;     /* Unit flags */
	struct sdunit **unit;
};

struct sdifc {
	char *name;

	struct sdev *(*pnp)(void);
	struct sdev *(*legacy)(int, int);
	int (*enable)(struct sdev *);
	int (*disable)(struct sdev *);

	int (*verify)(struct sdunit *);
	int (*online)(struct sdunit *);
	int (*rio)(struct sdreq *);
	int (*rctl)(struct sdunit *, char *, int);
	int (*wctl)(struct sdunit *, struct cmdbuf *);

	int32_t (*bio)(struct sdunit *, int, int, void *, int32_t, uint64_t);
	struct sdev *(*probe)(struct devconf *);
	void (*clear)(struct sdev *);
	char *(*rtopctl)(struct sdev *, char *, char *);
	int (*wtopctl)(struct sdev *, struct cmdbuf *);
};

struct sdreq {
	struct sdunit *unit;
	int lun;
	int write;
	unsigned char cmd[16];
	int clen;
	void *data;
	int dlen;

	int flags;

	int status;
	int32_t rlen;
	unsigned char sense[256];
};

enum {
	SDnosense = 0x00000001,
	SDvalidsense = 0x00010000,

	SDinq0periphqual = 0xe0,
	SDinq0periphtype = 0x1f,
	SDinq1removable = 0x80,

	/* periphtype values */
	SDperdisk = 0, /* Direct access (disk) */
	SDpertape = 1, /* Sequential eg, tape */
	SDperpr = 2,   /* Printer */
	SDperworm = 4, /* Worm */
	SDpercd = 5,   /* CD-ROM */
	SDpermo = 7,   /* rewriteable MO */
	SDperjuke = 8, /* medium-changer */
};

enum {
	SDretry = -5, /* internal to controllers */
	SDmalloc = -4,
	SDeio = -3,
	SDtimeout = -2,
	SDnostatus = -1,

	SDok = 0,

	SDcheck = 0x02, /* check condition */
	SDbusy = 0x08,  /* busy */

	SDmaxio = 2048 * 1024,
	SDnpart = 16,
};

/*
 * mmc/sd/sdio host controller interface
 */

struct sdio {
	char *name;
	int (*init)(void);
	void (*enable)(void);
	int (*inquiry)(char *, int);
	int (*cmd)(uint32_t, uint32_t, uint32_t *);
	void (*iosetup)(int, void *, int, int);
	void (*io)(int, unsigned char *, int);
};

extern struct sdio sdio;

/* devsd.c */
extern void sdadddevs(struct sdev *);
extern void sdaddconf(struct sdunit *);
extern void sdaddallconfs(void (*f)(struct sdunit *));
extern void sdaddpart(struct sdunit *, char *, uint64_t, uint64_t);
extern int sdsetsense(struct sdreq *, int, int, int, int);
extern int sdmodesense(struct sdreq *, unsigned char *, void *, int);
extern int sdfakescsi(struct sdreq *, void *, int);

/* sdscsi.c */
extern int scsiverify(struct sdunit *);
extern int scsionline(struct sdunit *);
extern int32_t scsibio(struct sdunit *, int, int, void *, int32_t, uint64_t);
extern struct sdev *scsiid(struct sdev *, struct sdifc *);

/*
 *  hardware info about a device
 */
struct devport {
	uint32_t port;
	int size;
};

struct devconf {
	uint32_t intnum;       /* interrupt number */
	char *type;            /* card type, malloced */
	int nports;            /* Number of ports */
	struct devport *ports; /* The ports themselves */
};
