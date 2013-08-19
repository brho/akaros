enum
{
	Nmaxaddr=	64,
	Nmhash=		31,

	Ncloneqid=	1,
	Naddrqid,
	N2ndqid,
	N3rdqid,
	Ndataqid,
	Nctlqid,
	Nstatqid,
	Ntypeqid,
	Nifstatqid,
	Nmtuqid,
};

/*
 *  Macros to manage struct qid's used for multiplexed devices
 */
#define NETTYPE(x)	(((uint32_t)x)&0x1f)
#define NETID(x)	((((uint32_t)x))>>5)
#define NETQID(i,t)	((((uint32_t)i)<<5)|(t))

/*
 *  one per multiplexed connection
 */
struct netfile
{
	qlock_t qlock;

	int	inuse;
	uint32_t	mode;
	char	owner[KNAMELEN];

	int	type;			/* multiplexor type */
	int	prom;			/* promiscuous mode */
	int	scan;			/* base station scanning interval */
	int	bridge;			/* bridge mode */
	int	headersonly;		/* headers only - no data */
	uint8_t	maddr[8];		/* bitmask of multicast addresses requested */
	int	nmaddr;			/* number of multicast addresses */

	struct queue*	iq;			/* input */
};

/*
 *  a network address
 */
struct netaddr
{
	struct netaddr	*next;			/* allocation chain */
	struct netaddr	*hnext;
	uint8_t	addr[Nmaxaddr];
	struct kref ref;
};

/*
 *  a network interface
 */
struct netif
{
	qlock_t qlock;

	/* multiplexing */
	char	name[KNAMELEN];		/* for top level directory */
	int	nfile;			/* max number of Netfiles */
	struct netfile	**f;

	/* about net */
	int	limit;			/* flow control */
	int	alen;			/* address length */
	int	mbps;			/* megabits per sec */
	int	link;			/* link status */
	int	minmtu;
	int 	maxmtu;
	int	mtu;
	uint8_t	addr[Nmaxaddr];
	uint8_t	bcast[Nmaxaddr];
	struct netaddr	*maddr;			/* known multicast addresses */
	int	nmaddr;			/* number of known multicast addresses */
	struct netaddr *mhash[Nmhash];		/* hash table of multicast addresses */
	int	prom;			/* number of promiscuous opens */
	int	scan;			/* number of base station scanners */
	int	all;			/* number of -1 multiplexors */

	struct queue*	oq;			/* output */

	/* statistics */
	uint64_t	misses;
	uint64_t	inpackets;
	uint64_t	outpackets;
	uint64_t	crcs;			/* input crc errors */
	uint64_t	oerrs;			/* output errors */
	uint64_t	frames;			/* framing errors */
	uint64_t	overflows;		/* packet overflows */
	uint64_t	buffs;			/* buffering errors */
	uint64_t	soverflows;		/* software overflow */

	/* routines for touching the hardware */
	void	*arg;
	void	(*promiscuous)(void*, int);
	void	(*multicast)(void*, uint8_t*, int);
	int	(*hwmtu)(void*, int);	/* get/set mtu */
	void	(*scanbs)(void*, unsigned int);	/* scan for base stations */
};

void	netifinit(struct netif*, char*, int, uint32_t);
struct walkqid*	netifwalk(struct netif*, struct chan*, struct chan*, char **, int, struct errbuf *perrbuf);
struct chan*	netifopen(struct netif*, struct chan*, int, struct errbuf *perrbuf);
void	netifclose(struct netif*, struct chan*, struct errbuf *perrbuf);
long	netifread(struct netif*, struct chan*, void*, long, int64_t, struct errbuf *perrbuf);
struct block*	netifbread(struct netif*, struct chan*, long, int64_t, struct errbuf *perrbuf);
long	netifwrite(struct netif*, struct chan*, void*, long, struct errbuf *perrbuf);
long	netifwstat(struct netif*, struct chan*, uint8_t*, long, struct errbuf *perrbuf);
long	netifstat(struct netif*, struct chan*, uint8_t*, long, struct errbuf *perrbuf);
int	activemulti(struct netif*, uint8_t*, int);
