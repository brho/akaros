/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Network address translation for VM guests.
 *
 * There are two styles of addressing: qemu and real-addr.  qemu-style is what
 * you expect from Qemu's user-mode networking.  real-addr mode uses the same
 * addresses for the guest as the host uses.
 *
 * For qemu-style networking:
 * 		guest = 10.0.2.15, mask = 255.255.255.0, router = 10.0.2.2.
 * For real-addr networking:
 * 		guest = host_v4, mask = host_mask, router = host's_route
 *
 * Real-addr mode is useful for guests that statically config their address from
 * the nodes hard drive.  It might also help for apps that want to advertise
 * their IP address to external users (though that would require straight-thru
 * port-fowardarding set up by the VMM).
 *
 * As far as the guest is concerned, the host is the guest_v4_router.  If we
 * ever get a remote IP addr of 'router', that'll be redirected to the host's
 * loopback IP.  That means the guest can't reach the "real" router (in
 * real-addr mode).  The host can reach the guest via 127.0.0.1.  In either
 * case, use the host's side of a mapping.
 *
 * TODO:
 * - We're working with the IOVs that the guest gave us through virtio.  If we
 *   care, that whole thing is susceptible to time-of-check attacks.  The guest
 *   could be modifying the IOV that we're working on, so we need to not care
 *   too much about that.
 *
 * - Consider having the kernel proto bypass outbound path overwrite the src
 *   address and the proto port.  We don't care about the proto payload.  We
 *   might care about IP and proto headers.  o/w, the user could fake traffic
 *   for other ports - basically they can craft whatever packet they want (which
 *   is what they had previously with unrestricted access to devether).
 *
 * - Consider injecting packets to the guest from #srv/snoop-PID.
 *
 * - IPv6 support
 *
 * FAQ:
 * - Why are we using FD taps, instead of threads, to watch all the host FD?
 *   Couldn't we block a uthread on each FD?  I went back and forth on this.
 *   The final reason for this is to avoid copies and weird joins.  The
 *   concurrency with the guest is based on the number of IOVs they give us -
 *   not the number of host conversations open.  We could listen on 1000 convs,
 *   each with their own read buffer, but we'd then have to copy the entire
 *   packet into the IOVs the guest gave us.  We'd also need to synchronize on
 *   access to virtio and do some structural work (draw out how the packets
 *   would move around).  It'd be different if each uthread could just push
 *   packets into virtio-net (push vs the current pull model).
 *
 * - What's the deal with sending packets to broadcast domain targets?  Short
 *   answer: the host responds to every ARP request, regardless of the IP.  If
 *   the networking is done QEMU style, there's only one other host: the router,
 *   so that's not interesting.  If we are in real-addr mode and the guest is
 *   trying to reach someone in our broadcast, they'll be told that we
 *   (host_eth_addr) is the MAC for that addr.  Then the guest sends us an IP
 *   packet for that target.  Akaros will see an IP packet and will route it to
 *   its own broadcast (on the real network).  The guest's ARP only matters when
 *   it comes to getting the packet to us, not the actual network's broadcast
 *   domain.
 *
 * - Why is the RX path single threaded?  So it's possible to rewrite
 *   __poll_inbound() such that readv() is not called while holding the rx_mtx.
 *   To do so, we pop the first item off the inbound_todo list (so we have the
 *   ref), do the read, then put it back on the list if it hasn't been drained
 *   to empty.  The main issue, apart from being more complicated, is that since
 *   we're unlocking and relocking, any invariant that we had before calling
 *   __poll_inbound needs to be rechecked.  Specifically, we would need to check
 *   __poll_injection *after* returning from __poll_inbound.  Otherwise we could
 *   sleep with a packet waiting to be injected.  Whoops!  That could have been
 *   dealt with, but it's subtle.  There also might be races with FD taps
 *   firing, the fdtap_watcher not putting items on the list, and the thread
 *   then not putting it on the list.  Specifically:
 *   	fdtap_watcher:							__poll_inbound:
 *   	-------------------------------------------------------
 *   											yanks map off list
 *   											map tracked as "on inbound"
 *   											unlock mtx
 *   											readv, get -1 EAGAIN
 *   											decide to drop the item
 *   	packet arrives
 *   	FD tap fires
 *   	send event
 *   	lock mtx
 *   	see map is "on inbound"
 *   	ignore event
 *   	unlock mtx
 *   											lock mtx
 *   											clear "on inbound"
 *   											unlock + sleep on CV
 *   The FD has data, but we lost the event, and we'll never read it.
 *
 * - Why is the fdtap_watcher its own thread?  You can't kick a CV from vcore
 *   context, since you almost always want to hold the MTX while kicking the CV
 *   (see the lengthy comments in the CV code).  It's easier to blockon an event
 *   queue as a uthread.  But since the RX thread wants to sleep on two sources,
 *   it's simpler this way.  It also decouples the inbound_todo list from the
 *   evq.
 *
 * - Could we model the packet injection with an event queue?  Maybe with a UCQ
 *   or BCQ.  We'd need some support from the kernel (minor) and maybe
 *   user-level event posting (major) to do it right.  If we did that, we
 *   probably could get rid of the fdtap_watcher.  The RX checks inbound_todo,
 *   then blocks on two evqs (inbound and inject).  This way is simpler, for
 *   now.
 *
 * - Why do we rewrite IP addresses for the router to loopback, instead of
 *   host_v4_addr?  First off, you have to pick one: loopback or host_v4_addr,
 *   not both.  If we accept both (say, when the host connects to a static map),
 *   then on the other end (i.e. TX, response to an RX) will need to know which
 *   option we chose for its rewriting rule.  We have to be consistent with how
 *   we handle ROUTER_IP and do the same thing in both directions.  Given that
 *   we have to pick one of them, I opted for 127.0.0.1.  That way, any host
 *   users know how to connect to the guest without worrying about their actual
 *   IP address.  This also allows us to announce services on the host that are
 *   only available to loopback (i.e. not the main network) and let the guest
 *   reach those.
 *
 * - How can a guest reach the real host IP in qemu mode, but not in real-addr
 *   mode?  This comes up when someone uses a static map but connects with the
 *   host_v4_addr as the source (which you do by contacting host_v4_addr as the
 *   *destination* from akaros).  We don't rewrite those on the RX path.  When
 *   the guest responds, it responds to whatever the src was on the inbound
 *   path.  To the guest, our host_v4_addr is just another IP, which the host
 *   knows how to route to.  It'd be similar to the guest trying to reach an
 *   address that is in the broadcast domain of the host.  This doesn't work for
 *   real-addr mode, since the guest has the same IP as the host.  Most guests
 *   won't emit a packet that is sent to their own IP address.  If they did, the
 *   NAT code would remap it, but the guest just won't send it out.  Hence the
 *   warning.
 */

#include <vmm/net.h>
#include <parlib/iovec.h>
#include <iplib/iplib.h>
#include <parlib/ros_debug.h>
#include <parlib/uthread.h>
#include <ndblib/ndb.h>
#include <iplib/iplib.h>
#include <parlib/printf-ext.h>
#include <parlib/event.h>
#include <parlib/spinlock.h>
#include <parlib/kref.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/queue.h>

/* Global control variables.  The main VMM sets these manually. */
bool vnet_snoop = FALSE;
bool vnet_real_ip_addrs = FALSE;
bool vnet_map_diagnostics = FALSE;
unsigned long vnet_nat_timeout = 200;

uint8_t host_v4_addr[IPV4_ADDR_LEN];
uint8_t host_v4_mask[IPV4_ADDR_LEN];
uint8_t host_v4_router[IPV4_ADDR_LEN];
uint8_t host_v4_dns[IPV4_ADDR_LEN];

uint8_t loopback_v4_addr[IPV4_ADDR_LEN];
uint8_t bcast_v4_addr[IPV4_ADDR_LEN];

uint8_t guest_v4_addr[IPV4_ADDR_LEN];
uint8_t guest_v4_mask[IPV4_ADDR_LEN];
uint8_t guest_v4_router[IPV4_ADDR_LEN];
uint8_t guest_v4_dns[IPV4_ADDR_LEN];

/* We'll use this in all our eth headers with the guest. */
uint8_t host_eth_addr[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x0a};
uint8_t guest_eth_addr[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x0b};
const char dhcp_hostname[] = "host";
const char dhcp_guestname[] = "guest";

int snoop_fd;

/* We map between host port and guest port for a given protocol.  We don't care
 * about confirming IP addresses or anything - we just swap ports. */
struct ip_nat_map {
	TAILQ_ENTRY(ip_nat_map)		lookup_tuple;
	TAILQ_ENTRY(ip_nat_map)		lookup_fd;
	struct kref					kref;
	uint8_t						protocol;
	uint16_t					guest_port;
	uint16_t					host_port;
	int							host_data_fd;
	bool						is_static;
	bool						is_stale;
	/* These fields are protected by the rx mutex */
	TAILQ_ENTRY(ip_nat_map)		inbound;
	bool						is_on_inbound;
};

#define NR_VNET_HASH 128
TAILQ_HEAD(ip_nat_map_tailq, ip_nat_map);
struct spin_pdr_lock maps_lock = SPINPDR_INITIALIZER;
/* Two hash tables: one for tuples (tx) and one for FD (rx).  There's one kref
 * for being in both tables; they are treated as a unit. */
struct ip_nat_map_tailq map_hash_tuple[NR_VNET_HASH];
struct ip_nat_map_tailq map_hash_fd[NR_VNET_HASH];

/* The todo list, used to track FDs that had activity but haven't told us EAGAIN
 * yet.  The list is protected by the rx_mtx */
struct ip_nat_map_tailq inbound_todo = TAILQ_HEAD_INITIALIZER(inbound_todo);

/* buf_pkt: tracks a packet, used for injecting packets (usually synthetic
 * responses) into the guest via receive_packet. */
struct buf_pkt {
	STAILQ_ENTRY(buf_pkt)		next;
	uint8_t						*buf;
	size_t						sz;
};
STAILQ_HEAD(buf_pkt_stailq, buf_pkt);

struct buf_pkt_stailq inject_pkts = STAILQ_HEAD_INITIALIZER(inject_pkts);
uth_mutex_t *rx_mtx;
uth_cond_var_t *rx_cv;
struct event_queue *inbound_evq;

static void tap_inbound_conv(int fd);

#define GOLDEN_RATIO_64 0x61C8864680B583EBull

static struct ip_nat_map_tailq *list_hash_tuple(uint8_t protocol,
                                                uint16_t guest_port)
{
	size_t idx;

	idx = ((protocol << 16 | guest_port) * GOLDEN_RATIO_64) % NR_VNET_HASH;
	return &map_hash_tuple[idx];
}

static struct ip_nat_map_tailq *list_hash_fd(int host_data_fd)
{
	size_t idx;

	idx = (host_data_fd * GOLDEN_RATIO_64) % NR_VNET_HASH;
	return &map_hash_fd[idx];
}

/* Returnes a refcnted map. */
static struct ip_nat_map *lookup_map_by_tuple(uint8_t protocol,
                                              uint16_t guest_port)
{
	struct ip_nat_map *i;

	spin_pdr_lock(&maps_lock);
	TAILQ_FOREACH(i, list_hash_tuple(protocol, guest_port), lookup_tuple) {
		if ((i->protocol == protocol) && (i->guest_port == guest_port)) {
			kref_get(&i->kref, 1);
			break;
		}
	}
	spin_pdr_unlock(&maps_lock);
	return i;
}

static struct ip_nat_map *lookup_map_by_hostfd(int host_data_fd)
{
	struct ip_nat_map *i;

	spin_pdr_lock(&maps_lock);
	TAILQ_FOREACH(i, list_hash_fd(host_data_fd), lookup_fd) {
		if (i->host_data_fd == host_data_fd) {
			kref_get(&i->kref, 1);
			break;
		}
	}
	spin_pdr_unlock(&maps_lock);
	return i;
}

/* Stores the ref to the map in the global lookup 'table.' */
static void add_map(struct ip_nat_map *map)
{
	spin_pdr_lock(&maps_lock);
	TAILQ_INSERT_HEAD(list_hash_tuple(map->protocol, map->guest_port),
	                  map, lookup_tuple);
	TAILQ_INSERT_HEAD(list_hash_fd(map->host_data_fd), map, lookup_fd);
	spin_pdr_unlock(&maps_lock);
}

static void map_release(struct kref *kref)
{
	struct ip_nat_map *map = container_of(kref, struct ip_nat_map, kref);

	close(map->host_data_fd);
	free(map);
}

/* Creates a reference counted ip_nat_map for protocol between guest_port and
 * host_port.  Static mappings are ones that never expire, such as a port
 * forwarding.  Caller should add it to the lookup structure.
 *
 * For the host port, pass "*" for any port, like in a dialstring. */
static struct ip_nat_map *create_map(uint8_t protocol, uint16_t guest_port,
                                     char *host_port, bool is_static)
{
	struct ip_nat_map *map;
	char dialstring[128];
	char conv_dir[NETPATHLEN];
	char *proto_str;
	int bypass_fd;
	bool port_check;

	map = malloc(sizeof(struct ip_nat_map));
	assert(map);
	kref_init(&map->kref, map_release, 1);
	map->protocol = protocol;
	map->guest_port = guest_port;
	map->is_static = is_static;
	map->is_stale = FALSE;
	map->is_on_inbound = FALSE;

	switch (protocol) {
	case IP_UDPPROTO:
		proto_str = "udp";
		break;
	case IP_TCPPROTO:
		proto_str = "tcp";
		break;
	default:
		panic("get_map for unsupported protocol %d!", protocol);
	}
	snprintf(dialstring, sizeof(dialstring), "%s!*!%s", proto_str, host_port);

	bypass_fd = bypass9(dialstring, conv_dir, 0);
	if (bypass_fd < 0) {
		fprintf(stderr, "Failed to clone a conv for %s:%d (%r), won't bypass!\n",
		        proto_str, guest_port);
		free(map);
		return NULL;
	}

	port_check = get_port9(conv_dir, "local", &map->host_port);
	parlib_assert_perror(port_check);

	map->host_data_fd = open_data_fd9(conv_dir, O_NONBLOCK);
	parlib_assert_perror(map->host_data_fd >= 0);

	tap_inbound_conv(map->host_data_fd);

	close(bypass_fd);
	return map;
}

/* Looks up or creates an ip_nat_map for the given proto/port tuple. */
static struct ip_nat_map *get_map_by_tuple(uint8_t protocol,
                                           uint16_t guest_port)
{
	struct ip_nat_map *map;

	map = lookup_map_by_tuple(protocol, guest_port);
	if (map)
		return map;
	map = create_map(protocol, guest_port, "*", FALSE);
	if (!map)
		return NULL;
	kref_get(&map->kref, 1);
	add_map(map);
	return map;
}

static void *map_reaper(void *arg)
{
	struct ip_nat_map *i, *temp;
	struct ip_nat_map_tailq to_release;

	while (1) {
		uthread_sleep(vnet_nat_timeout);
		TAILQ_INIT(&to_release);
		spin_pdr_lock(&maps_lock);
		/* Only need to scan one map_hash, might as well be the tuple */
		for (int j = 0; j < NR_VNET_HASH; j++) {
			TAILQ_FOREACH_SAFE(i, &map_hash_tuple[j], lookup_tuple, temp) {
				if (i->is_static)
					continue;
				if (!i->is_stale) {
					i->is_stale = TRUE;
					continue;
				}
				/* Remove from both lists, hashing for the FD list */
				TAILQ_REMOVE(&map_hash_tuple[j], i, lookup_tuple);
				TAILQ_REMOVE(list_hash_fd(i->host_data_fd), i, lookup_fd);
				/* Use the lookup_tuple for the temp list */
				TAILQ_INSERT_HEAD(&to_release, i, lookup_tuple);
			}
		}
		spin_pdr_unlock(&maps_lock);
		TAILQ_FOREACH_SAFE(i, &to_release, lookup_tuple, temp)
			kref_put(&i->kref);
	}
	return 0;
}

static void map_dumper(void)
{
	struct ip_nat_map *i;

	fprintf(stderr, "\n\nVNET NAT maps:\n---------------\n");
	spin_pdr_lock(&maps_lock);
	for (int j = 0; j < NR_VNET_HASH; j++) {
		TAILQ_FOREACH(i, &map_hash_tuple[j], lookup_tuple) {
			fprintf(stderr, "\tproto %2d, host %5d, guest %5d, FD %4d, stale %d, static %d, ref %d\n",
			        i->protocol, i->host_port, i->guest_port, i->host_data_fd,
			        i->is_stale, i->is_static, i->kref.refcnt);
		}
	}
	spin_pdr_unlock(&maps_lock);
}

static void init_map_lookup(struct virtual_machine *vm)
{
	for (int i = 0; i < NR_VNET_HASH; i++)
		TAILQ_INIT(&map_hash_tuple[i]);
	for (int i = 0; i < NR_VNET_HASH; i++)
		TAILQ_INIT(&map_hash_fd[i]);
	vmm_run_task(vm, map_reaper, NULL);
}

static struct buf_pkt *zalloc_bpkt(size_t size)
{
	struct buf_pkt *bpkt;

	bpkt = malloc(sizeof(struct buf_pkt));
	assert(bpkt);
	bpkt->sz = size;
	bpkt->buf = malloc(bpkt->sz);
	assert(bpkt->buf);
	memset(bpkt->buf, 0, bpkt->sz);
	return bpkt;
}

static void free_bpkt(struct buf_pkt *bpkt)
{
	free(bpkt->buf);
	free(bpkt);
}

/* Queues a buf_pkt, which the rx thread will inject when it wakes. */
static void inject_buf_pkt(struct buf_pkt *bpkt)
{
	uth_mutex_lock(rx_mtx);
	STAILQ_INSERT_TAIL(&inject_pkts, bpkt, next);
	uth_mutex_unlock(rx_mtx);
	uth_cond_var_broadcast(rx_cv);
}

/* Helper for xsum_update, mostly for paranoia with integer promotion and
 * cleanly keeping variables as u16. */
static uint16_t ones_comp(uint16_t x)
{
	return ~x;
}

/* IP checksum updater.  If you change amt bytes in a packet from old to new,
 * this updates the xsum at xsum_off in the iov.
 *
 * Assumes a few things:
 * - there's a 16 byte xsum at xsum_off
 * - amt is a multiple of two
 * - the data at *old and *new is network (big) endian
 *
 * There's no assumption about the alignment of old and new, thanks to Plan 9's
 * sensible nhgets() (just byte accesses, not assuming u16 alignment).
 *
 * See RFC 1624 for the math.  I opted for Eqn 3, instead of 4, since I didn't
 * want to deal with subtraction underflow / carry / etc.  Also note that we
 * need to do the intermediate carry before doing the one's comp.  That wasn't
 * clear from the RFC either.  RFC 1141 didn't need to do that, since they
 * didn't complement the intermediate HC (xsum). */
static void xsum_update(struct iovec *iov, int iovcnt, size_t xsum_off,
                        uint8_t *old, uint8_t *new, size_t amt)
{
	uint32_t xsum;

	assert(amt % 2 == 0);
	xsum = iov_get_be16(iov, iovcnt, xsum_off);
	/* for each short: HC' = ~(~HC + ~m + m') (' == new, ~ == ones-comp) */
	for (int i = 0; i < amt / 2; i++, old += 2, new += 2) {
		xsum = ones_comp(xsum) + ones_comp(nhgets(old)) + nhgets(new);
		/* Need to deal with the carry for any additions, before the outer ~()
		 * operation.  (Not mentioned in the RFC, determined manually...) */
		while (xsum >> 16)
			xsum = (xsum & 0xffff) + (xsum >> 16);
		/* Yes, next time around the loop we ones comp right back.  Not worth
		 * optimizing. */
		xsum = ones_comp(xsum);
	}
	iov_put_be16(iov, iovcnt, xsum_off, xsum);
}

static void snoop_on_virtio(void)
{
	int ret;
	int srv_fd, pipe_dir_fd, pipe_ctl_fd, pipe_srv_fd, pipe_snoop_fd;
	const char cmd[] = "oneblock";
	char buf[MAX_PATH_LEN];

	pipe_dir_fd = open("#pipe", O_PATH);
	parlib_assert_perror(pipe_dir_fd >= 0);

	pipe_ctl_fd = openat(pipe_dir_fd, "ctl", O_RDWR);
	parlib_assert_perror(pipe_ctl_fd >= 0);
	ret = write(pipe_ctl_fd, cmd, sizeof(cmd));
	parlib_assert_perror(ret == sizeof(cmd));
	close(pipe_ctl_fd);

	pipe_snoop_fd = openat(pipe_dir_fd, "data", O_RDWR);
	parlib_assert_perror(pipe_snoop_fd >= 0);
	ret = fcntl(pipe_snoop_fd, F_SETFL, O_NONBLOCK);
	parlib_assert_perror(!ret);
	snoop_fd = pipe_snoop_fd;

	pipe_srv_fd = openat(pipe_dir_fd, "data1", O_RDWR);
	parlib_assert_perror(pipe_srv_fd >= 0);
	ret = snprintf(buf, sizeof(buf), "#srv/%s-%d", "snoop", getpid());
	/* We don't close srv_fd here.  When we exit, it'll close and remove. */
	srv_fd = open(buf, O_RDWR | O_EXCL | O_CREAT | O_REMCLO, 0444);
	parlib_assert_perror(srv_fd >= 0);
	ret = snprintf(buf, sizeof(buf), "%d", pipe_srv_fd);
	ret = write(srv_fd, buf, ret);
	parlib_assert_perror(ret > 0);
	close(pipe_srv_fd);
}

/* Gets the host's IPv4 information from iplib and ndb. */
static void get_host_ip_addrs(void)
{
	struct ndb *ndb;
	struct ndbtuple *nt;
	char *dns = "dns";
	char my_ip_str[40];
	char buf[128];
	struct ipifc *to_free;
	struct iplifc *lifc;
	int ret;
	uint8_t router_ip[IPaddrlen];

	register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);
	register_printf_specifier('M', printf_ipmask, printf_ipmask_info);

	lifc = get_first_noloop_iplifc(NULL, &to_free);
	if (!lifc) {
		fprintf(stderr, "IP addr lookup failed (%r), no VM networking\n");
		return;
	}
	snprintf(my_ip_str, sizeof(my_ip_str), "%i", lifc->ip);
	snprintf(buf, sizeof(buf), "%i%M", lifc->ip, lifc->mask);
	v4parsecidr(host_v4_addr, host_v4_mask, buf);
	free_ipifc(to_free);

	ret = my_router_addr(router_ip, NULL);
	if (ret) {
		fprintf(stderr, "Router lookup failed (%r), no VM networking\n");
		return;
	}
	v6tov4(host_v4_router, router_ip);

	ndb = ndbopen("/net/ndb");
	if (!ndb) {
		fprintf(stderr, "NDB open failed (%r), no VM networking\n");
		return;
	}
	nt = ndbipinfo(ndb, "ip", my_ip_str, &dns, 1);
	if (!nt) {
		fprintf(stderr, "DNS server lookup failed (%r), no VM networking\n");
		return;
	}
	v4parseip(host_v4_dns, nt->val);
	ndbfree(nt);
	ndbclose(ndb);
}

static void set_ip_addrs(void)
{
	get_host_ip_addrs();

	loopback_v4_addr[0] = 127;
	loopback_v4_addr[1] = 0;
	loopback_v4_addr[2] = 0;
	loopback_v4_addr[3] = 1;

	bcast_v4_addr[0] = 255;
	bcast_v4_addr[1] = 255;
	bcast_v4_addr[2] = 255;
	bcast_v4_addr[3] = 255;

	/* even in qemu mode, the guest gets the real DNS IP */
	memcpy(guest_v4_dns, host_v4_dns, IPV4_ADDR_LEN);

	if (vnet_real_ip_addrs) {
		memcpy(guest_v4_addr, host_v4_addr, IPV4_ADDR_LEN);
		memcpy(guest_v4_mask, host_v4_mask, IPV4_ADDR_LEN);
		memcpy(guest_v4_router, host_v4_router, IPV4_ADDR_LEN);
	} else {
		guest_v4_addr[0] = 10;
		guest_v4_addr[1] = 0;
		guest_v4_addr[2] = 2;
		guest_v4_addr[3] = 15;

		guest_v4_mask[0] = 255;
		guest_v4_mask[1] = 255;
		guest_v4_mask[2] = 255;
		guest_v4_mask[3] = 0;

		guest_v4_router[0] = 10;
		guest_v4_router[1] = 0;
		guest_v4_router[2] = 2;
		guest_v4_router[3] = 2;
	}
}

static void tap_inbound_conv(int fd)
{
	struct fd_tap_req tap_req = {0};
	int ret;

	tap_req.fd = fd;
	tap_req.cmd = FDTAP_CMD_ADD;
	tap_req.filter = FDTAP_FILT_READABLE;
	tap_req.ev_q = inbound_evq;
	tap_req.ev_id = fd;
	tap_req.data = NULL;
	ret = sys_tap_fds(&tap_req, 1);
	parlib_assert_perror(ret == 1);
}

/* For every FD tap that fires, make sure the map is on the inbound_todo list
 * and kick the receiver.
 *
 * A map who's FD fires might already be on the list - it's possible for an FD
 * to drain to 0 and get another packet (thus triggering a tap) before
 * __poll_inbound() notices and removes it from the list. */
static void *fdtap_watcher(void *arg)
{
	struct event_msg msg[1];
	struct ip_nat_map *map;

	while (1) {
		uth_blockon_evqs(msg, NULL, 1, inbound_evq);
		map = lookup_map_by_hostfd(msg->ev_type);
		/* Could get an event for an FD/map that has since been reaped. */
		if (!map)
			continue;
		uth_mutex_lock(rx_mtx);
		if (!map->is_on_inbound) {
			map->is_on_inbound = TRUE;
			TAILQ_INSERT_TAIL(&inbound_todo, map, inbound);
			uth_cond_var_broadcast(rx_cv);
		} else {
			kref_put(&map->kref);
		}
		uth_mutex_unlock(rx_mtx);
	}
	return 0;
}

static struct event_queue *get_inbound_evq(void)
{
	struct event_queue *ceq;

	ceq = get_eventq_raw();
	ceq->ev_mbox->type = EV_MBOX_CEQ;
	ceq_init(&ceq->ev_mbox->ceq, CEQ_OR, NR_FILE_DESC_MAX, 128);
	/* As far as flags go, we might not want IPI in the future.  Depends on some
	 * longer range VMM/2LS changes.  Regarding INDIR, if you want to find out
	 * about the event (i.e. not poll) for non-VCPD mboxes (like this evq's
	 * mbox), then you need INDIR.  We need that for the wakeup/blockon. */
	ceq->ev_flags = EVENT_INDIR | EVENT_SPAM_INDIR | EVENT_WAKEUP | EVENT_IPI;
	evq_attach_wakeup_ctlr(ceq);
	return ceq;
}

void vnet_port_forward(char *protocol, char *host_port, char *guest_port)
{
	struct ip_nat_map *map;
	uint8_t proto_nr;

	if (!strcmp("udp", protocol)) {
		proto_nr = IP_UDPPROTO;
	} else if (!strcmp("tcp", protocol)) {
		proto_nr = IP_TCPPROTO;
	} else {
		fprintf(stderr, "Can't port forward protocol %s\n", protocol);
		return;
	}
	map = create_map(proto_nr, atoi(guest_port), host_port, TRUE);
	if (!map) {
		fprintf(stderr, "Failed to set up port forward!");
		exit(-1);
	}
	add_map(map);
}

static void ev_handle_diag(struct event_msg *ev_msg, unsigned int ev_type,
                           void *data)
{
	map_dumper();
}

void vnet_init(struct virtual_machine *vm, struct virtio_vq_dev *vqdev)
{
	set_ip_addrs();
	virtio_net_set_mac(vqdev, guest_eth_addr);
	rx_mtx = uth_mutex_alloc();
	rx_cv = uth_cond_var_alloc();
	if (vnet_snoop)
		snoop_on_virtio();
	init_map_lookup(vm);
	inbound_evq = get_inbound_evq();
	vmm_run_task(vm, fdtap_watcher, NULL);
	if (vnet_map_diagnostics)
		register_ev_handler(EV_FREE_APPLE_PIE, ev_handle_diag, NULL);
}

/* Good DHCP reference:
 * http://www.tcpipguide.com/free/t_TCPIPDynamicHostConfigurationProtocolDHCP.htm
 */
#define DHCP_MAX_OPT_LEN 200
#define DHCP_MAIN_BODY_LEN 236
#define DHCP_RSP_LEN (DHCP_MAIN_BODY_LEN + DHCP_MAX_OPT_LEN)
#define DHCP_LEASE_TIME 3600

#define DHCP_OP_REQ				1
#define DHCP_OP_RSP				2

#define DHCP_MSG_DISCOVER		1
#define DHCP_MSG_OFFER			2
#define DHCP_MSG_REQUEST		3
#define DHCP_MSG_DECLINE		4
#define DHCP_MSG_ACK			5
#define DHCP_MSG_NAK			6
#define DHCP_MSG_RELEASE		7
#define DHCP_MSG_INFORM			8

#define DHCP_MAGIC_COOKIE_1		0x63
#define DHCP_MAGIC_COOKIE_2		0x82
#define DHCP_MAGIC_COOKIE_3		0x53
#define DHCP_MAGIC_COOKIE_4		0x63

#define DHCP_OPT_PAD			0
#define DHCP_OPT_SUBNET			1
#define DHCP_OPT_ROUTER			3
#define DHCP_OPT_DNS			6
#define DHCP_OPT_HOSTNAME		12
#define DHCP_OPT_LEASE			51
#define DHCP_OPT_MSG_TYPE		53
#define DHCP_OPT_SERVER_ID		54
#define DHCP_OPT_END_OF_OPT		255

static int get_dhcp_req_type(struct iovec *iov, int iovcnt)
{
	size_t idx = ETH_HDR_LEN + IPV4_HDR_LEN + UDP_HDR_LEN + DHCP_MAIN_BODY_LEN;

	if (!iov_has_bytes(iov, iovcnt, idx + 4)) {
		fprintf(stderr, "DHCP request too short!\n");
		return -1;
	}
	/* Sanity checks */
	if ((iov_get_byte(iov, iovcnt, idx + 0) != DHCP_MAGIC_COOKIE_1) ||
	    (iov_get_byte(iov, iovcnt, idx + 1) != DHCP_MAGIC_COOKIE_2) ||
	    (iov_get_byte(iov, iovcnt, idx + 2) != DHCP_MAGIC_COOKIE_3) ||
	    (iov_get_byte(iov, iovcnt, idx + 3) != DHCP_MAGIC_COOKIE_4)) {
		fprintf(stderr, "DHCP request didn't have magic cookie!\n");
		return -1;
	}
	/* Some clients might ask us to look in sname or other locations, which is
	 * communicated by an option.  So far, the clients I've seen just use the
	 * main options to communicate the message type. */
	idx += 4;
	while (1) {
		if (!iov_has_bytes(iov, iovcnt, idx + 1)) {
			fprintf(stderr, "DHCP request too short!\n");
			return -1;
		}
		switch (iov_get_byte(iov, iovcnt, idx)) {
		case DHCP_OPT_MSG_TYPE:
			if (!iov_has_bytes(iov, iovcnt, idx + 3)) {
				fprintf(stderr, "DHCP request too short!\n");
				return -1;
			}
			return iov_get_byte(iov, iovcnt, idx + 2);
		case DHCP_OPT_PAD:
			idx += 1;
			break;
		case DHCP_OPT_END_OF_OPT:
			fprintf(stderr, "DHCP request without a type!\n");
			return -1;
		default:
			if (!iov_has_bytes(iov, iovcnt, idx + 2)) {
				fprintf(stderr, "DHCP request too short!\n");
				return -1;
			}
			/* idx + 1 is size of the payload.  Skip the opt, size, payload. */
			idx += 2 + iov_get_byte(iov, iovcnt, idx + 1);
			break;
		}
	}
}

static size_t build_dhcp_response(struct iovec *iov, int iovcnt, uint8_t *buf)
{
	uint8_t *p = buf;
	int req_type;

	*p++ = DHCP_OP_RSP;
	*p++ = ETH_HTYPE_ETH;
	*p++ = ETH_ADDR_LEN;
	*p++ = 0x00;	/* hops */
	/* TODO: copies XID; assumes the inbound packet had standard headers */
	iov_memcpy_from(iov, iovcnt, ETH_HDR_LEN + IPV4_HDR_LEN + UDP_HDR_LEN + 4,
	                p, 4);
	p += 4;
	p += 8;			/* secs, flags, CIADDR */
	memcpy(p, guest_v4_addr, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	memcpy(p, guest_v4_router, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	memcpy(p, guest_v4_router, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	iov_memcpy_from(iov, iovcnt, ETH_ADDR_LEN, p, ETH_ADDR_LEN);
	p += 16;	/* CHaddr has 16 bytes, we only use 6 */
	memcpy(p, dhcp_hostname, strlen(dhcp_hostname));
	p += 64;
	p += 128;

	req_type = get_dhcp_req_type(iov, iovcnt);

	/* DHCP options: Technically, we should be responding with whatever fields
	 * they asked for in their incoming message.  For the most part, there are a
	 * bunch of standard things we can just respond with. */

	*p++ = DHCP_MAGIC_COOKIE_1;
	*p++ = DHCP_MAGIC_COOKIE_2;
	*p++ = DHCP_MAGIC_COOKIE_3;
	*p++ = DHCP_MAGIC_COOKIE_4;

	*p++ = DHCP_OPT_MSG_TYPE;
	*p++ = 1;
	switch (req_type) {
	case DHCP_MSG_DISCOVER:
		*p++ = DHCP_MSG_OFFER;
		break;
	case DHCP_MSG_REQUEST:
		*p++ = DHCP_MSG_ACK;
		break;
	default:
		panic("Unexpected DHCP message type %d", req_type);
	}

	*p++ = DHCP_OPT_SUBNET;
	*p++ = 4;
	memcpy(p, guest_v4_mask, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;

	*p++ = DHCP_OPT_ROUTER;
	*p++ = 4;
	memcpy(p, guest_v4_router, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;

	*p++ = DHCP_OPT_DNS;
	*p++ = 4;
	memcpy(p, guest_v4_dns, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;

	*p++ = DHCP_OPT_HOSTNAME;
	*p++ = strlen(dhcp_guestname);	/* not null-terminated */
	memcpy(p, dhcp_guestname, strlen(dhcp_guestname));
	p += strlen(dhcp_guestname);

	if (req_type == DHCP_MSG_REQUEST) {
		*p++ = DHCP_OPT_LEASE;
		*p++ = 4;
		hnputl(p, DHCP_LEASE_TIME);
		p += 4;

		*p++ = DHCP_OPT_SERVER_ID;
		*p++ = 4;
		memcpy(p, guest_v4_dns, IPV4_ADDR_LEN);
		p += IPV4_ADDR_LEN;

	}

	*p++ = DHCP_OPT_END_OF_OPT;

	return p - buf;
}

/* Builds a UDP packet responding to IOV with buf of payload_sz.  Sent from
 * src_port to dst_port.  Returns the new size, including UDP headers. */
static size_t build_udp_response(struct iovec *iov, int iovcnt, uint8_t *buf,
                                 size_t payload_sz,
                                 uint16_t src_port, uint16_t dst_port)
{
	uint8_t *p = buf;

	hnputs(p, src_port);
	p += 2;
	hnputs(p, dst_port);
	p += 2;
	hnputs(p, payload_sz + UDP_HDR_LEN);
	p += 2;
	/* For v4, we don't need to do the xsum.  It's a minor pain too, since they
	 * xsum parts of the IP header too. */
	hnputs(p, 0);
	p += 2;

	return p - buf + payload_sz;
}

/* Builds an IP packet responding to iov with buf of payload_sz.  Sent from->to
 * with protocol.  Returns the new size, including IP headers.
 *
 * We take parameters for the IP, since some usages won't use the iov's IP (e.g.
 * DHCP). */
static size_t build_ip_response(struct iovec *iov, int iovcnt, uint8_t *buf,
                                size_t payload_sz, uint8_t *from, uint8_t *to,
                                uint8_t protocol)
{
	uint8_t *p = buf;
	uint8_t *xsum_addr;

	*p = 0x45;		/* version, etc */
	p += 2;
	hnputs(p, payload_sz + IPV4_HDR_LEN);
	p += 2;
	p += 4;
	*p = 255; 		/* TTL */
	p += 1;
	*p = protocol;
	p += 1;
	xsum_addr = p;
	p += 2;
	memcpy(p, from, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	memcpy(p, to, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	hnputs(xsum_addr, ip_calc_xsum(buf, IPV4_HDR_LEN));

	return p - buf + payload_sz;
}

/* Builds an ethernet response to iov from buf of payload_sz.  Returns the new
 * size, including ethernet headers. */
static size_t build_eth_response(struct iovec *iov, int iovcnt, uint8_t *buf,
                                 size_t payload_sz, uint16_t ether_type)
{
	uint8_t *p = buf;

	iov_memcpy_from(iov, iovcnt, ETH_OFF_SRC, p, ETH_ADDR_LEN);
	p += ETH_ADDR_LEN;
	memcpy(p, host_eth_addr, ETH_ADDR_LEN);
	p += ETH_ADDR_LEN;
	hnputs(p, ether_type);
	p += 2;

	return p - buf + payload_sz;
}

static void fake_dhcp_response(struct iovec *iov, int iovcnt)
{
	struct buf_pkt *bpkt;
	size_t payload_sz;

	switch (get_dhcp_req_type(iov, iovcnt)) {
	case DHCP_MSG_OFFER:
	case DHCP_MSG_DECLINE:
	case DHCP_MSG_ACK:
	case DHCP_MSG_NAK:
	case DHCP_MSG_RELEASE:
	case DHCP_MSG_INFORM:
		return;
	}
	bpkt = zalloc_bpkt(ETH_HDR_LEN + IPV4_HDR_LEN + UDP_HDR_LEN + DHCP_RSP_LEN);

	payload_sz = build_dhcp_response(iov, iovcnt, bpkt->buf + ETH_HDR_LEN
	                                              + IPV4_HDR_LEN + UDP_HDR_LEN);
	payload_sz = build_udp_response(iov, iovcnt,
	                                bpkt->buf + ETH_HDR_LEN + IPV4_HDR_LEN,
	                                payload_sz, 67, 68);
	/* For offers and initial requests, we definitely need to send it to
	 * 255.255.255.255 (bcast).  For renewals, it seems like that that also
	 * suffices, which seems reasonable. */
	payload_sz = build_ip_response(iov, iovcnt, bpkt->buf + ETH_HDR_LEN,
	                               payload_sz, guest_v4_router, bcast_v4_addr,
	                               IP_UDPPROTO);
	payload_sz = build_eth_response(iov, iovcnt, bpkt->buf, payload_sz,
	                                ETH_TYPE_IPV4);

	assert(payload_sz <= bpkt->sz);
	bpkt->sz = payload_sz;
	inject_buf_pkt(bpkt);
}

static size_t build_arp_response(struct iovec *iov, int iovcnt, uint8_t *buf)
{
	uint8_t *p = buf;

	hnputs(p, ETH_HTYPE_ETH);
	p += 2;
	hnputs(p, ETH_TYPE_IPV4);
	p += 2;
	*p++ = ETH_ADDR_LEN;
	*p++ = IPV4_ADDR_LEN;
	hnputs(p, ARP_OP_RSP);
	p += 2;
	/* SHA: addr they are looking for, which is always host's eth addr */
	memcpy(p, host_eth_addr, ETH_ADDR_LEN);
	p += ETH_ADDR_LEN;
	/* SPA: addr they are looking for, which was the request TPA */
	iov_memcpy_from(iov, iovcnt, ETH_HDR_LEN + ARP_OFF_TPA, p, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;
	/* THA was the SHA of the request */
	iov_memcpy_from(iov, iovcnt, ETH_HDR_LEN + ARP_OFF_SHA, p, ETH_ADDR_LEN);
	p += ETH_ADDR_LEN;
	/* TPA was the SPA of the request */
	iov_memcpy_from(iov, iovcnt, ETH_HDR_LEN + ARP_OFF_SPA, p, IPV4_ADDR_LEN);
	p += IPV4_ADDR_LEN;

	return p - buf;
}

static bool should_ignore_arp(struct iovec *iov, int iovcnt)
{
	size_t arp_off = ETH_HDR_LEN;

	if (iov_get_be16(iov, iovcnt, arp_off + ARP_OFF_OP) != ARP_OP_REQ)
		return TRUE;
	/* ARP probes set the SPA to 0.  Ignore these. */
	if (iov_get_be32(iov, iovcnt, arp_off + ARP_OFF_SPA) == 0)
		return TRUE;
	return FALSE;
}

static void handle_arp_tx(struct iovec *iov, int iovcnt)
{
	struct buf_pkt *bpkt;
	size_t payload_sz;

	if (should_ignore_arp(iov, iovcnt))
		return;
	bpkt = zalloc_bpkt(ETH_HDR_LEN + ARP_PKT_LEN);
	payload_sz = build_arp_response(iov, iovcnt, bpkt->buf + ETH_HDR_LEN);
	payload_sz = build_eth_response(iov, iovcnt, bpkt->buf, payload_sz,
	                                ETH_TYPE_ARP);
	assert(payload_sz <= bpkt->sz);
	bpkt->sz = payload_sz;
	inject_buf_pkt(bpkt);
}

/* Helper for protocols: updates an xsum, given a port number change */
static void xsum_changed_port(struct iovec *iov, int iovcnt, size_t xsum_off,
                              uint16_t old_port, uint16_t new_port)
{
	uint16_t old_port_be, new_port_be;

	/* xsum update expects to work on big endian */
	hnputs(&old_port_be, old_port);
	hnputs(&new_port_be, new_port);
	xsum_update(iov, iovcnt, xsum_off, (uint8_t*)&old_port_be,
	            (uint8_t*)&new_port_be, 2);
}

static struct ip_nat_map *handle_udp_tx(struct iovec *iov, int iovcnt,
                                        size_t udp_off)
{
	uint16_t src_port, dst_port;
	struct ip_nat_map *map;

	if (!iov_has_bytes(iov, iovcnt, udp_off + UDP_HDR_LEN)) {
		fprintf(stderr, "Short UDP header, dropping!\n");
		return NULL;
	}
	src_port = iov_get_be16(iov, iovcnt, udp_off + UDP_OFF_SRC_PORT);
	dst_port = iov_get_be16(iov, iovcnt, udp_off + UDP_OFF_DST_PORT);
	if (dst_port == 67) {
		fake_dhcp_response(iov, iovcnt);
		return NULL;
	}
	map = get_map_by_tuple(IP_UDPPROTO, src_port);
	if (!map)
		return NULL;
	xsum_changed_port(iov, iovcnt, udp_off + UDP_OFF_XSUM, src_port,
	                  map->host_port);
	iov_put_be16(iov, iovcnt, udp_off + UDP_OFF_SRC_PORT, map->host_port);
	return map;
}

static struct ip_nat_map *handle_tcp_tx(struct iovec *iov, int iovcnt,
                                        size_t tcp_off)
{
	uint16_t src_port, dst_port;
	struct ip_nat_map *map;

	if (!iov_has_bytes(iov, iovcnt, tcp_off + TCP_HDR_LEN)) {
		fprintf(stderr, "Short TCP header, dropping!\n");
		return NULL;
	}
	src_port = iov_get_be16(iov, iovcnt, tcp_off + TCP_OFF_SRC_PORT);
	dst_port = iov_get_be16(iov, iovcnt, tcp_off + TCP_OFF_DST_PORT);
	map = get_map_by_tuple(IP_TCPPROTO, src_port);
	if (!map)
		return NULL;
	xsum_changed_port(iov, iovcnt, tcp_off + TCP_OFF_XSUM, src_port,
	                  map->host_port);
	iov_put_be16(iov, iovcnt, tcp_off + TCP_OFF_SRC_PORT, map->host_port);
	return map;
}

static struct ip_nat_map *handle_icmp_tx(struct iovec *iov, int iovcnt,
                                         size_t icmp_off)
{
	/* TODO: we could respond to pings sent to us (router_ip).  For anything
	 * else, we'll need to work with the bypass (if possible, maybe ID it with
	 * the Identifier field and map that to the bypassed conv)). */
	return NULL;
}

/* Some protocols (like TCP and UDP) need to adjust their xsums whenever an IPv4
 * address changes. */
static void ipv4_proto_changed_addr(struct iovec *iov, int iovcnt,
                                    uint8_t protocol, size_t proto_hdr_off,
                                    uint8_t *old_addr, uint8_t *new_addr)
{
	switch (protocol) {
	case IP_UDPPROTO:
		xsum_update(iov, iovcnt, proto_hdr_off + UDP_OFF_XSUM, old_addr,
		            new_addr, IPV4_ADDR_LEN);
		break;
	case IP_TCPPROTO:
		xsum_update(iov, iovcnt, proto_hdr_off + TCP_OFF_XSUM, old_addr,
		            new_addr, IPV4_ADDR_LEN);
		break;
	}
}

/* Helper, changes a packet's IP address, updating xsums.  'which' controls
 * whether we're changing the src or dst address. */
static void ipv4_change_addr(struct iovec *iov, int iovcnt, size_t ip_off,
                             uint8_t protocol, uint8_t proto_hdr_off,
                             uint8_t *old_addr, uint8_t *new_addr, size_t which)
{
	xsum_update(iov, iovcnt, ip_off + IPV4_OFF_XSUM, old_addr, new_addr,
	            IPV4_ADDR_LEN);
	ipv4_proto_changed_addr(iov, iovcnt, protocol, proto_hdr_off, old_addr,
	                        new_addr);
	iov_memcpy_to(iov, iovcnt, ip_off + which, new_addr, IPV4_ADDR_LEN);
}

static size_t ipv4_get_header_len(struct iovec *iov, int iovcnt, size_t ip_off)
{
	/* First byte, lower nibble, times 4. */
	return (iov_get_byte(iov, iovcnt, ip_off + 0) & 0x0f) * 4;
}

static size_t ipv4_get_proto_off(struct iovec *iov, int iovcnt, size_t ip_off)
{
	return ipv4_get_header_len(iov, iovcnt, ip_off) + ip_off;
}

static uint8_t ipv4_get_version(struct iovec *iov, int iovcnt, size_t ip_off)
{
	/* First byte, upper nibble, but keep in the upper nibble location */
	return iov_get_byte(iov, iovcnt, ip_off + 0) & 0xf0;
}

static void handle_ipv4_tx(struct iovec *iov, int iovcnt)
{
	size_t ip_off = ETH_HDR_LEN;
	uint8_t protocol;
	size_t proto_hdr_off;
	struct ip_nat_map *map;
	uint8_t src_addr[IPV4_ADDR_LEN];
	uint8_t dst_addr[IPV4_ADDR_LEN];

	if (!iov_has_bytes(iov, iovcnt, ip_off + IPV4_HDR_LEN)) {
		fprintf(stderr, "Short IPv4 header, dropping!\n");
		return;
	}
	/* It's up to each protocol to give us the ip_nat_map matching the packet
	 * and to change the packet's src port. */
	protocol = iov_get_byte(iov, iovcnt, ip_off + IPV4_OFF_PROTO);
	proto_hdr_off = ipv4_get_proto_off(iov, iovcnt, ip_off);
	switch (protocol) {
	case IP_UDPPROTO:
		map = handle_udp_tx(iov, iovcnt, proto_hdr_off);
		break;
	case IP_TCPPROTO:
		map = handle_tcp_tx(iov, iovcnt, proto_hdr_off);
		break;
	case IP_ICMPPROTO:
		map = handle_icmp_tx(iov, iovcnt, proto_hdr_off);
		break;
	}
	/* If the protocol handler already dealt with it (e.g. via emulation), we
	 * bail out.  o/w, they gave us the remapping we should use to rewrite and
	 * send the packet. */
	if (!map)
		return;
	/* At this point, we have a refcnted map, which will keep the map alive and
	 * its FD open. */
	iov_memcpy_from(iov, iovcnt, ip_off + IPV4_OFF_SRC, src_addr,
	                IPV4_ADDR_LEN);
	iov_memcpy_from(iov, iovcnt, ip_off + IPV4_OFF_DST, dst_addr,
	                IPV4_ADDR_LEN);
	/* If the destination is the ROUTER_IP, then it's really meant to go to the
	 * host (loopback).  In that case, we also need the src to be loopback, so
	 * that the *host's* IP stack recognizes the connection (necessary for
	 * host-initiated connections via static maps). */
	if (!memcmp(dst_addr, guest_v4_router, IPV4_ADDR_LEN)) {
		ipv4_change_addr(iov, iovcnt, ip_off, protocol, proto_hdr_off,
		                 dst_addr, loopback_v4_addr, IPV4_OFF_DST);
		ipv4_change_addr(iov, iovcnt, ip_off, protocol, proto_hdr_off,
		                 src_addr, loopback_v4_addr, IPV4_OFF_SRC);
	} else {
		ipv4_change_addr(iov, iovcnt, ip_off, protocol, proto_hdr_off,
		                 src_addr, host_v4_addr, IPV4_OFF_SRC);
	}
	/* We didn't change the size of the packet, just a few fields.  So we
	 * shouldn't need to worry about iov[] being too big.  This is different
	 * than the receive case, where the guest should give us an MTU-sized iov.
	 * Here, they just gave us whatever they wanted to send.
	 *
	 * However, we still need to drop the ethernet header from the front of the
	 * packet, and just send the IP header + payload. */
	iov_strip_bytes(iov, iovcnt, ETH_HDR_LEN);
	/* As far as blocking goes, this is like blasting out a raw IP packet.  It
	 * shouldn't block, preferring to drop, though there might be some cases
	 * where a qlock is grabbed or the medium/NIC blocks. */
	writev(map->host_data_fd, iov, iovcnt);
	map->is_stale = FALSE;
	kref_put(&map->kref);
}

static void handle_ipv6_tx(struct iovec *iov, int iovcnt)
{
}

/* virtio-net calls this when the guest transmits a packet */
int vnet_transmit_packet(struct iovec *iov, int iovcnt)
{
	uint16_t ether_type;

	if (vnet_snoop)
		writev(snoop_fd, iov, iovcnt);
	if (!iov_has_bytes(iov, iovcnt, ETH_HDR_LEN)) {
		fprintf(stderr, "Short ethernet frame from the guest, dropping!\n");
		return -1;
	}
	ether_type = iov_get_be16(iov, iovcnt, ETH_OFF_ETYPE);
	switch (ether_type) {
	case ETH_TYPE_ARP:
		handle_arp_tx(iov, iovcnt);
		break;
	case ETH_TYPE_IPV4:
		handle_ipv4_tx(iov, iovcnt);
		break;
	case ETH_TYPE_IPV6:
		handle_ipv6_tx(iov, iovcnt);
		break;
	default:
		fprintf(stderr, "Unknown ethertype 0x%x, dropping!\n", ether_type);
		return -1;
	};
	return 0;
}

/* Polls for injected packets, filling the iov[iovcnt] on success and returning
 * the amount.  0 means 'nothing there.' */
static size_t __poll_injection(struct iovec *iov, int iovcnt)
{
	size_t ret;
	struct buf_pkt *bpkt;

	if (STAILQ_EMPTY(&inject_pkts))
		return 0;
	bpkt = STAILQ_FIRST(&inject_pkts);
	STAILQ_REMOVE_HEAD(&inject_pkts, next);
	iov_memcpy_to(iov, iovcnt, 0, bpkt->buf, bpkt->sz);
	ret = bpkt->sz;
	free_bpkt(bpkt);
	return ret;
}

static void handle_udp_rx(struct iovec *iov, int iovcnt, size_t len,
                          struct ip_nat_map *map, size_t udp_off)
{
	assert(len >= udp_off + UDP_HDR_LEN);
	xsum_changed_port(iov, iovcnt, udp_off + UDP_OFF_XSUM,
	                  iov_get_be16(iov, iovcnt, udp_off + UDP_OFF_DST_PORT),
	                  map->guest_port);
	iov_put_be16(iov, iovcnt, udp_off + UDP_OFF_DST_PORT, map->guest_port);
}

static void handle_tcp_rx(struct iovec *iov, int iovcnt, size_t len,
                          struct ip_nat_map *map, size_t tcp_off)
{
	assert(len >= tcp_off + TCP_HDR_LEN);
	xsum_changed_port(iov, iovcnt, tcp_off + TCP_OFF_XSUM,
	                  iov_get_be16(iov, iovcnt, tcp_off + TCP_OFF_DST_PORT),
	                  map->guest_port);
	iov_put_be16(iov, iovcnt, tcp_off + TCP_OFF_DST_PORT, map->guest_port);
}

/* Computes and stores the xsum for the ipv4 header in the iov. */
static void xsum_ipv4_header(struct iovec *iov, int iovcnt, size_t ip_off)
{
	size_t hdr_len = ipv4_get_header_len(iov, iovcnt, ip_off);
	uint8_t buf[hdr_len];

	iov_memcpy_from(iov, iovcnt, ip_off, buf, hdr_len);
	*(uint16_t*)&buf[IPV4_OFF_XSUM] = 0;
	iov_put_be16(iov, iovcnt, ip_off + IPV4_OFF_XSUM,
	             ip_calc_xsum(buf, IPV4_HDR_LEN));
}

static void handle_ipv4_rx(struct iovec *iov, int iovcnt, size_t len,
                           struct ip_nat_map *map)
{
	size_t ip_off = ETH_HDR_LEN;
	uint8_t protocol;
	size_t proto_hdr_off;
	uint8_t src_addr[IPV4_ADDR_LEN];
	uint8_t dst_addr[IPV4_ADDR_LEN];

	protocol = iov_get_byte(iov, iovcnt, ip_off + IPV4_OFF_PROTO);
	proto_hdr_off = ipv4_get_proto_off(iov, iovcnt, ip_off);
	switch (map->protocol) {
	case IP_UDPPROTO:
		handle_udp_rx(iov, iovcnt, len, map, proto_hdr_off);
		break;
	case IP_TCPPROTO:
		handle_tcp_rx(iov, iovcnt, len, map, proto_hdr_off);
		break;
	default:
		panic("Bad proto %d on map for conv FD %d\n", map->protocol,
		      map->host_data_fd);
	}
	iov_memcpy_from(iov, iovcnt, ip_off + IPV4_OFF_SRC, src_addr,
	                IPV4_ADDR_LEN);
	iov_memcpy_from(iov, iovcnt, ip_off + IPV4_OFF_DST, dst_addr,
	                IPV4_ADDR_LEN);
	/* If the src was the host (loopback), the guest thinks the remote is
	 * ROUTER_IP. */
	if (!memcmp(src_addr, loopback_v4_addr, IPV4_ADDR_LEN)) {
		ipv4_change_addr(iov, iovcnt, ip_off, map->protocol, proto_hdr_off,
		                 src_addr, guest_v4_router, IPV4_OFF_SRC);
	}
	/* Interesting case.  If we rewrite it to guest_v4_router, when the guest
	 * responds, *that* packet will get rewritten to loopback.  If we ignore it,
	 * and it's qemu mode, it'll actually work.  If it's real addr mode, the
	 * guest won't send an IP packet out that it thinks is for itself.  */
	if (vnet_real_ip_addrs && !memcmp(src_addr, host_v4_addr, IPV4_ADDR_LEN)) {
		fprintf(stderr, "VNET received packet from host_v4_addr.  Not translating, the guest cannot respond!\n");
	}
	/* Regardless, the dst changes from HOST_IP/loopback to GUEST_IP */
	ipv4_change_addr(iov, iovcnt, ip_off, map->protocol, proto_hdr_off,
	                 dst_addr, guest_v4_addr, IPV4_OFF_DST);
	/* Note we did the incremental xsum for the IP header, but also do a final
	 * xsum.  We need the final xsum in case the kernel's networking stack
	 * messed up the header. */
	xsum_ipv4_header(iov, iovcnt, ip_off);
}

/* NAT / translate an inbound packet iov[len], using map.  If a packet comes in
 * on a certain FD, then it's going to the guest at the appropriate port, no
 * questions asked.
 *
 * The iov has ETH_HDR_LEN bytes at the front.  The data actually read from the
 * conv starts at that offset.  len includes this frontal padding - it's the
 * full length of the real data in the iov + the ethernet header.  len may
 * include data beyond the IP packet length; we often get padding from the
 * kernel networking stack.  Returns the final size of the packet. */
static size_t handle_rx(struct iovec *iov, int iovcnt, size_t len,
                        struct ip_nat_map *map)
{
	size_t ip_off = ETH_HDR_LEN;
	uint8_t version;
	uint16_t ether_type;

	/* The conv is reading from a Qmsg queue.  We should always receive at least
	 * an IPv4 header from the kernel. */
	assert(len >= IPV4_HDR_LEN + ETH_HDR_LEN);
	version = ipv4_get_version(iov, iovcnt, ip_off);
	switch (version) {
	case IP_VER4:
		ether_type = ETH_TYPE_IPV4;
		handle_ipv4_rx(iov, iovcnt, len, map);
		break;
	case IP_VER6:
		ether_type = ETH_TYPE_IPV6;
		/* Technically, this could be a bad actor outside our node */
		panic("Got an IPv6 packet on FD %d!\n", map->host_data_fd);
	default:
		panic("Unexpected IP version 0x%x, probably a bug", version);
	}
	iov_memcpy_to(iov, iovcnt, ETH_OFF_DST, guest_eth_addr, ETH_ADDR_LEN);
	iov_memcpy_to(iov, iovcnt, ETH_OFF_SRC, host_eth_addr, ETH_ADDR_LEN);
	iov_put_be16(iov, iovcnt, ETH_OFF_ETYPE, ether_type);
	return len;
}

/* Polls for inbound packets on the host's FD, filling the iov[iovcnt] on
 * success and returning the amount.  0 means 'nothing there.'
 *
 * Notes on concurrency:
 * - The inbound_todo list is protected by the rx_mtx.  Since we're readv()ing
 *   while holding the mtx (because we're in a FOREACH), we're single threaded
 *   in the RX path.
 * - The inbound_todo list is filled by another thread that puts maps on the
 *   list whenever their FD tap fires.
 * - The maps on the inbound_todo list are refcounted.  It's possible for them
 *   to be reaped and removed from the mapping lookup, but the mapping would
 *   stay around until we drained all of the packets from the inbound conv. */
static size_t __poll_inbound(struct iovec *iov, int iovcnt)
{
	struct ip_nat_map *i, *temp;
	ssize_t pkt_sz = 0;
	struct iovec iov_copy[iovcnt];

	/* We're going to readv ETH_HDR_LEN bytes into the iov.  To do so, we'll use
	 * a separate iov to track this offset.  The iov and iov_copy both point to
	 * the same memory (minus the stripping). */
	memcpy(iov_copy, iov, sizeof(struct iovec) * iovcnt);
	iov_strip_bytes(iov_copy, iovcnt, ETH_HDR_LEN);
	TAILQ_FOREACH_SAFE(i, &inbound_todo, inbound, temp) {
		pkt_sz = readv(i->host_data_fd, iov_copy, iovcnt);
		if (pkt_sz > 0) {
			i->is_stale = FALSE;
			return handle_rx(iov, iovcnt, pkt_sz + ETH_HDR_LEN, i);
		}
		parlib_assert_perror(errno == EAGAIN);
		TAILQ_REMOVE(&inbound_todo, i, inbound);
		i->is_on_inbound = FALSE;
		kref_put(&i->kref);
	}
	return 0;
}

/* virtio-net calls this when it wants us to fill iov with a packet. */
int vnet_receive_packet(struct iovec *iov, int iovcnt)
{
	size_t rx_amt;

	uth_mutex_lock(rx_mtx);
	while (1) {
		rx_amt = __poll_injection(iov, iovcnt);
		if (rx_amt)
			break;
		rx_amt = __poll_inbound(iov, iovcnt);
		if (rx_amt)
			break;
		uth_cond_var_wait(rx_cv, rx_mtx);
	}
	uth_mutex_unlock(rx_mtx);
	iov_trim_len_to(iov, iovcnt, rx_amt);
	if (vnet_snoop)
		writev(snoop_fd, iov, iovcnt);
	return rx_amt;
}
