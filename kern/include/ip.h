//INFERNO

typedef struct	Conv	Conv;
typedef struct	Fs	Fs;
typedef union	Hwaddr	Hwaddr;
typedef struct	IP	IP;
typedef struct	IPaux	IPaux;
typedef struct	Ipself	Ipself;
typedef struct	Ipselftab	Ipselftab;
typedef struct	Iplink	Iplink;
typedef struct	Iplifc	Iplifc;
typedef struct	Ipmulti	Ipmulti;
typedef struct	IProuter IProuter;
typedef struct	Ipifc	Ipifc;
typedef struct	Iphash	Iphash;
typedef struct	Ipht	Ipht;
typedef struct	Netlog	Netlog;
typedef struct	Ifclog	Ifclog;
typedef struct	Medium	Medium;
typedef struct	Proto	Proto;
typedef struct	Arpent	Arpent;
typedef struct	Arp Arp;
typedef struct	Route	Route;

typedef struct	Routerparams	Routerparams;
typedef struct 	Hostparams	Hostparams;
typedef struct 	V6router	V6router;
typedef struct	V6params	V6params;

#pragma incomplete Arp
#pragma	incomplete Ifclog
#pragma incomplete Ipself
#pragma incomplete Ipselftab
#pragma incomplete IP
#pragma incomplete Netlog

enum
{
	Addrlen=	64,
	Maxproto=	20,
	Nhash=		64,
	Maxincall=	5,
	Nchans=		256,
	MAClen=		16,		/* longest mac address */

	MAXTTL=		255,
	DFLTTOS=	0,

	IPaddrlen=	16,
	IPv4addrlen=	4,
	IPv4off=	12,
	IPllen=		4,

	/* ip versions */
	V4=		4,
	V6=		6,
	IP_VER4= 	0x40,
	IP_VER6=	0x60,

	/* 2^Lroot trees in the root table */
	Lroot=		10,

	Maxpath =	64,
};

enum
{
	Idle=		0,
	Announcing=	1,
	Announced=	2,
	Connecting=	3,
	Connected=	4,
};

/*
 *  one per conversation directory
 */
struct Conv
{
	QLock;

	int	x;			/* conversation index */
	Proto*	p;

	int	restricted;		/* remote port is restricted */
	uint	ttl;			/* max time to live */
	uint	tos;			/* type of service */
	int	ignoreadvice;		/* don't terminate connection on icmp errors */

	uchar	ipversion;
	uchar	laddr[IPaddrlen];	/* local IP address */
	uchar	raddr[IPaddrlen];	/* remote IP address */
	ushort	lport;			/* local port number */
	ushort	rport;			/* remote port number */

	char	*owner;			/* protections */
	int	perm;
	int	inuse;			/* opens of listen/data/ctl */
	int	length;
	int	state;

	/* udp specific */
	int	headers;		/* data src/dst headers in udp */
	int	reliable;		/* true if reliable udp */

	Conv*	incall;			/* calls waiting to be listened for */
	Conv*	next;

	Queue*	rq;			/* queued data waiting to be read */
	Queue*	wq;			/* queued data waiting to be written */
	Queue*	eq;			/* returned error packets */
	Queue*	sq;			/* snooping queue */
	Ref	snoopers;		/* number of processes with snoop open */

	Rendez	cr;
	char	cerr[ERRMAX];

	QLock	listenq;
	Rendez	listenr;

	Ipmulti	*multi;			/* multicast bindings for this interface */

	void*	ptcl;			/* protocol specific stuff */

	Route	*r;			/* last route used */
	ulong	rgen;			/* routetable generation for *r */
};

struct Medium
{
	char	*name;
	int	hsize;		/* medium header size */
	int	mintu;		/* default min mtu */
	int	maxtu;		/* default max mtu */
	int	maclen;		/* mac address length  */
	void	(*bind)(Ipifc*, int, char**);
	void	(*unbind)(Ipifc*);
	void	(*bwrite)(Ipifc *ifc, Block *b, int version, uchar *ip);

	/* for arming interfaces to receive multicast */
	void	(*addmulti)(Ipifc *ifc, uchar *a, uchar *ia);
	void	(*remmulti)(Ipifc *ifc, uchar *a, uchar *ia);

	/* process packets written to 'data' */
	void	(*pktin)(Fs *f, Ipifc *ifc, Block *bp);

	/* routes for router boards */
	void	(*addroute)(Ipifc *ifc, int, uchar*, uchar*, uchar*, int);
	void	(*remroute)(Ipifc *ifc, int, uchar*, uchar*);
	void	(*flushroutes)(Ipifc *ifc);

	/* for routing multicast groups */
	void	(*joinmulti)(Ipifc *ifc, uchar *a, uchar *ia);
	void	(*leavemulti)(Ipifc *ifc, uchar *a, uchar *ia);

	/* address resolution */
	void	(*ares)(Fs*, int, uchar*, uchar*, int, int);	/* resolve */
	void	(*areg)(Ipifc*, uchar*);			/* register */

	/* v6 address generation */
	void	(*pref2addr)(uchar *pref, uchar *ea);

	int	unbindonclose;	/* if non-zero, unbind on last close */
};

/* logical interface associated with a physical one */
struct Iplifc
{
	uchar	local[IPaddrlen];
	uchar	mask[IPaddrlen];
	uchar	remote[IPaddrlen];
	uchar	net[IPaddrlen];
	uchar	tentative;	/* =1 => v6 dup disc on, =0 => confirmed unique */
	uchar	onlink;		/* =1 => onlink, =0 offlink. */
	uchar	autoflag;	/* v6 autonomous flag */
	long 	validlt;	/* v6 valid lifetime */
	long 	preflt;		/* v6 preferred lifetime */
	long	origint;	/* time when addr was added */
	Iplink	*link;		/* addresses linked to this lifc */
	Iplifc	*next;
};

/* binding twixt Ipself and Iplifc */
struct Iplink
{
	Ipself	*self;
	Iplifc	*lifc;
	Iplink	*selflink;	/* next link for this local address */
	Iplink	*lifclink;	/* next link for this ifc */
	ulong	expire;
	Iplink	*next;		/* free list */
	int	ref;
};

/* rfc 2461, pp.40--43. */

/* default values, one per stack */
struct Routerparams {
	int	mflag;
	int	oflag;
	int 	maxraint;
	int	minraint;
	int	linkmtu;
	int	reachtime;
	int	rxmitra;
	int	ttl;
	int	routerlt;	
};

struct Hostparams {
	int	rxmithost;
};

struct Ipifc
{
	RWlock;
	
	Conv	*conv;		/* link to its conversation structure */
	char	dev[64];	/* device we're attached to */
	Medium	*m;		/* Media pointer */
	int	maxtu;		/* Maximum transfer unit */
	int	mintu;		/* Minumum tranfer unit */
	int	mbps;		/* megabits per second */
	void	*arg;		/* medium specific */
	int	reassemble;	/* reassemble IP packets before forwarding */

	/* these are used so that we can unbind on the fly */
	Lock	idlock;
	uchar	ifcid;		/* incremented each 'bind/unbind/add/remove' */
	int	ref;		/* number of proc's using this ipifc */
	Rendez	wait;		/* where unbinder waits for ref == 0 */
	int	unbinding;

	uchar	mac[MAClen];	/* MAC address */

	Iplifc	*lifc;		/* logical interfaces on this physical one */

	ulong	in, out;	/* message statistics */
	ulong	inerr, outerr;	/* ... */

	uchar	sendra6;	/* == 1 => send router advs on this ifc	*/
	uchar	recvra6;	/* == 1 => recv router advs on this ifc */
	Routerparams rp;	/* router parameters as in RFC 2461, pp.40--43. 
					used only if node is router */
};

/*
 *  one per multicast-lifc pair used by a Conv
 */
struct Ipmulti
{
	uchar	ma[IPaddrlen];
	uchar	ia[IPaddrlen];
	Ipmulti	*next;
};

/*
 *  hash table for 2 ip addresses + 2 ports
 */
enum
{
	Nipht=		521,	/* convenient prime */

	IPmatchexact=	0,	/* match on 4 tuple */
	IPmatchany,		/* *!* */
	IPmatchport,		/* *!port */
	IPmatchaddr,		/* addr!* */
	IPmatchpa,		/* addr!port */
};
struct Iphash
{
	Iphash	*next;
	Conv	*c;
	int	match;
};
struct Ipht
{
	Lock;
	Iphash	*tab[Nipht];
};
void iphtadd(Ipht*, Conv*);
void iphtrem(Ipht*, Conv*);
Conv* iphtlook(Ipht *ht, uchar *sa, ushort sp, uchar *da, ushort dp);

/*
 *  one per multiplexed protocol
 */
struct Proto
{
	QLock;
	char*		name;		/* protocol name */
	int		x;		/* protocol index */
	int		ipproto;	/* ip protocol type */

	char*		(*connect)(Conv*, char**, int);
	char*		(*announce)(Conv*, char**, int);
	char*		(*bind)(Conv*, char**, int);
	int		(*state)(Conv*, char*, int);
	void		(*create)(Conv*);
	void		(*close)(Conv*);
	void		(*rcv)(Proto*, Ipifc*, Block*);
	char*		(*ctl)(Conv*, char**, int);
	void		(*advise)(Proto*, Block*, char*);
	int		(*stats)(Proto*, char*, int);
	int		(*local)(Conv*, char*, int);
	int		(*remote)(Conv*, char*, int);
	int		(*inuse)(Conv*);
	int		(*gc)(Proto*);	/* returns true if any conversations are freed */

	Fs		*f;		/* file system this proto is part of */
	Conv		**conv;		/* array of conversations */
	int		ptclsize;	/* size of per protocol ctl block */
	int		nc;		/* number of conversations */
	int		ac;
	Qid		qid;		/* qid for protocol directory */
	ushort		nextport;
	ushort		nextrport;

	void		*priv;
};

/*
 *  Stream for sending packets to user level
 */
struct IProuter {
	QLock;
	int	opens;
	Queue	*q;
};

/*
 *  one per IP protocol stack
 */
struct Fs
{
	RWlock;
	int	dev;

	int	np;
	Proto*	p[Maxproto+1];		/* list of supported protocols */
	Proto*	t2p[256];		/* vector of all protocols */
	Proto*	ipifc;			/* kludge for ipifcremroute & ipifcaddroute */
	Proto*	ipmux;			/* kludge for finding an ip multiplexor */

	IP	*ip;
	Ipselftab	*self;
	Arp	*arp;
	V6params	*v6p;
	IProuter iprouter;

	Route	*v4root[1<<Lroot];	/* v4 routing forest */
	Route	*v6root[1<<Lroot];	/* v6 routing forest */
	Route	*queue;			/* used as temp when reinjecting routes */

	Netlog	*alog;
	Ifclog	*ilog;

	char	ndb[1024];		/* an ndb entry for this interface */
	int	ndbvers;
	long	ndbmtime;
};

/* one per default router known to host */
struct V6router {
	uchar	inuse;
	Ipifc	*ifc;
	int	ifcid;
	uchar	routeraddr[IPaddrlen];
	long	ltorigin;
	Routerparams	rp;
};

struct V6params
{
	Routerparams	rp;		/* v6 params, one copy per node now */
	Hostparams	hp;
	V6router	v6rlist[3];	/* max 3 default routers, currently */
	int		cdrouter;	/* uses only v6rlist[cdrouter] if   */ 
					/* cdrouter >= 0. */
};


int	Fsconnected(Conv*, char*);
Conv*	Fsnewcall(Conv*, uchar*, ushort, uchar*, ushort, uchar);
int	Fspcolstats(char*, int);
int	Fsproto(Fs*, Proto*);
int	Fsbuiltinproto(Fs*, uchar);
Conv*	Fsprotoclone(Proto*, char*);
Proto*	Fsrcvpcol(Fs*, uchar);
Proto*	Fsrcvpcolx(Fs*, uchar);
char*	Fsstdconnect(Conv*, char**, int);
char*	Fsstdannounce(Conv*, char**, int);
char*	Fsstdbind(Conv*, char**, int);
ulong	scalednconv(void);

/* 
 *  logging
 */
enum
{
	Logip=		1<<1,
	Logtcp=		1<<2,
	Logfs=		1<<3,
	Logil=		1<<4,
	Logicmp=	1<<5,
	Logudp=		1<<6,
	Logcompress=	1<<7,
	Logilmsg=	1<<8,
	Loggre=		1<<9,
	Logppp=		1<<10,
	Logtcprxmt=	1<<11,
	Logigmp=	1<<12,
	Logudpmsg=	1<<13,
	Logipmsg=	1<<14,
	Logrudp=	1<<15,
	Logrudpmsg=	1<<16,
	Logesp=		1<<17,
	Logtcpwin=	1<<18,
};

void	netloginit(Fs*);
void	netlogopen(Fs*);
void	netlogclose(Fs*);
void	netlogctl(Fs*, char*, int);
long	netlogread(Fs*, void*, ulong, long);
void	netlog(Fs*, int, char*, ...);
void	ifcloginit(Fs*);
long	ifclogread(Fs*, Chan *,void*, ulong, long);
void	ifclog(Fs*, uchar *, int);
void	ifclogopen(Fs*, Chan*);
void	ifclogclose(Fs*, Chan*);

/*
 *  iproute.c
 */
typedef	struct RouteTree RouteTree;
typedef struct Routewalk Routewalk;
typedef struct V4route V4route;
typedef struct V6route V6route;

enum
{

	/* type bits */
	Rv4=		(1<<0),		/* this is a version 4 route */
	Rifc=		(1<<1),		/* this route is a directly connected interface */
	Rptpt=		(1<<2),		/* this route is a pt to pt interface */
	Runi=		(1<<3),		/* a unicast self address */
	Rbcast=		(1<<4),		/* a broadcast self address */
	Rmulti=		(1<<5),		/* a multicast self address */
	Rproxy=		(1<<6),		/* this route should be proxied */
};

struct Routewalk
{
	int	o;
	int	h;
	char*	p;
	char*	e;
	void*	state;
	void	(*walk)(Route*, Routewalk*);
};

struct	RouteTree
{
	Route*	right;
	Route*	left;
	Route*	mid;
	uchar	depth;
	uchar	type;
	uchar	ifcid;		/* must match ifc->id */
	Ipifc	*ifc;
	char	tag[4];
	int	ref;
};

struct V4route
{
	ulong	address;
	ulong	endaddress;
	uchar	gate[IPv4addrlen];
};

struct V6route
{
	ulong	address[IPllen];
	ulong	endaddress[IPllen];
	uchar	gate[IPaddrlen];
};

struct Route
{
	RouteTree;

	union {
		V6route	v6;
		V4route v4;
	};
};
extern void	v4addroute(Fs *f, char *tag, uchar *a, uchar *mask, uchar *gate, int type);
extern void	v6addroute(Fs *f, char *tag, uchar *a, uchar *mask, uchar *gate, int type);
extern void	v4delroute(Fs *f, uchar *a, uchar *mask, int dolock);
extern void	v6delroute(Fs *f, uchar *a, uchar *mask, int dolock);
extern Route*	v4lookup(Fs *f, uchar *a, Conv *c);
extern Route*	v6lookup(Fs *f, uchar *a, Conv *c);
extern long	routeread(Fs *f, char*, ulong, int);
extern long	routewrite(Fs *f, Chan*, char*, int);
extern void	routetype(int, char*);
extern void	ipwalkroutes(Fs*, Routewalk*);
extern void	convroute(Route*, uchar*, uchar*, uchar*, char*, int*);

/*
 *  devip.c
 */

/*
 *  Hanging off every ip channel's ->aux is the following structure.
 *  It maintains the state used by devip and iproute.
 */
struct IPaux
{
	char	*owner;		/* the user that did the attach */
	char	tag[4];
};

extern IPaux*	newipaux(char*, char*);

/*
 *  arp.c
 */
struct Arpent
{
	uchar	ip[IPaddrlen];
	uchar	mac[MAClen];
	Medium	*type;			/* media type */
	Arpent*	hash;
	Block*	hold;
	Block*	last;
	uint	ctime;			/* time entry was created or refreshed */
	uint	utime;			/* time entry was last used */
	uchar	state;
	Arpent	*nextrxt;		/* re-transmit chain */
	uint	rtime;			/* time for next retransmission */
	uchar	rxtsrem;
	Ipifc	*ifc;
	uchar	ifcid;			/* must match ifc->id */
};

extern void	arpinit(Fs*);
extern int	arpread(Arp*, char*, ulong, int);
extern int	arpwrite(Fs*, char*, int);
extern Arpent*	arpget(Arp*, Block *bp, int version, Ipifc *ifc, uchar *ip, uchar *h);
extern void	arprelease(Arp*, Arpent *a);
extern Block*	arpresolve(Arp*, Arpent *a, Medium *type, uchar *mac);
extern void	arpenter(Fs*, int version, uchar *ip, uchar *mac, int len, int norefresh);

/*
 * ipaux.c
 */

extern int	myetheraddr(uchar*, char*);
extern ulong	parseip(uchar*, char*);
extern ulong	parseipmask(uchar*, char*);
extern char*	v4parseip(uchar*, char*);
extern void	maskip(uchar *from, uchar *mask, uchar *to);
extern int	parsemac(uchar *to, char *from, int len);
extern uchar*	defmask(uchar*);
extern int	isv4(uchar*);
extern void	v4tov6(uchar *v6, uchar *v4);
extern int	v6tov4(uchar *v4, uchar *v6);
extern int	eipfmt(Fmt*);

#define	ipmove(x, y) memmove(x, y, IPaddrlen)
#define	ipcmp(x, y) ( (x)[IPaddrlen-1] != (y)[IPaddrlen-1] || memcmp(x, y, IPaddrlen) )

extern uchar IPv4bcast[IPaddrlen];
extern uchar IPv4bcastobs[IPaddrlen];
extern uchar IPv4allsys[IPaddrlen];
extern uchar IPv4allrouter[IPaddrlen];
extern uchar IPnoaddr[IPaddrlen];
extern uchar v4prefix[IPaddrlen];
extern uchar IPallbits[IPaddrlen];

#define	NOW	TK2MS(MACHP(0)->ticks)

/*
 *  media
 */
extern Medium	ethermedium;
extern Medium	nullmedium;
extern Medium	pktmedium;
extern Medium	tripmedium;

/*
 *  ipifc.c
 */
extern Medium*	ipfindmedium(char *name);
extern void	addipmedium(Medium *med);
extern int	ipforme(Fs*, uchar *addr);
extern int	iptentative(Fs*, uchar *addr);
extern int	ipisbm(uchar *);
extern int	ipismulticast(uchar *);
extern Ipifc*	findipifc(Fs*, uchar *remote, int type);
extern void	findprimaryip(Fs*, uchar*);
extern void	findlocalip(Fs*, uchar *local, uchar *remote);
extern int	ipv4local(Ipifc *ifc, uchar *addr);
extern int	ipv6local(Ipifc *ifc, uchar *addr);
extern int	ipv6anylocal(Ipifc *ifc, uchar *addr);
extern Iplifc*	iplocalonifc(Ipifc *ifc, uchar *ip);
extern int	ipproxyifc(Fs *f, Ipifc *ifc, uchar *ip);
extern int	ipismulticast(uchar *ip);
extern int	ipisbooting(void);
extern int	ipifccheckin(Ipifc *ifc, Medium *med);
extern void	ipifccheckout(Ipifc *ifc);
extern int	ipifcgrab(Ipifc *ifc);
extern void	ipifcaddroute(Fs*, int, uchar*, uchar*, uchar*, int);
extern void	ipifcremroute(Fs*, int, uchar*, uchar*);
extern void	ipifcremmulti(Conv *c, uchar *ma, uchar *ia);
extern void	ipifcaddmulti(Conv *c, uchar *ma, uchar *ia);
extern char*	ipifcrem(Ipifc *ifc, char **argv, int argc);
extern char*	ipifcadd(Ipifc *ifc, char **argv, int argc, int tentative, Iplifc *lifcp);
extern long	ipselftabread(Fs*, char *a, ulong offset, int n);
extern char*	ipifcaddpref6(Ipifc *ifc, char**argv, int argc);
extern void	ipsendra6(Fs *f, int on);

/*
 *  ip.c
 */
extern void	iprouting(Fs*, int);
extern void	icmpnoconv(Fs*, Block*);
extern void	icmpcantfrag(Fs*, Block*, int);
extern void	icmpttlexceeded(Fs*, uchar*, Block*);
extern ushort	ipcsum(uchar*);
extern void	ipiput4(Fs*, Ipifc*, Block*);
extern void	ipiput6(Fs*, Ipifc*, Block*);
extern int	ipoput4(Fs*, Block*, int, int, int, Conv*);
extern int	ipoput6(Fs*, Block*, int, int, int, Conv*);
extern int	ipstats(Fs*, char*, int);
extern ushort	ptclbsum(uchar*, int);
extern ushort	ptclcsum(Block*, int, int);
extern void	ip_init(Fs*);
extern void	update_mtucache(uchar*, ulong);
extern ulong	restrict_mtu(uchar*, ulong);

/*
 * bootp.c
 */
char*	(*bootp)(Ipifc*);
int	(*bootpread)(char*, ulong, int);

/*
 *  iprouter.c
 */
void	useriprouter(Fs*, Ipifc*, Block*);
void	iprouteropen(Fs*);
void	iprouterclose(Fs*);
long	iprouterread(Fs*, void*, int);

/*
 *  resolving inferno/plan9 differences
 */
Chan*		commonfdtochan(int, int, int, int);
char*		commonuser(void);
char*		commonerror(void);

/*
 * chandial.c
 */
extern Chan*	chandial(char*, char*, char*, Chan**);

/*
 *  global to all of the stack
 */
extern void	(*igmpreportfn)(Ipifc*, uchar*);

/* IPV6 */
#define MIN(a, b) ((a) <= (b) ? (a) : (b))

/* rfc 3513 defines the address prefices */
#define isv6mcast(addr)	  ((addr)[0] == 0xff)
#define islinklocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0x80)
#define issitelocal(addr) ((addr)[0] == 0xfe && ((addr)[1] & 0xc0) == 0xc0)
#define isv6global(addr) (((addr)[0] & 0xe0) == 0x20)

#define optexsts(np) (nhgets((np)->ploadlen) > 24)
#define issmcast(addr) (memcmp((addr), v6solicitednode, 13) == 0)

/* from RFC 2460 */

typedef struct Ip6hdr     Ip6hdr;
typedef struct Opthdr     Opthdr;
typedef struct Routinghdr Routinghdr;
typedef struct Fraghdr6    Fraghdr6;

struct Ip6hdr {
	uchar vcf[4];       	// version:4, traffic class:8, flow label:20
	uchar ploadlen[2];  	// payload length: packet length - 40
	uchar proto;		// next header type
	uchar ttl;          	// hop limit
	uchar src[IPaddrlen];
	uchar dst[IPaddrlen];
};

struct Opthdr {
	uchar nexthdr;
	uchar len;
};

struct Routinghdr {
	uchar nexthdr;
	uchar len;
	uchar rtetype;
	uchar segrem;
};

struct Fraghdr6 {
	uchar nexthdr;
	uchar res;
	uchar offsetRM[2];	// Offset, Res, M flag
	uchar id[4];
};


enum {			/* Header Types */
	HBH		= 0,	//?
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
	//	multicast flgs and scop

	well_known_flg				= 0,
	transient_flg				= 1,

	node_local_scop 			= 1,
	link_local_scop 			= 2,
	site_local_scop 			= 5,
	org_local_scop				= 8,
	global_scop				= 14,

	//	various prefix lengths

	SOLN_PREF_LEN				= 13,

	//	icmpv6 unreach codes
	icmp6_no_route				= 0,
	icmp6_ad_prohib				= 1,
	icmp6_unassigned			= 2,
	icmp6_adr_unreach			= 3,
	icmp6_port_unreach			= 4,
	icmp6_unkn_code				= 5,

	// 	various flags & constants

	v6MINTU      				= 1280,
	HOP_LIMIT    				= 255,
	ETHERHDR_LEN 				= 14,
	IPV6HDR_LEN  				= 40,
	IPV4HDR_LEN  				= 20,

	// 	option types

	SRC_LLADDRESS    			= 1,
	TARGET_LLADDRESS 			= 2,
	PREFIX_INFO      			= 3,
	REDIR_HEADER     			= 4,
	MTU_OPTION       			= 5,

	SRC_UNSPEC  				= 0,
	SRC_UNI     				= 1,
	TARG_UNI    				= 2,
	TARG_MULTI  				= 3,

	t_unitent   				= 1,
	t_uniproxy  				= 2,
	t_unirany   				= 3,

	//	Router constants (all times in milliseconds)

	MAX_INITIAL_RTR_ADVERT_INTERVAL 	= 16000,
	MAX_INITIAL_RTR_ADVERTISEMENTS  	= 3,
	MAX_FINAL_RTR_ADVERTISEMENTS    	= 3,
	MIN_DELAY_BETWEEN_RAS 			= 3000,
	MAX_RA_DELAY_TIME     			= 500,

	//	Host constants

	MAX_RTR_SOLICITATION_DELAY 		= 1000,
	RTR_SOLICITATION_INTERVAL  		= 4000,
	MAX_RTR_SOLICITATIONS      		= 3,

	//	Node constants

	MAX_MULTICAST_SOLICIT   		= 3,
	MAX_UNICAST_SOLICIT     		= 3,
	MAX_ANYCAST_DELAY_TIME  		= 1000,
	MAX_NEIGHBOR_ADVERTISEMENT 		= 3,
	REACHABLE_TIME 				= 30000,
	RETRANS_TIMER  				= 1000,
	DELAY_FIRST_PROBE_TIME 			= 5000,

};

extern void ipv62smcast(uchar *, uchar *);
extern void icmpns(Fs *f, uchar* src, int suni, uchar* targ, int tuni, uchar* mac);
extern void icmpna(Fs *f, uchar* src, uchar* dst, uchar* targ, uchar* mac, uchar flags);
extern void icmpttlexceeded6(Fs *f, Ipifc *ifc, Block *bp);
extern void icmppkttoobig6(Fs *f, Ipifc *ifc, Block *bp);
extern void icmphostunr(Fs *f, Ipifc *ifc, Block *bp, int code, int free);

extern uchar v6allnodesN[IPaddrlen];
extern uchar v6allnodesL[IPaddrlen];
extern uchar v6allroutersN[IPaddrlen];
extern uchar v6allroutersL[IPaddrlen];
extern uchar v6allnodesNmask[IPaddrlen];
extern uchar v6allnodesLmask[IPaddrlen];
extern uchar v6allroutersS[IPaddrlen];
extern uchar v6solicitednode[IPaddrlen];
extern uchar v6solicitednodemask[IPaddrlen];
extern uchar v6Unspecified[IPaddrlen];
extern uchar v6loopback[IPaddrlen];
extern uchar v6loopbackmask[IPaddrlen];
extern uchar v6linklocal[IPaddrlen];
extern uchar v6linklocalmask[IPaddrlen];
extern uchar v6sitelocal[IPaddrlen];
extern uchar v6sitelocalmask[IPaddrlen];
extern uchar v6glunicast[IPaddrlen];
extern uchar v6multicast[IPaddrlen];
extern uchar v6multicastmask[IPaddrlen];

extern int v6llpreflen;
extern int v6slpreflen;
extern int v6lbpreflen;
extern int v6mcpreflen;
extern int v6snpreflen;
extern int v6aNpreflen;
extern int v6aLpreflen;

extern int ReTransTimer;
