/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

struct conf {
	// locally generated
	char *type;
	char *dev;
	char mpoint[32];
	int cfd;					// ifc control channel
	int dfd;					// ifc data channel (for ppp)
	char *cputype;
	uint8_t hwa[32];			// hardware address
	int hwatype;
	int hwalen;
	uint8_t cid[32];
	int cidlen;
	char *baud;

	// learned info
	uint8_t gaddr[IPaddrlen];
	uint8_t laddr[IPaddrlen];
	uint8_t mask[IPaddrlen];
	uint8_t raddr[IPaddrlen];
	uint8_t dns[2 * IPaddrlen];
	uint8_t fs[2 * IPaddrlen];
	uint8_t auth[2 * IPaddrlen];
	uint8_t ntp[IPaddrlen];
	int mtu;

	// dhcp specific
	int state;
	int fd;
	uint32_t xid;
	uint32_t starttime;
	char sname[64];
	char hostname[32];
	char domainname[64];
	uint8_t server[IPaddrlen];	// server IP address
	uint32_t offered;			// offered lease time
	uint32_t lease;				// lease time
	uint32_t resend;			// # of resends for current state
	uint32_t timeout;			// time to timeout - seconds

	//
	// IPv6
	//

	// solicitation specific - XXX add support for IPv6 leases
	// ulong	solicit_retries;

	// router-advertisement related
	uint8_t sendra;
	uint8_t recvra;
	uint8_t mflag;
	uint8_t oflag;
	int maxraint;				// rfc2461, p.39:
								// 4sec ≤ maxraint ≤ 1800sec, def 600
	int minraint;				// 3sec ≤ minraint ≤ 0.75*maxraint
	int linkmtu;
	int reachtime;				// 3,600,000 msec, default 0
	int rxmitra;				// default 0
	int ttl;					// default 0 (unspecified)

	// default gateway params
	uint8_t v6gaddr[IPaddrlen];
	int routerlt;				// router life time

	// prefix related
	uint8_t v6pref[IPaddrlen];
	int prefixlen;
	uint8_t onlink;				// flag: address is `on-link'
	uint8_t autoflag;			// flag: autonomous
	uint32_t validlt;			// valid lifetime (seconds)
	uint32_t preflt;			// preferred lifetime (seconds)
};

struct ctl {
	struct ctl *next;
	char *ctl;
};

extern struct ctl *firstctl, **ctll;
extern struct conf conf;
extern int noconfig;
extern int ipv6auto;
extern int debug;
extern int dodhcp;
extern int dolog;
extern int nip;
extern int plan9;
extern int dupl_disc;
extern int myifc;
extern char *vs;

void adddefroute(char *, uint8_t *);
int addoption(char *opt);
void binddevice(void);
void bootprequest(void);
void controldevice(void);
void dhcpquery(int, int);
void dhcprecv(void);
void dhcpsend(int);
int dhcptimer(void);
void dhcpwatch(int);
void doadd(int);
void doremove(void);
void dounbind(void);
void ea2lla(uint8_t *lla, uint8_t *ea);
void evexit(int rc);
int getndb(void);
void getoptions(uint8_t *p);
int ip4cfg(void);
int ip6cfg(int autoconf);
int ipconfig4(void);
int ipconfig6(int);
void ipv62smcast(uint8_t *smcast, uint8_t *a);
long jitter(void);
void lookforip(char *);
void mkclientid(void);
void ndbconfig(void);
int nipifcs(char *);
int openlisten(void);
uint8_t *optadd(uint8_t *, int, void *, int);
uint8_t *optaddaddr(uint8_t *, int, uint8_t *);
uint8_t *optaddbyte(uint8_t *, int, int);
uint8_t *optaddstr(uint8_t *, int, char *);
uint8_t *optaddulong(uint8_t *, int, uint32_t);
uint8_t *optaddvec(uint8_t *, int, uint8_t *, int);
uint8_t *optget(uint8_t *, int, int *);
int optgetaddr(uint8_t *, int, uint8_t *);
int optgetaddrs(uint8_t *, int, uint8_t *, int);
int optgetbyte(uint8_t *, int);
int optgetp9addrs(uint8_t *ap, int op, uint8_t *ip, int n);
int optgetstr(uint8_t *, int, char *, int);
uint32_t optgetulong(uint8_t *, int);
int optgetvec(uint8_t *, int, uint8_t *, int);
struct bootp *parsebootp(uint8_t *p, int n);
int parseoptions(uint8_t *p, int n);
int parseverb(char *);
void procsetname(char *fmt, ...);
void putndb(void);
uint32_t randint(uint32_t low, uint32_t hi);
void tweakservers(void);
void usage(void);
int validip(uint8_t *);
void warning(char *fmt, ...);
void writendb(char *, int, int);

/*
 * IPv6
 */
void doipv6(int);
int ipconfig6(int);
void recvra6(void);
void sendra6(void);
void v6paraminit(struct conf *);

enum {
	IsRouter = 1,
	IsHostRecv = 2,
	IsHostNoRecv = 3,

	MAClen = 6,

	IPv4 = 4,
	IPv6 = 6,
	Defmtu = 1400,

	IP_HOPBYHOP = 0,
	ICMPv4 = 1,
	IP_IGMPPROTO = 2,
	IP_TCPPROTO = 6,
	IP_UDPPROTO = 17,
	IP_ILPROTO = 40,
	IP_v6ROUTE = 43,
	IP_v6FRAG = 44,
	IP_IPsecESP = 50,
	IP_IPsecAH = 51,
	IP_v6NOMORE = 59,
	ICMP6_RS = 133,
	ICMP6_RA = 134,

	IP_IN_IP = 41,
};

enum {
	MFMASK = 1 << 7,
	OCMASK = 1 << 6,
	OLMASK = 1 << 7,
	AFMASK = 1 << 6,
};

enum {
	MAXTTL = 255,
	D64HLEN = IPV6HDR_LEN - IPV4HDR_LEN,
	IP_MAX = 32 * 1024,
};

struct routersol {
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header	type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t res[4];
};

struct routeradv {
	uint8_t vcf[4];				// version:4, traffic class:8, flow label:20
	uint8_t ploadlen[2];		// payload length: packet length - 40
	uint8_t proto;				// next header type
	uint8_t ttl;				// hop limit
	uint8_t src[IPaddrlen];
	uint8_t dst[IPaddrlen];
	uint8_t type;
	uint8_t code;
	uint8_t cksum[2];
	uint8_t cttl;
	uint8_t mor;
	uint8_t routerlt[2];
	uint8_t rchbltime[4];
	uint8_t rxmtimer[4];
};

struct lladdropt {
	uint8_t type;
	uint8_t len;
	uint8_t lladdr[MAClen];
};

struct prefixopt {
	uint8_t type;
	uint8_t len;
	uint8_t plen;
	uint8_t lar;
	uint8_t validlt[4];
	uint8_t preflt[4];
	uint8_t reserv[4];
	uint8_t pref[IPaddrlen];
};

struct mtuopt {
	uint8_t type;
	uint8_t len;
	uint8_t reserv[2];
	uint8_t mtu[4];
};
