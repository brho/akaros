/* Copyright (c) 2010 The Regents of the University of California
 * David Zhu <yuzhu@cs.berkeley.edu> 
 * See LICENSE for details.
 *
 * Simplified network device interface */
#ifndef ROS_KERN_NET_DEV_H
#define ROS_KERN_NET_DEV_H

#include <bits/netinet.h>
#include <stdio.h>
#include <socket.h>
#include <ros/common.h>

struct net_device_ops {
	int (*init)(struct netif *netif);
	int (*send_frame) (struct netif *netif, const char data, size_t len) ;
	int (*send_pbuf) (struct netif *netif, const struct pbuf* pb);
	int (*recv_pbuf) (struct netif *netif, const struct pbuf* pb);
}

struct netif {
	/* TODO: next netif so we can build a list of them*/
	struct in_addr ip_addr;
	struct in_addr gw;
	uint16_t mtu;
	uint8_t mac[6];
	struct net_device_ops ops;
	uint8_t eth_up;   // status 
}; 


#endif //ROS_KERN_NET_DEV_H
