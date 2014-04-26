/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <iplib.h>
#include <icmp.h>
#include <ctype.h>
#include <pthread.h>
#include <spinlock.h>
#include <timing.h>
#include <tsc-compat.h>
#include <printf-ext.h>
#include <alarm.h>
#include <ndb.h>

#define NR_MSG				4
#define SLEEPMS				1000
#define SECONDTSC			(get_tsc_freq())
#define MINUTETSC			(60 * SECONDTSC)
#define BUFSIZE				(64 * 1024 + 512)

typedef struct Req Req;
struct Req
{
	uint16_t	seq;	/* sequence number */
	uint64_t	tsctime;	/* time sent */
	int64_t	rtt;
	int	ttl;
	int	replied;
	Req	 *next;
};

struct proto {
	int	version;
	char	*net;
	int	echocmd;
	int	echoreply;
	unsigned iphdrsz;

	void	(*prreply)(Req *r, void *v);
	void	(*prlost)(uint16_t seq, void *v);
};


Req	*first;		/* request list */
Req	*last;		/* ... */
struct spin_pdr_lock listlock = SPINPDR_INITIALIZER;

char *argv0;

int addresses;
int debug;
int done;
int flood;
int lostmsgs;
int lostonly;
int quiet;
int rcvdmsgs;
int pingrint;
uint16_t firstseq;
int64_t sum;
int waittime = 5000;

static char *network, *target;

void lost(Req*, void*);
void reply(Req*, void*);

static void
usage(void)
{
	fprintf(stderr,
	    "usage: %s [-6alq] [-s msgsize] [-i millisecs] [-n #pings] dest\n",
		argv0);
	exit(1);
}

static void
prlost4(uint16_t seq, void *v)
{
	struct ip4hdr *ip4 = v;

	printf("lost %u: %i -> %i\n", seq, ip4->src, ip4->dst);
}

static void
prlost6(uint16_t seq, void *v)
{
	struct ip6hdr *ip6 = v;

	printf("lost %u: %i -> %i\n", seq, ip6->src, ip6->dst);
}

static void
prreply4(Req *r, void *v)
{
	struct ip4hdr *ip4 = v;

	printf("%u: %i -> %i rtt %lld µs, avg rtt %lld µs, ttl = %d\n",
		r->seq - firstseq, ip4->src, ip4->dst, r->rtt, sum/rcvdmsgs,
		r->ttl);
}

static void
prreply6(Req *r, void *v)
{
	struct ip6hdr *ip6 = v;

	printf("%u: %i -> %i rtt %lld µs, avg rtt %lld µs, ttl = %d\n",
		r->seq - firstseq, ip6->src, ip6->dst, r->rtt, sum/rcvdmsgs,
		r->ttl);
}

static struct proto v4pr = {
	4,		"icmp",
	EchoRequest,	EchoReply,
	IPV4HDR_LEN,
	prreply4,	prlost4,
};
static struct proto v6pr = {
	6,		"icmpv6",
	EchoRequestV6,	EchoReplyV6,
	IPV6HDR_LEN,
	prreply6,	prlost6,
};

static struct proto *proto = &v4pr;


struct icmphdr *
geticmp(void *v)
{
	char *p = v;

	return (struct icmphdr *)(p + proto->iphdrsz);
}

void
clean(uint16_t seq, int64_t now, void *v)
{
	int ttl;
	Req **l, *r;

	ttl = 0;
	if (v) {
		if (proto->version == 4)
			ttl = ((struct ip4hdr *)v)->ttl;
		else
			ttl = ((struct ip6hdr *)v)->ttl;
	}
	spin_pdr_lock(&listlock);
	last = NULL;
	for(l = &first; *l; ){
		r = *l;
		if(v && r->seq == seq){
			r->rtt = ndiff(r->tsctime, now);
			r->ttl = ttl;
			reply(r, v);
		}
		if (now - r->tsctime > MINUTETSC) {
			*l = r->next;
			r->rtt = ndiff(r->tsctime, now);
			if(v)
				r->ttl = ttl;
			if(r->replied == 0)
				lost(r, v);
			free(r);
		}else{
			last = r;
			l = &r->next;
		}
	}
	spin_pdr_unlock(&listlock);
}

static uint8_t loopbacknet[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0xff, 0xff,
	127, 0, 0, 0
};
static uint8_t loopbackmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0, 0, 0
};

/*
 * find first ip addr suitable for proto and
 * that isn't the friggin loopback address.
 * deprecate link-local and multicast addresses.
 */
static int
myipvnaddr(uint8_t *ip, struct proto *proto, char *net)
{
	int ipisv4, wantv4;
	struct ipifc *nifc;
	struct iplifc *lifc;
	uint8_t mynet[IPaddrlen], linklocal[IPaddrlen];
	static struct ipifc *ifc;

	ipmove(linklocal, IPnoaddr);
	wantv4 = proto->version == 4;
	ifc = readipifc(net, ifc, -1);
	for(nifc = ifc; nifc; nifc = nifc->next)
		for(lifc = nifc->lifc; lifc; lifc = lifc->next){
			maskip(lifc->ip, loopbackmask, mynet);
			if(ipcmp(mynet, loopbacknet) == 0)
				continue;
			if(ISIPV6MCAST(lifc->ip) || ISIPV6LINKLOCAL(lifc->ip)) {
				ipmove(linklocal, lifc->ip);
				continue;
			}
			ipisv4 = isv4(lifc->ip) != 0;
			if(ipcmp(lifc->ip, IPnoaddr) != 0 && wantv4 == ipisv4){
				ipmove(ip, lifc->ip);
				return 0;
			}
		}
	/* no global unicast addrs found, fall back to link-local, if any */
	ipmove(ip, linklocal);
	return ipcmp(ip, IPnoaddr) == 0? -1: 0;
}

void
sender(int fd, int msglen, int interval, int n)
{
	int i, extra;
	uint16_t seq;
	char *buf = malloc(BUFSIZE);
	uint8_t me[IPaddrlen], mev4[IPv4addrlen];
	struct icmphdr *icmp;
	Req *r;

	firstseq = seq = rand();

	icmp = geticmp(buf);
	memset(buf, 0, proto->iphdrsz + ICMP_HDRSIZE);
	for(i = proto->iphdrsz + ICMP_HDRSIZE; i < msglen; i++)
		buf[i] = i;
	icmp->type = proto->echocmd;
	icmp->code = 0;

	/* arguably the kernel should fill in the right src addr. */
	myipvnaddr(me, proto, network);
	if (proto->version == 4) {
		v6tov4(mev4, me);
		memmove(((struct ip4hdr *)buf)->src, mev4, IPv4addrlen);
	} else
		ipmove(((struct ip6hdr *)buf)->src, me);
	if (addresses)
		printf("\t%i -> %s\n", me, target);

	if(pingrint != 0 && interval <= 0)
		pingrint = 0;
	extra = 0;
	for(i = 0; i < n; i++){
		if(i != 0){
			if(pingrint != 0)
				extra = rand();
			/* uth_sleep takes seconds, interval is in ms */
			uthread_sleep((interval + extra) / 1000);
		}
		r = calloc(sizeof *r, 1);
		if (r == NULL){
			printf("out of memory? \n");
			break;
		}
		hnputs(icmp->seq, seq);
		r->seq = seq;
		r->next = NULL;
		r->replied = 0;
		r->tsctime = read_tsc();	/* avoid early free in reply! */
		spin_pdr_lock(&listlock);
		if(first == NULL)
			first = r;
		else
			last->next = r;
		last = r;
		spin_pdr_unlock(&listlock);
		r->tsctime = read_tsc();
		if(write(fd, buf, msglen) < msglen){
			fprintf(stderr, "%s: write failed: %r\n", argv0);
			return;
		}
		seq++;
	}
	done = 1;
}

void
rcvr(int fd, int msglen, int interval, int nmsg)
{
	int i, n, munged;
	uint16_t x;
	int64_t now;
	uint8_t *buf = malloc(BUFSIZE);
	struct icmphdr *icmp;
	Req *r;
	struct alarm_waiter waiter;

	init_awaiter(&waiter, alarm_abort_sysc);
	waiter.data = current_uthread;

	sum = 0;
	while(lostmsgs+rcvdmsgs < nmsg){
		/* arm to wake ourselves if the read doesn't connect in time */
		set_awaiter_rel(&waiter, 1000 *
		                ((nmsg - lostmsgs - rcvdmsgs) * interval + waittime));
		set_alarm(&waiter);
		n = read(fd, buf, BUFSIZE);
		/* cancel immediately, so future syscalls don't get aborted */
		unset_alarm(&waiter);

		now = read_tsc();
		if(n <= 0){	/* read interrupted - time to go */
			/* Faking time being a minute in the future, so clean marks our
			 * message as lost.  Note this will also end up cancelling any other
			 * pending replies that would have expired by then.  Whatever. */
			clean(0, now + MINUTETSC, NULL);
			continue;
		}
		if(n < msglen){
			printf("bad len %d/%d\n", n, msglen);
			continue;
		}
		icmp = geticmp(buf);
		munged = 0;
		for(i = proto->iphdrsz + ICMP_HDRSIZE; i < msglen; i++)
			if(buf[i] != (uint8_t)i)
				munged++;
		if(munged)
			printf("corrupted reply\n");
		x = nhgets(icmp->seq);
		if(icmp->type != proto->echoreply || icmp->code != 0) {
			printf("bad type/code/sequence %d/%d/%d (want %d/%d/%d)\n",
				icmp->type, icmp->code, x,
				proto->echoreply, 0, x);
			continue;
		}
		clean(x, now, buf);
	}

	spin_pdr_lock(&listlock);
	for(r = first; r; r = r->next)
		if(r->replied == 0)
			lostmsgs++;
	spin_pdr_unlock(&listlock);

	if(!quiet && lostmsgs)
		printf("%d out of %d messages lost\n", lostmsgs,
			lostmsgs+rcvdmsgs);
}

static int
isdottedquad(char *name)
{
	int dot = 0, digit = 0;

	for (; *name != '\0'; name++)
		if (*name == '.')
			dot++;
		else if (isdigit(*name))
			digit++;
		else
			return 0;
	return dot && digit;
}

static int
isv6lit(char *name)
{
	int colon = 0, hex = 0;

	for (; *name != '\0'; name++)
		if (*name == ':')
			colon++;
		else if (isxdigit(*name))
			hex++;
		else
			return 0;
	return colon;
}

/* from /sys/src/libc/9sys/dial.c */

enum
{
	Maxstring	= 128,
	Maxpath		= 256,
};

typedef struct DS DS;
struct DS {
	/* dist string */
	char	buf[Maxstring];
	char	*netdir;
	char	*proto;
	char	*rem;

	/* other args */
	char	*local;
	char	*dir;
	int	*cfdp;
};

/*
 *  parse a dial string
 */
static void
_dial_string_parse(char *str, DS *ds)
{
	char *p, *p2;

	strncpy(ds->buf, str, Maxstring);
	ds->buf[Maxstring-1] = 0;

	p = strchr(ds->buf, '!');
	if(p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->rem = ds->buf;
	} else {
		if(*ds->buf != '/' && *ds->buf != '#'){
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for(p2 = p; *p2 != '/'; p2--)
				;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->rem = p + 1;
	}
}

/* end excerpt from /sys/src/libc/9sys/dial.c */

/* side effect: sets network & target */
static int
isv4name(char *name)
{
	int r = 1;
	char *root, *ip, *pr;
	DS ds;

	_dial_string_parse(name, &ds);

	/* cope with leading /net.alt/icmp! and the like */
	root = NULL;
	if (ds.netdir != NULL) {
		pr = strrchr(ds.netdir, '/');
		if (pr == NULL)
			pr = ds.netdir;
		else {
			*pr++ = '\0';
			root = ds.netdir;
			network = strdup(root);
		}
		if (strcmp(pr, v4pr.net) == 0)
			return 1;
		if (strcmp(pr, v6pr.net) == 0)
			return 0;
	}

	/* if it's a literal, it's obvious from syntax which proto it is */
	free(target);
	target = strdup(ds.rem);
	if (isdottedquad(ds.rem))
		return 1;
	else if (isv6lit(ds.rem))
		return 0;
	/*we don't have cs.*/
	/* map name to ip and look at its syntax */
	ip = csgetvalue(root, "sys", ds.rem, "ip", NULL);
	if (ip == NULL)
		ip = csgetvalue(root, "dom", ds.rem, "ip", NULL);
	if (ip == NULL)
		ip = csgetvalue(root, "sys", ds.rem, "ipv6", NULL);
	if (ip == NULL)
		ip = csgetvalue(root, "dom", ds.rem, "ipv6", NULL);
	if (ip != NULL)
		r = isv4name(ip);
	free(ip);
	return r;
}

/* Feel free to put these in a struct or something */
int fd, msglen, interval, nmsg;

void *rcvr_thread(void* arg)
{	
	rcvr(fd, msglen, interval, nmsg);
	printd(lostmsgs ? "lost messages" : "");
	return 0;
}

int main(int argc, char **argv)
{
	char *ds;
	int pid;
	pthread_t rcvr;

	register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);

	msglen = interval = 0;
	nmsg = NR_MSG;

	argv0 = argv[0];
	if (argc <= 1)
		usage();
	argc--, argv++;
	while (**argv == '-'){
		switch(argv[0][1]){
		case '6':
			proto = &v6pr;
			break;
		case 'a':
			addresses = 1;
			break;
		case 'd':
			debug++;
			break;
		case 'f':
			flood = 1;
			break;
		case 'i':
			argc--,argv++;
			interval = atoi(*argv);
			if(interval < 0)
				usage();
			break;
		case 'l':
			lostonly++;
			break;
		case 'n':
			argc--,argv++;
			nmsg = atoi(*argv);
			if(nmsg < 0)
				usage();
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			pingrint = 1;
			break;
		case 's':
			argc--,argv++;
			msglen = atoi(*argv);
			break;
		case 'w':
			argc--,argv++;
			waittime = atoi(*argv);
			if(waittime < 0)
				usage();
			break;
		default:
			usage();
			break;
		}
		
		argc--,argv++;
	}

	if(msglen < proto->iphdrsz + ICMP_HDRSIZE)
		msglen = proto->iphdrsz + ICMP_HDRSIZE;
	if(msglen < 64)
		msglen = 64;
	if(msglen >= 64*1024)
		msglen = 64*1024-1;
	if(interval <= 0 && !flood)
		interval = SLEEPMS;

	if(argc < 1)
		usage();

	/* TODO: consider catching ctrl-c and other signals. */

	if (!isv4name(argv[0]))
		proto = &v6pr;
	ds = netmkaddr(argv[0], proto->net, "1");
	printf("ping: dial %s\n", ds);
	fd = dial(ds, 0, 0, 0);
	if(fd < 0){
		fprintf(stderr, "%s: couldn't dial %s: %r\n", argv0, ds);
		exit(1);
	}

	if (!quiet)
		printf("sending %d %d byte messages %d ms apart to %s\n",
			nmsg, msglen, interval, ds);

	/* Spawning the receiver on a separate thread, possibly separate core */
	if (pthread_create(&rcvr, NULL, &rcvr_thread, NULL)) {
		perror("Failed to create recevier");
		exit(-1);
	}
	sender(fd, msglen, interval, nmsg);
	/* races with prints from the rcvr.  either lock, or live with it! */
	printd("Sent, now joining\n");
	pthread_join(rcvr, NULL);
	return 0;
}

/* Note: this gets called from clean, which happens when we complete a loop and
 * got a reply in the receiver.  One problem is that we call printf from the
 * receive loop, blocking all future receivers, so that their times include the
 * printf time.  This isn't a huge issue, since the sender sleeps between each
 * one, and hopefully the print is done when the sender fires again. */
void
reply(Req *r, void *v)
{
	r->rtt /= 1000LL;
	sum += r->rtt;
	if(!r->replied)
		rcvdmsgs++;
	if(!quiet && !lostonly)
		if(addresses)
			(*proto->prreply)(r, v);
		else
			printf("%3d: rtt %5lld usec, avg rtt %5lld usec, ttl = %d\n",
				r->seq - firstseq, r->rtt, sum/rcvdmsgs, r->ttl);
	r->replied = 1;
}

void
lost(Req *r, void *v)
{
	if(!quiet)
		if(addresses && v != NULL)
			(*proto->prlost)(r->seq - firstseq, v);
		else
			printf("%3d: lost\n", r->seq - firstseq);
	lostmsgs++;
}
