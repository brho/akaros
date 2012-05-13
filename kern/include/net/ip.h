#ifndef ROS_KERN_IP_H
#define ROS_KERN_IP_H
#include <net/pbuf.h>
#include <net.h>
#include <bits/netinet.h>
#define ip_addr_isany(addr1) ((addr1) == NULL || (addr1)->s_addr == IPADDR_ANY)
#define ip_addr_cmp(addr1, addr2) ((addr1)->s_addr == (addr2)->s_addr)
#define ip_match(addr1, addr2) (ip_addr_isany(addr1) || ip_addr_isany(addr2) || ip_addr_cmp(addr1, addr2))
#define ip_addr_copy(addr1, addr2) ((addr1).s_addr = (addr2).s_addr)

struct in_addr {
    uint32_t s_addr;
};

typedef struct in_addr ip_addr_t;

#define IP_PCB \
/* ips are in network byte order */ \
struct in_addr local_ip; \
struct in_addr remote_ip; \
uint8_t so_options; \
uint8_t tos; \
uint8_t ttl; \
uint8_t addr_hint;

int ip_output(struct pbuf *p, struct in_addr *src, struct in_addr *dest, uint8_t ttl, uint8_t tos, uint8_t proto);
int ip_input(struct pbuf *p);

#endif // ROS_KERN_IP_H
