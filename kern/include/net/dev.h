/* Copyright (c) 2010 The Regents of the University of California
 * David Zhu <yuzhu@cs.berkeley.edu> 
 * See LICENSE for details.
 *
 * Simplified network device interface */
#ifndef ROS_KERN_NET_DEV_H
#define ROS_KERN_NET_DEV_H

struct netif {
	/* TODO: next netif so we can build a list of them*/
	struct ip_addr ip_addr;
	void* init;
	netif_init_fn input;
	netif_output_fn send_frame;
	netif_output_fn send_pbuf;
	netif_output_raw output_raw;
}; 





#endif //ROS_KERN_NET_DEV_H
