#ifndef ROS_KERN_IP_H
#define ROS_KERN_IP_H
#include <net/pbuf.h>
#include <net.h>
#include <bits/netinet.h>
extern struct in_addr global_ip;
int ip_output(struct pbuf *p, struct in_addr *src, struct in_addr *dest, uint8_t proto);

#endif // ROS_KERN_IP_H
