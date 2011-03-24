#ifndef ROS_KERN_IP_H
#define ROS_KERN_IP_H
#include <net/pbuf.h>
#include <net.h>
#include <bits/netinet.h>
#define ip_addr_isany(addr1) ((addr1) == NULL || (addr1)->s_addr == IPADDR_ANY)
#define ip_addr_cmp(addr1, addr2) ((addr1)->s_addr == (addr2)->s_addr)
#define ip_match(addr1, addr2) (ip_addr_isany(addr1) || ip_addr_isany(addr2) || ip_addr_cmp(addr1, addr2))

extern struct in_addr global_ip;
int ip_output(struct pbuf *p, struct in_addr *src, struct in_addr *dest, uint8_t proto);
int ip_input(struct pbuf *p);

#endif // ROS_KERN_IP_H
