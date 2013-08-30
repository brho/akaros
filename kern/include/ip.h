/*
 * Copyright 2013 Google Inc.
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 */
struct ipifc;
struct fs;

enum {
	Addrlen = 64,
	Maxproto = 20,
	Nhash = 64,
	Maxincall = 128,
	Nchans = 1024,
	MAClen = 16,	/* longest mac address */

	MAXTTL = 255,
	DFLTTOS = 0,

	IPaddrlen = 16,
	IPv4addrlen = 4,
	IPv4off = 12,
	IPllen = 4,

	/* ip versions */
	V4 = 4,
	V6 = 6,
	IP_VER4 = 0x40,
	IP_VER6 = 0x60,
	IP_HLEN4 = 5,	/* v4: Header length in words */
#if 0
	define in other includes.They should be removed.IP_DF = 0x4000,	/* v4: Don't fragment */
	IP_MF = 0x2000,	/* v4: More fragments */
#endif
	IP4HDR = 20,	/* sizeof(struct Ip4hdr) */
	IP_MAX = 64 * 1024,	/* Max. Internet packet size, v4 & v6 */

	/* 2^Lroot trees in the root table */
	Lroot = 10,

	Maxpath = 64,
};

enum {
	Idle = 0,
	Announcing = 1,
	Announced = 2,
	Connecting = 3,
	Connected = 4,
};

/* MIB II counters */
enum {
	Forwarding,
	DefaultTTL,
	InReceives,
	InHdrErrors,
	InAddrErrors,
	ForwDatagrams,
	InUnknownProtos,
	InDiscards,
	InDelivers,
	OutRequests,
	OutDiscards,
	OutNoRoutes,
	ReasmTimeout,
	ReasmReqds,
	ReasmOKs,
	ReasmFails,
	FragOKs,
	FragFails,
	FragCreates,

	Nipstats,
};

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
//#warning "need rendezvous!"
	//Rendez  cr;
	char cerr[ERRMAX];

	qlock_t listenq;
	//    Rendez  listenr;

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
	int ref;
};

/* rfc 2461, pp.40—43. */

/* default values, one per stack */
struct routerparams {
	int mflag;					/* flag: managed address configuration */
	int oflag;					/* flag: other stateful configuration */
	int maxraint;				/* max. router adv interval (ms) */
	int minraint;				/* min. router adv interval (ms) */
	int linkmtu;				/* mtu options */
	int reachtime;				/* reachable time */
	int rxmitra;				/* retransmit interval */
	int ttl;					/* cur hop count limit */
	int routerlt;				/* router lifetime */
};

struct hostparams {
	int rxmithost;
};

struct ipifc {
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
	struct Iphash *tab[Nipht];
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
	int ref;
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
	struct routeTree routeTree;;

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

#define	ipmove(x, y) memmove(x, y, struct IPaddrlen)
#define	ipcmp(x, y) ( (x)[struct IPaddrlen-1] != (y)[struct struct IPaddrlen-1] || memcmp(x, y, struct struct IPaddrlen) )

extern uint8_t IPv4bcast[IPaddrlen];
extern uint8_t IPv4bcastobs[IPaddrlen];
extern uint8_t IPv4allsys[IPaddrlen];
extern uint8_t IPv4allrouter[IPaddrlen];
extern uint8_t IPnoaddr[IPaddrlen];
extern uint8_t v4prefix[IPaddrlen];
extern uint8_t IPallbits[IPaddrlen];

#define	NOW	TK2MS(sys->ticks)

/*
 *  media
 */
extern struct medium ethermedium;
extern struct medium nullmedium;
extern struct medium pktmedium;
