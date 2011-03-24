#ifndef ROS_KERN_UDP_H
#define ROS_KERN_UDP_H
#include <net/pbuf.h>
#include <net.h>
#include <bits/netinet.h>
#include <socket.h>

#define UDP_HLEN 8
#define UDP_TTL 255

struct udp_pcb {
		/* ips are in network byte order */
    struct in_addr local_ip;
    struct in_addr remote_ip;
    /** ports are in host byte order */
    uint16_t local_port, remote_port;
    uint8_t ttl;
    uint8_t flags;
    /* Protocol specific PCB members */
    struct udp_pcb *next;
		struct socket *pcbsock;
};

extern struct udp_pcb *udp_pcbs;
#define GLOBAL_IP 0x0A000001 // 10.0.0.1
struct udp_pcb * udp_new(void);
int udp_send(struct udp_pcb *pcb, struct pbuf *p);
int udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                    struct in_addr *dst_ip, uint16_t dst_port);
int udp_bind(struct udp_pcb *pcb, struct in_addr *ip, uint16_t port);
int udp_input(struct pbuf *p);

#define UDP_FLAGS_NOCHKSUM       0x01U
#define UDP_FLAGS_UDPLITE        0x02U
#define UDP_FLAGS_CONNECTED      0x04U
#define UDP_FLAGS_MULTICAST_LOOP 0x08U

#if 0

/** Function prototype for udp pcb receive callback functions
 * addr and port are in same byte order as in the pcb
 * The callback is responsible for freeing the pbuf
 * if it's not used any more.
 *
 * ATTENTION: Be aware that 'addr' points into the pbuf 'p' so freeing this pbuf
 *            makes 'addr' invalid, too.
 *
 * @param arg user supplied argument (udp_pcb.recv_arg)
 * @param pcb the udp_pcb which received data
 * @param p the packet buffer that was received
 * @param addr the remote IP address from which the packet was received
 * @param port the remote port from which the packet was received
 */
typedef void (*udp_recv_fn)(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    ip_addr_t *addr, u16_t port);


/* udp_pcbs export for exernal reference (e.g. SNMP agent) */
extern struct udp_pcb *udp_pcbs;

/* The following functions is the application layer interface to the
   UDP code. */
void             udp_remove     (struct udp_pcb *pcb);
err_t            udp_bind       (struct udp_pcb *pcb, ip_addr_t *ipaddr,
                                 u16_t port);
err_t            udp_connect    (struct udp_pcb *pcb, ip_addr_t *ipaddr,
                                 u16_t port);
void             udp_disconnect (struct udp_pcb *pcb);
void             udp_recv       (struct udp_pcb *pcb, udp_recv_fn recv,
                                 void *recv_arg);
err_t            udp_sendto_if  (struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port,
                                 struct netif *netif);
err_t            udp_sendto     (struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port);
err_t            udp_send       (struct udp_pcb *pcb, struct pbuf *p);

#if LWIP_CHECKSUM_ON_COPY
err_t            udp_sendto_if_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port,
                                 struct netif *netif, u8_t have_chksum,
                                 u16_t chksum);
err_t            udp_sendto_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 ip_addr_t *dst_ip, u16_t dst_port,
                                 u8_t have_chksum, u16_t chksum);
err_t            udp_send_chksum(struct udp_pcb *pcb, struct pbuf *p,
                                 u8_t have_chksum, u16_t chksum);
#endif /* LWIP_CHECKSUM_ON_COPY */

#define          udp_flags(pcb) ((pcb)->flags)
#define          udp_setflags(pcb, f)  ((pcb)->flags = (f))

/* The following functions are the lower layer interface to UDP. */
void             udp_input      (struct pbuf *p, struct netif *inp);

#define udp_init() /* Compatibility define, not init needed. */

#if UDP_DEBUG
void udp_debug_print(struct udp_hdr *udphdr);
#else
#define udp_debug_print(udphdr)
#endif

#ifdef __cplusplus
}
#endif

#endif /* LWIP_UDP */

//#endif /* if 0 , comment out */
#endif /* __LWIP_UDP_H__ */
