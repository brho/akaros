// INFERNO

#ifndef ROS_KERN_IP_H
#define ROS_KERN_IP_H

enum {
	Addrlen = 64,
	Maxproto = 20,
	Nhash = 64,
	Maxincall = 500,
	Nchans = 256,
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

/*
 *  one per conversation directory
 */
struct Proto;
struct conv {
	qlock_t qlock;

	int x;						/* conversation index */
	struct Proto *p;

	int restricted;				/* remote port is restricted */
	uint32_t ttl;				/* max time to live */
	uint32_t tos;				/* type of service */
	int ignoreadvice;			/* don't terminate connection on icmp errors */

	uint8_t ipversion;
	uint8_t laddr[IPaddrlen];	/* local IP address */
	uint8_t raddr[IPaddrlen];	/* remote IP address */
	uint16_t lport;				/* local port number */
	uint16_t rport;				/* remote port number */

	char *owner;				/* protections */
	int perm;
	int inuse;					/* opens of listen/data/ctl */
	int length;
	int state;

	/* udp specific */
	int headers;				/* data src/dst headers in udp */
	int reliable;				/* true if reliable udp */

	struct conv *incall;		/* calls waiting to be listened for */
	struct conv *next;

	struct queue *rq;			/* queued data waiting to be read */
	struct queue *wq;			/* queued data waiting to be written */
	struct queue *eq;			/* returned error packets */
	struct queue *sq;			/* snooping queue */
	atomic_t snoopers;			/* number of processes with snoop open */

	struct rendez cr;
	char cerr[ERRMAX];

	qlock_t listenq;
	struct rendez listenr;

	struct Ipmulti *multi;		/* multicast bindings for this interface */

	void *ptcl;					/* Protocol specific stuff */

	struct route *r;			/* last route used */
	uint32_t rgen;				/* routetable generation for *r */
};

struct Ipifc;
struct Fs;

struct medium {
	char *name;
	int hsize;					/* medium header size */
	int mintu;					/* default min mtu */
	int maxtu;					/* default max mtu */
	int maclen;					/* mac address length  */
	void (*bind) (struct Ipifc * unused_Ipifc, int unused_int,
				  char **unused_char_pp_t);
	void (*unbind) (struct Ipifc * unused_Ipifc);
	void (*bwrite) (struct Ipifc * ifc,
					struct block * b, int version, uint8_t * ip);

	/* for arming interfaces to receive multicast */
	void (*addmulti) (struct Ipifc * ifc, uint8_t * a, uint8_t * ia);
	void (*remmulti) (struct Ipifc * ifc, uint8_t * a, uint8_t * ia);

	/* process packets written to 'data' */
	void (*pktin) (struct Fs * f, struct Ipifc * ifc, struct block * bp);

	/* routes for router boards */
	void (*addroute) (struct Ipifc * ifc, int unused_int, uint8_t * u8p,
					  uint8_t *, uint8_t * u8p2, int);
	void (*remroute) (struct Ipifc * ifc, int i, uint8_t * u8p,
					  uint8_t * uu8p2);
	void (*flushroutes) (struct Ipifc * ifc);

	/* for routing multicast groups */
	void (*joinmulti) (struct Ipifc * ifc, uint8_t * a, uint8_t * ia);
	void (*leavemulti) (struct Ipifc * ifc, uint8_t * a, uint8_t * ia);

	/* address resolution */
	void (*ares) (struct Fs *, int unused_int, uint8_t * unused_uint8_p_t, uint8_t *, int, int);	/* resolve */
	void (*areg) (struct Ipifc * unused_Ipifc, uint8_t * unused_uint8_p_t);	/* register */

	/* v6 address generation */
	void (*pref2addr) (uint8_t * pref, uint8_t * ea);

	int unbindonclose;			/* if non-zero, unbind on last close */
};

/* logical interface associated with a physical one */
struct Iplifc {
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
	struct Iplifc *next;
};

/* binding twixt Ipself and Iplifc */
struct Iplink {
	struct Ipself *self;
	struct Iplifc *lifc;
	struct Iplink *selflink;	/* next link for this local address */
	struct Iplink *lifclink;	/* next link for this ifc */
	uint32_t expire;
	struct Iplink *next;		/* free list */
	struct kref ref;
};

/* rfc 2461, pp.40--43. */

/* default values, one per stack */
struct routerparams {
	int mflag;
	int oflag;
	int maxraint;
	int minraint;
	int linkmtu;
	int reachtime;
	int rxmitra;
	int ttl;
	int routerlt;
};

struct Ipifc {
	rwlock_t rwlock;

	struct conv *conv;			/* link to its conversation structure */
	char dev[64];				/* device we're attached to */
	struct medium *m;			/* Media pointer */
	int maxtu;					/* Maximum transfer unit */
	int mintu;					/* Minumum tranfer unit */
	int mbps;					/* megabits per second */
	void *arg;					/* medium specific */
	int reassemble;				/* reassemble IP packets before forwarding */

	/* these are used so that we can unbind on the fly */
	spinlock_t idlock;
	uint8_t ifcid;				/* incremented each 'bind/unbind/add/remove' */
	int ref;					/* number of proc's using this Ipifc */
	struct rendez wait;			/* where unbinder waits for ref == 0 */
	int unbinding;

	uint8_t mac[MAClen];		/* MAC address */

	struct Iplifc *lifc;		/* logical interfaces on this physical one */

	uint32_t in, out;			/* message statistics */
	uint32_t inerr, outerr;		/* ... */

	uint8_t sendra6;			/* == 1 => send router advs on this ifc */
	uint8_t recvra6;			/* == 1 => recv router advs on this ifc */
	struct routerparams rp;		/* router parameters as in RFC 2461, pp.40--43. 
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
struct Iphash {
	struct Iphash *next;
	struct conv *c;
	int match;
};

struct Iphash;
struct Ipht {
	spinlock_t lock;
	struct Iphash *tab[Nipht];
};
void iphtadd(struct Ipht *, struct conv *);
void iphtrem(struct Ipht *, struct conv *);
struct conv *iphtlook(struct Ipht *ht, uint8_t * sa, uint16_t sp, uint8_t * da,
					  uint16_t dp);

/*
 *  one per multiplexed Protocol
 */
struct Proto {
	qlock_t qlock;
	char *name;					/* protocol name */
	int x;						/* protocol index */
	int ipproto;				/* ip protocol type */

	char *(*connect) (struct conv *, char **unused_char_pp_t, int);
	char *(*announce) (struct conv *, char **unused_char_pp_t, int);
	char *(*bind) (struct conv *, char **unused_char_pp_t, int);
	int (*state) (struct conv *, char *unused_char_p_t, int);
	void (*create) (struct conv *);
	void (*close) (struct conv *);
	void (*rcv) (struct Proto *, struct Ipifc *, struct block *);
	char *(*ctl) (struct conv *, char **unused_char_pp_t, int);
	void (*advise) (struct Proto *, struct block *, char *unused_char_p_t);
	int (*stats) (struct Proto *, char *unused_char_p_t, int);
	int (*local) (struct conv *, char *unused_char_p_t, int);
	int (*remote) (struct conv *, char *unused_char_p_t, int);
	int (*inuse) (struct conv *);
	int (*gc) (struct Proto *);	/* returns true if any conversations are freed */
	void (*newconv) (struct Proto * udp, struct conv * conv);

	struct Fs *f;				/* file system this proto is part of */
	struct conv **conv;			/* array of conversations */
	int ptclsize;				/* size of per protocol ctl block */
	int nc;						/* number of conversations */
	int ac;
	struct qid qid;				/* qid for protocol directory */
	uint16_t nextport;
	uint16_t nextrport;

	void *priv;
};

/*
 *  Stream for sending packets to user level
 */
struct IProuter {
	qlock_t qlock;
	int opens;
	struct queue *q;
};

/*
 *  one per IP protocol stack
 */
struct Fs {
	rwlock_t rwlock;
	int dev;

	int np;
	struct Proto *p[Maxproto + 1];	/* list of supported protocols */
	struct Proto *t2p[256];		/* vector of all protocols */
	struct Proto *ipifc;		/* kludge for ipifcremroute & ipifcaddroute */
	struct Proto *ipmux;		/* kludge for finding an ip multiplexor */

	struct IP *ip;
	struct Ipselftab *self;
	struct arp *arp;
	struct V6params *v6p;
	struct IProuter iprouter;

	struct route *v4root[1 << Lroot];	/* v4 routing forest */
	struct route *v6root[1 << Lroot];	/* v6 routing forest */
	struct route *queue;		/* used as temp when reinjecting routes */

	struct Netlog *alog;
	struct Ifclog *ilog;

	char ndb[1024];				/* an ndb entry for this interface */
	int ndbvers;
	long ndbmtime;
};

/* one per default router known to host */
struct V6router {
	uint8_t inuse;
	struct Ipifc *ifc;
	int ifcid;
	uint8_t routeraddr[IPaddrlen];
	long ltorigin;
	struct routerparams rp;
};

struct hostparams {
	int rxmithost;
};

struct V6params {
	struct routerparams rp;		/* v6 params, one copy per node now */
	struct hostparams hp;
	struct V6router v6rlist[3];	/* max 3 default routers, currently */
	int cdrouter;				/* uses only v6rlist[cdrouter] if   */
	/* cdrouter >= 0. */
};

int Fsconnected(struct conv *, char *unused_char_p_t);
struct conv *Fsnewcall(struct conv *, uint8_t * unused_uint8_p_t, uint16_t,
					   uint8_t *, uint16_t, uint8_t unused_uint8_t);
int Fspcolstats(char *unused_char_p_t, int);
int Fsproto(struct Fs *, struct Proto *);
int Fsbuiltinproto(struct Fs *, uint8_t unused_uint8_t);
struct conv *Fsprotoclone(struct Proto *, char *unused_char_p_t);
struct Proto *Fsrcvpcol(struct Fs *, uint8_t unused_uint8_t);
struct Proto *Fsrcvpcolx(struct Fs *, uint8_t unused_uint8_t);
char *Fsstdconnect(struct conv *, char **unused_char_pp_t, int);
char *Fsstdannounce(struct conv *, char **unused_char_pp_t, int);
char *Fsstdbind(struct conv *, char **unused_char_pp_t, int);
uint32_t scalednconv(void);

/* 
 *  logging
 */
enum {
	Logip = 1 << 1,
	Logtcp = 1 << 2,
	Logfs = 1 << 3,
	Logil = 1 << 4,
	Logicmp = 1 << 5,
	Logudp = 1 << 6,
	Logcompress = 1 << 7,
	Logilmsg = 1 << 8,
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

void netloginit(struct Fs *);
void netlogopen(struct Fs *);
void netlogclose(struct Fs *);
void netlogctl(struct Fs *, char *unused_char_p_t, int);
long netlogread(struct Fs *, void *, uint32_t, long);
void netlog(struct Fs *, int unused_int, char *unused_char_p_t, ...);
void ifcloginit(struct Fs *);
long ifclogread(struct Fs *, struct chan *, void *, uint32_t, long);
void ifclog(struct Fs *, uint8_t *, int);
void ifclogopen(struct Fs *, struct chan *);
void ifclogclose(struct Fs *, struct chan *);

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

struct RouteTree {
	struct route *right;
	struct route *left;
	struct route *mid;
	uint8_t depth;
	uint8_t type;
	uint8_t ifcid;				/* must match ifc->id */
	struct Ipifc *ifc;
	char tag[4];
	struct kref kref;
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
	struct RouteTree rt;

	union {
		struct V6route v6;
		struct V4route v4;
	};
};
extern void v4addroute(struct Fs *f, char *tag, uint8_t * a, uint8_t * mask,
					   uint8_t * gate, int type);
extern void v6addroute(struct Fs *f, char *tag, uint8_t * a, uint8_t * mask,
					   uint8_t * gate, int type);
extern void v4delroute(struct Fs *f, uint8_t * a, uint8_t * mask, int dolock);
extern void v6delroute(struct Fs *f, uint8_t * a, uint8_t * mask, int dolock);
extern struct route *v4lookup(struct Fs *f, uint8_t * a, struct conv *c);
extern struct route *v6lookup(struct Fs *f, uint8_t * a, struct conv *c);
extern long routeread(struct Fs *f, char *unused_char_p_t, uint32_t, int);
extern long routewrite(struct Fs *f, struct chan *, char *unused_char_p_t, int);
extern void routetype(int unused_int, char *unused_char_p_t);
extern void ipwalkroutes(struct Fs *, struct routewalk *);
extern void convroute(struct route *r, uint8_t * u8pt, uint8_t * u8pt1,
					  uint8_t * u8pt2, char *unused_char_p_t, int *intp);

/*
 *  devip.c
 */

/*
 *  Hanging off every ip channel's ->aux is the following structure.
 *  It maintains the state used by devip and iproute.
 */
struct IPaux {
	char *owner;				/* the user that did the attach */
	char tag[4];
};

extern struct IPaux *newipaux(char *unused_char_p_t, char *);

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
	struct Ipifc *ifc;
	uint8_t ifcid;				/* must match ifc->id */
};

extern void arpinit(struct Fs *);
extern int arpread(struct arp *, char *unused_char_p_t, uint32_t, int);
extern int arpwrite(struct Fs *, char *unused_char_p_t, int);
extern struct arpent *arpget(struct arp *, struct block *bp, int version,
							 struct Ipifc *ifc, uint8_t * ip, uint8_t * h);
extern void arprelease(struct arp *, struct arpent *a);
extern struct block *arpresolve(struct arp *, struct arpent *a,
								struct medium *type, uint8_t * mac);
extern void arpenter(struct Fs *, int version, uint8_t * ip,
					 uint8_t * mac, int len, int norefresh);

/*
 * ipaux.c
 */

extern int myetheraddr(uint8_t * unused_uint8_p_t, char *unused_char_p_t);
extern uint32_t parseip(uint8_t * unused_uint8_p_t, char *unused_char_p_t);
extern uint32_t parseipmask(uint8_t * unused_uint8_p_t, char *unused_char_p_t);
extern char *v4parseip(uint8_t * unused_uint8_p_t, char *unused_char_p_t);
extern void maskip(uint8_t * from, uint8_t * mask, uint8_t * to);
extern int parsemac(uint8_t * to, char *from, int len);
extern uint8_t *defmask(uint8_t * unused_uint8_p_t);
extern int isv4(uint8_t * unused_uint8_p_t);
extern void v4tov6(uint8_t * v6, uint8_t * v4);
extern int v6tov4(uint8_t * v4, uint8_t * v6);
//extern int    eipfmt(Fmt*);

#define	ipmove(x, y) memmove(x, y, IPaddrlen)
#define	ipcmp(x, y) ( (x)[IPaddrlen-1] != (y)[IPaddrlen-1] || memcmp(x, y, IPaddrlen) )

extern uint8_t IPv4bcast[IPaddrlen];
extern uint8_t IPv4bcastobs[IPaddrlen];
extern uint8_t IPv4allsys[IPaddrlen];
extern uint8_t IPv4allrouter[IPaddrlen];
extern uint8_t IPnoaddr[IPaddrlen];
extern uint8_t v4prefix[IPaddrlen];
extern uint8_t IPallbits[IPaddrlen];

/*
 *  media
 */
extern struct medium ethermedium;
extern struct medium nullmedium;
extern struct medium pktmedium;
extern struct medium tripmedium;

/*
 *  ipifc.c
 */
extern struct medium *ipfindmedium(char *name);
extern void addipmedium(struct medium *med);
extern int ipforme(struct Fs *, uint8_t * addr);
extern int iptentative(struct Fs *, uint8_t * addr);
extern int ipisbm(uint8_t *);
extern int ipismulticast(uint8_t *);
extern struct Ipifc *findipifc(struct Fs *, uint8_t * remote, int type);
extern void findprimaryip(struct Fs *, uint8_t * unused_uint8_p_t);
extern void findlocalip(struct Fs *, uint8_t * local, uint8_t * remote);
extern int ipv4local(struct Ipifc *ifc, uint8_t * addr);
extern int ipv6local(struct Ipifc *ifc, uint8_t * addr);
extern int ipv6anylocal(struct Ipifc *ifc, uint8_t * addr);
extern struct Iplifc *iplocalonifc(struct Ipifc *ifc, uint8_t * ip);
extern int ipproxyifc(struct Fs *f, struct Ipifc *ifc, uint8_t * ip);
extern int ipismulticast(uint8_t * ip);
extern int ipisbooting(void);
extern int ipifccheckin(struct Ipifc *ifc, struct medium *med);
extern void ipifccheckout(struct Ipifc *ifc);
extern int ipifcgrab(struct Ipifc *ifc);
extern void ipifcaddroute(struct Fs *, int unused_int,
						  uint8_t * unused_uint8_p_t, uint8_t *, uint8_t *,
						  int);
extern void ipifcremroute(struct Fs *, int unused_int, uint8_t * u8pt,
						  uint8_t * u8pt2);
extern void ipifcremmulti(struct conv *c, uint8_t * ma, uint8_t * ia);
extern void ipifcaddmulti(struct conv *c, uint8_t * ma, uint8_t * ia);
extern char *ipifcrem(struct Ipifc *ifc, char **argv, int argc);
extern char *ipifcadd(struct Ipifc *ifc, char **argv, int argc, int tentative,
					  struct Iplifc *lifcp);
extern long ipselftabread(struct Fs *, char *a, uint32_t offset, int n);
extern char *ipifcaddpref6(struct Ipifc *ifc, char **argv, int argc);
extern void ipsendra6(struct Fs *f, int on);

/*
 *  ip.c
 */
extern void iprouting(struct Fs *, int);
extern void icmpnoconv(struct Fs *, struct block *);
extern void icmpcantfrag(struct Fs *, struct block *, int);
extern void icmpttlexceeded(struct Fs *, uint8_t * unused_uint8_p_t,
					struct block *);

uint16_t ipchecksum(uint8_t *addr, int len);
extern uint16_t ipcsum(uint8_t * unused_uint8_p_t);
extern void ipiput4(struct Fs *, struct Ipifc *unused_ipifc, struct block *);
extern void ipiput6(struct Fs *, struct Ipifc *unused_ipifc, struct block *);
extern int ipoput4(struct Fs *,
				   struct block *, int unused_int, int, int, struct conv *);
extern int ipoput6(struct Fs *,
				   struct block *, int unused_int, int, int, struct conv *);
extern int ipstats(struct Fs *, char *unused_char_p_t, int);
extern uint16_t ptclbsum(uint8_t * unused_uint8_p_t, int);
extern uint16_t ptclcsum(struct block *, int unused_int, int);
extern void ip_init(struct Fs *);
extern void update_mtucache(uint8_t * unused_uint8_p_t, uint32_t);
extern uint32_t restrict_mtu(uint8_t * unused_uint8_p_t, uint32_t);

/*
 * bootp.c
 */
char *(*bootp) (struct Ipifc * unused_ipifc);
int (*bootpread) (char *unused_char_p_t, uint32_t, int);

/*
 *  iprouter.c
 */
void useriprouter(struct Fs *, struct Ipifc *unused_ipifc, struct block *);
void iprouteropen(struct Fs *);
void iprouterclose(struct Fs *);
long iprouterread(struct Fs *, void *, int);

/*
 *  resolving inferno/plan9 differences
 */
struct chan *commonfdtochan(int unused_int, int, int, int);
char *commonuser(void);
char *commonerror(void);

/*
 * chandial.c
 */
extern struct chan *chandial(char *u1, char *u2, char *u3, struct chan **c);

/*
 *  global to all of the stack
 */
extern void (*igmpreportfn) (struct Ipifc * unused_ipifc,
							 uint8_t * unused_uint8_p_t);

/* IPV6 */
/* rfc 3513 defines the address prefices */
#define isv6mcast(addr)	  ((addr)[0] == 0xff)
#define islinklocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0x80)
#define issitelocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0xc0)
#define isv6global(addr) (((addr)[0] & 0xe0) == 0x20)

#define optexsts(np) (nhgets((np)->ploadlen) > 24)
#define issmcast(addr) (memcmp((addr), v6solicitednode, 13) == 0)

/* from RFC 2460 */

typedef struct Ip6hdr Ip6hdr;
typedef struct Opthdr Opthdr;
typedef struct Routinghdr Routinghdr;
typedef struct Fraghdr6 Fraghdr6;

struct ip6hdr {
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
};

struct Opthdr {
	uint8_t nexthdr;
	uint8_t len;
};

struct Routinghdr {
	uint8_t nexthdr;
	uint8_t len;
	uint8_t rtetype;
	uint8_t segrem;
};

struct fraghdr6 {
	uint8_t nexthdr;
	uint8_t res;
	uint8_t offsetRM[2];		// Offset, Res, M flag
	uint8_t id[4];
};

enum {							/* Header Types */
	HBH = 0,					//?
	ICMP = 1,
	IGMP = 2,
	GGP = 3,
	IPINIP = 4,
	ST = 5,
	TCP = 6,
	UDP = 17,
	ISO_TP4 = 29,
	RH = 43,
	FH = 44,
	IDRP = 45,
	RSVP = 46,
	AH = 51,
	ESP = 52,
	ICMPv6 = 58,
	NNH = 59,
	DOH = 60,
	ISO_IP = 80,
	IGRP = 88,
	OSPF = 89,

	Maxhdrtype = 256,
};

enum {
	//  multicast flgs and scop

	well_known_flg = 0,
	transient_flg = 1,

	node_local_scop = 1,
	link_local_scop = 2,
	site_local_scop = 5,
	org_local_scop = 8,
	global_scop = 14,

	//  various prefix lengths

	SOLN_PREF_LEN = 13,

	//  icmpv6 unreach codes
	icmp6_no_route = 0,
	icmp6_ad_prohib = 1,
	icmp6_unassigned = 2,
	icmp6_adr_unreach = 3,
	icmp6_port_unreach = 4,
	icmp6_unkn_code = 5,

	//  various flags & constants

	v6MINTU = 1280,
	HOP_LIMIT = 255,
	ETHERHDR_LEN = 14,
	IPV6HDR_LEN = 40,
	IPV4HDR_LEN = 20,

	//  option types

	SRC_LLADDRESS = 1,
	TARGET_LLADDRESS = 2,
	PREFIX_INFO = 3,
	REDIR_HEADER = 4,
	MTU_OPTION = 5,

	SRC_UNSPEC = 0,
	SRC_UNI = 1,
	TARG_UNI = 2,
	TARG_MULTI = 3,

	t_unitent = 1,
	t_uniproxy = 2,
	t_unirany = 3,

	//  Router constants (all times in milliseconds)

	MAX_INITIAL_RTR_ADVERT_INTERVAL = 16000,
	MAX_INITIAL_RTR_ADVERTISEMENTS = 3,
	MAX_FINAL_RTR_ADVERTISEMENTS = 3,
	MIN_DELAY_BETWEEN_RAS = 3000,
	MAX_RA_DELAY_TIME = 500,

	//  Host constants

	MAX_RTR_SOLICITATION_DELAY = 1000,
	RTR_SOLICITATION_INTERVAL = 4000,
	MAX_RTR_SOLICITATIONS = 3,

	//  Node constants

	MAX_MULTICAST_SOLICIT = 3,
	MAX_UNICAST_SOLICIT = 3,
	MAX_ANYCAST_DELAY_TIME = 1000,
	MAX_NEIGHBOR_ADVERTISEMENT = 3,
	REACHABLE_TIME = 30000,
	RETRANS_TIMER = 1000,
	DELAY_FIRST_PROBE_TIME = 5000,

};

extern void ipv62smcast(uint8_t *, uint8_t *);
extern void icmpns(struct Fs *f, uint8_t * src, int suni, uint8_t * targ,
				   int tuni, uint8_t * mac);
extern void icmpna(struct Fs *f, uint8_t * src, uint8_t * dst, uint8_t * targ,
				   uint8_t * mac, uint8_t flags);
extern void icmpttlexceeded6(struct Fs *f, struct Ipifc *ifc, struct block *bp);
extern void icmppkttoobig6(struct Fs *f, struct Ipifc *ifc, struct block *bp);
extern void icmphostunr(struct Fs *f,
						struct Ipifc *ifc,
						struct block *bp, int code, int free);

extern uint8_t v6allnodesN[IPaddrlen];
extern uint8_t v6allnodesL[IPaddrlen];
extern uint8_t v6allroutersN[IPaddrlen];
extern uint8_t v6allroutersL[IPaddrlen];
extern uint8_t v6allnodesNmask[IPaddrlen];
extern uint8_t v6allnodesLmask[IPaddrlen];
extern uint8_t v6allroutersS[IPaddrlen];
extern uint8_t v6solicitednode[IPaddrlen];
extern uint8_t v6solicitednodemask[IPaddrlen];
extern uint8_t v6Unspecified[IPaddrlen];
extern uint8_t v6loopback[IPaddrlen];
extern uint8_t v6loopbackmask[IPaddrlen];
extern uint8_t v6linklocal[IPaddrlen];
extern uint8_t v6linklocalmask[IPaddrlen];
extern uint8_t v6sitelocal[IPaddrlen];
extern uint8_t v6sitelocalmask[IPaddrlen];
extern uint8_t v6glunicast[IPaddrlen];
extern uint8_t v6multicast[IPaddrlen];
extern uint8_t v6multicastmask[IPaddrlen];

extern int v6llpreflen;
extern int v6slpreflen;
extern int v6lbpreflen;
extern int v6mcpreflen;
extern int v6snpreflen;
extern int v6aNpreflen;
extern int v6aLpreflen;

extern int ReTransTimer;

int kdial(char *dest, char *local, char *dir, int *cfdp);

/* network interfaces and ethernet */
// INFERNO

enum {
	Nmaxaddr = 64,
	Nmhash = 31,

	Ncloneqid = 1,
	Naddrqid,
	N2ndqid,
	N3rdqid,
	Ndataqid,
	Nctlqid,
	Nstatqid,
	Ntypeqid,
	Nifstatqid,
};

/*
 *  Macros to manage Qid's used for multiplexed devices
 */
#define NETTYPE(x)	(((uint32_t)x)&0x1f)
/* The net's ID + 1 is stored starting at 1 << 5.  So ID 0 = 32, ID 1 = 64, and
 * NETID == -1 means no netid */
#define NETID(x)	(((uint32_t)(x) >> 5) - 1)
#define NETQID(i,t)	((((uint32_t)(i) + 1) << 5) | (t))

/*
 *  one per multiplexed connection
 */
struct netfile {
	qlock_t qlock;

	int inuse;
	uint32_t mode;
	char owner[KNAMELEN];

	int type;					/* multiplexor type */
	int prom;					/* promiscuous mode */
	int scan;					/* base station scanning interval */
	int bridge;					/* bridge mode */
	int headersonly;			/* headers only - no data */
	uint8_t maddr[8];			/* bitmask of multicast addresses requested */
	int nmaddr;					/* number of multicast addresses */

	struct queue *in;			/* input buffer */
};

/*
 *  a network address
 */
struct netaddr {
	struct netaddr *next;		/* allocation chain */
	struct netaddr *hnext;
	uint8_t addr[Nmaxaddr];
	int ref;					/* leaving this as an int, not a kref.  no reaping, yet. */
};

/*
 *  a network interface
 */
struct netif {
	qlock_t qlock;

	/* multiplexing */
	char name[KNAMELEN];		/* for top level directory */
	int nfile;					/* max number of Netfiles */
	struct netfile **f;

	/* about net */
	int limit;					/* flow control */
	int alen;					/* address length */
	int mbps;					/* megabits per sec */
	int link;					/* link status */
	uint8_t addr[Nmaxaddr];
	uint8_t bcast[Nmaxaddr];
	struct netaddr *maddr;		/* known multicast addresses */
	int nmaddr;					/* number of known multicast addresses */
	struct netaddr *mhash[Nmhash];	/* hash table of multicast addresses */
	int prom;					/* number of promiscuous opens */
	int scan;					/* number of base station scanners */
	int all;					/* number of -1 multiplexors */

	/* statistics */
	int misses;
	int inpackets;
	int outpackets;
	int crcs;					/* input crc errors */
	int oerrs;					/* output errors */
	int frames;					/* framing errors */
	int overflows;				/* packet overflows */
	int buffs;					/* buffering errors */
	int soverflows;				/* software overflow */

	/* routines for touching the hardware */
	void *arg;
	void (*promiscuous) (void *, int);
	void (*multicast) (void *, uint8_t * unused_uint8_p_t, int);
	void (*scanbs) (void *, unsigned nt);	/* scan for base stations */
};

void netifinit(struct netif *, char *, int, uint32_t);
struct walkqid *netifwalk(struct netif *, struct chan *, struct chan *, char **,
						  int);
struct chan *netifopen(struct netif *, struct chan *, int);
void netifclose(struct netif *, struct chan *);
long netifread(struct netif *, struct chan *, void *, long, uint32_t);
struct block *netifbread(struct netif *, struct chan *, long, uint32_t);
long netifwrite(struct netif *, struct chan *, void *, long);
int netifwstat(struct netif *, struct chan *, uint8_t *, int);
int netifstat(struct netif *, struct chan *, uint8_t *, int);
int activemulti(struct netif *, uint8_t *, int);

/*
 *  Ethernet specific
 */
enum {
	Eaddrlen = 6,
	ETHERMINTU = 60,	/* minimum transmit size */
	ETHERMAXTU = 1514,	/* maximum transmit size */
	ETHERHDRSIZE = 14,	/* size of an ethernet header */
};

struct etherpkt {
	uint8_t d[Eaddrlen];
	uint8_t s[Eaddrlen];
	uint8_t type[2];
	uint8_t data[1500];
};
// INFERNO
enum {
	MaxEther = 32,
	MaxFID = 16,
	Ntypes = 8,
};

struct ether {
	rwlock_t rwlock;
	int ctlrno;
	char *type;
	int irq;
	unsigned int tbdf;
	int port;
	int minmtu;
	int maxmtu;
	uint8_t ea[Eaddrlen];
	int encry;

	void (*attach) (struct ether *);	/* filled in by reset routine */
	void (*closed) (struct ether *);
	void (*detach) (struct ether *);
	void (*transmit) (struct ether *);
	long (*ifstat) (struct ether *, void *, long, uint32_t);
	long (*ctl) (struct ether *, void *, long);	/* custom ctl messages */
	void (*power) (struct ether *, int);	/* power on/off */
	void (*shutdown) (struct ether *);	/* shutdown hardware before reboot */
	void *ctlr;
	int pcmslot;				/* PCMCIA */
	int fullduplex;				/* non-zero if full duplex */
	int vlanid;					/* non-zero if vlan */

	struct queue *oq;

	qlock_t vlq;				/* array change */
	int nvlan;
	struct ether *vlans[MaxFID];

	/* another case where we wish we had anon struct members. */
	struct netif netif;
};

extern struct block *etheriq(struct ether *, struct block *, int);
extern void addethercard(char *unused_char_p_t, int (*)(struct ether *));
extern int archether(int unused_int, struct ether *);

#define NEXT_RING(x, len) (((x) + 1) % (len))
#define PREV_RING(x, len) (((x) == 0) ? (len) - 1: (x) - 1)

#endif /* ROS_KERN_IP_H */
