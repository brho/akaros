#ifndef ROS_KERN_UDP_H
#define ROS_KERN_UDP_H
#include <net/pbuf.h>
#include <net/ip.h>
#include <net.h>
#include <bits/netinet.h>
#include <socket.h>

#define UDP_HLEN 8
#define UDP_TTL 255


struct udp_pcb {
		IP_PCB;
    /** ports are in host byte order */
    uint16_t local_port, remote_port;
    uint8_t flags;
		uint8_t pad2;
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
int udp_bind(struct udp_pcb *pcb, const struct in_addr *ip, uint16_t port);
int udp_input(struct pbuf *p);

#define UDP_FLAGS_NOCHKSUM       0x01U
#define UDP_FLAGS_UDPLITE        0x02U
#define UDP_FLAGS_CONNECTED      0x04U
#define UDP_FLAGS_MULTICAST_LOOP 0x08U

#endif
