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
	/* barf. Temporary. */
#ifndef IP_DF
	IP_DF = 0x4000,	/* v4: Don't fragment */
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
//#warning "need rendezvous!"
	//Rendez  cr;
	char cerr[ERRMAX];

	qlock_t listenq;
	/*    Rendez  */int listenr;

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
	struct kref ref;
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

/*
 * Internet Protocol Version 6
 *
 * rfc2460 defines the protocol, rfc2461 neighbour discovery, and
 * rfc2462 address autoconfiguration.  rfc4443 defines ICMP; was rfc2463.
 * rfc4291 defines the address architecture (including prefices), was rfc3513.
 * rfc4007 defines the scoped address architecture.
 *
 * global unicast is anything but unspecified (::), loopback (::1),
 * multicast (ff00::/8), and link-local unicast (fe80::/10).
 *
 * site-local (fec0::/10) is now deprecated, originally by rfc3879.
 *
 * Unique Local IPv6 Unicast Addresses are defined by rfc4193.
 * prefix is fc00::/7, scope is global, routing is limited to roughly a site.
 */
#define isv6mcast(addr)	  ((addr)[0] == 0xff)
#define islinklocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0x80)

#define optexsts(np)	(nhgets((np)->ploadlen) > 24)
#define issmcast(addr)	(memcmp((addr), v6solicitednode, 13) == 0)

#ifndef MIN
#define MIN(a, b) ((a) <= (b)? (a): (b))
#endif

enum {				/* Header Types */
	HBH		= 0,	/* hop-by-hop multicast routing protocol */
	ICMP		= 1,
	IGMP		= 2,
	GGP		= 3,
	IPINIP		= 4,
	ST		= 5,
	TCP		= 6,
	UDP		= 17,
	ISO_TP4		= 29,
	RH		= 43,
	FH		= 44,
	IDRP		= 45,
	RSVP		= 46,
	AH		= 51,
	ESP		= 52,
	ICMPv6		= 58,
	NNH		= 59,
	DOH		= 60,
	ISO_IP		= 80,
	IGRP		= 88,
	OSPF		= 89,

	Maxhdrtype	= 256,
};

enum {
	/* multicast flags and scopes */

//	Well_known_flg	= 0,
//	Transient_flg	= 1,

//	Interface_local_scop = 1,
	Link_local_scop	= 2,
//	Site_local_scop	= 5,
//	Org_local_scop	= 8,
	Global_scop	= 14,

	/* various prefix lengths */
	SOLN_PREF_LEN	= 13,

	/* icmpv6 unreachability codes */
	Icmp6_no_route		= 0,
	Icmp6_ad_prohib		= 1,
	Icmp6_out_src_scope	= 2,
	Icmp6_adr_unreach	= 3,
	Icmp6_port_unreach	= 4,
	Icmp6_gress_src_fail	= 5,
	Icmp6_rej_route		= 6,
	Icmp6_unknown		= 7,  /* our own invention for internal use */

	/* various flags & constants */
	v6MINTU		= 1280,
	HOP_LIMIT	= 255,
	IP6HDR		= 40,		/* sizeof(Ip6hdr) = 8 + 2*16 */

	/* option types */

	/* neighbour discovery */
	SRC_LLADDR	= 1,
	TARGET_LLADDR	= 2,
	PREFIX_INFO	= 3,
	REDIR_HEADER	= 4,
	MTU_OPTION	= 5,
	/* new since rfc2461; see iana.org/assignments/icmpv6-parameters */
	V6nd_home	= 8,
	V6nd_srcaddrs	= 9,		/* rfc3122 */
	V6nd_ip		= 17,
	/* /lib/rfc/drafts/draft-jeong-dnsop-ipv6-dns-discovery-12.txt */
	V6nd_rdns	= 25,
	/* plan 9 extensions */
	V6nd_9fs	= 250,
	V6nd_9auth	= 251,

	SRC_UNSPEC	= 0,
	SRC_UNI		= 1,
	TARG_UNI	= 2,
	TARG_MULTI	= 3,

	Tunitent	= 1,
	Tuniproxy	= 2,
	Tunirany	= 3,

	/* Node constants */
	MAX_MULTICAST_SOLICIT	= 3,
	RETRANS_TIMER		= 1000,
};

/* we do this in case there's padding at the end of Ip6hdr */
#define IPV6HDR \
	uint8_t	vcf[4];		/* version:4, traffic class:8, flow label:20 */\
	uint8_t	ploadlen[2];	/* payload length: packet length - 40 */ \
	uint8_t	proto;		/* next header type */ \
	uint8_t	ttl;		/* hop limit */ \
	uint8_t	src[IPaddrlen]; \
	uint8_t	dst[IPaddrlen]

struct	ip6hdr {
	IPV6HDR;
	uint8_t	payload[];
};

struct	Opthdr {		/* unused */
	uint8_t	nexthdr;
	uint8_t	len;
};

/*
 * Beware routing header type 0 (loose source routing); see
 * http://www.secdev.org/conf/IPv6_RH_security-csw07.pdf.
 * Type 1 is unused.  Type 2 is for MIPv6 (mobile IPv6) filtering
 * against type 0 header.
 */
struct	Routinghdr {		/* unused */
	uint8_t	nexthdr;
	uint8_t	len;
	uint8_t	rtetype;
	uint8_t	segrem;
};

struct	fraghdr6 {
	uint8_t	nexthdr;
	uint8_t	res;
	uint8_t	offsetRM[2];	/* Offset, Res, M flag */
	uint8_t	id[4];
};

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
