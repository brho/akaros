/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch independent networking infrastructure */

#ifndef ROS_KERN_NET_H
#define ROS_KERN_NET_H

#include <bits/netinet.h>
#include <stdio.h>

/* A few other useful standard defines.  Note the IP header can change size. */
#define ETH_HDR_SZ 14
#define UDP_HDR_SZ 8
#define IP_ETH_TYPE 0x0800

/* ROS defaults: */
#define DEFAULT_TTL 64

/* Don't forget the bytes are in network order */
struct ethernet_hdr {
	uint8_t						dst_mac[6];
	uint8_t						src_mac[6];
	uint16_t					eth_type;
	/* might be an optional 802.1q tag here */
};

/* For the bit-enumerated fields, note that you need to read "backwards" through
 * the byte (first bits in memory are the "LSB" of the byte).  Can't seem to be
 * able to do it with flags/fragments (3/13 bits each...). */
struct ip_hdr {
	unsigned					hdr_len : 4;
	unsigned					version : 4;
	uint8_t						tos;
	uint16_t					packet_len;
	uint16_t					id;
	uint16_t					flags_frags;
	uint8_t						ttl;
	uint8_t						protocol;
	uint16_t					checksum;
	uint32_t					src_addr;
	uint32_t					dst_addr;
	/* Options could be here (depends on the hdr length) */
} __attribute__((packed));

struct udp_hdr {
	uint16_t					src_port;
	uint16_t					dst_port;
	uint16_t					length;
	uint16_t					checksum;
};

uint16_t __ip_checksum(void *buf, unsigned int len, uint32_t sum);
uint16_t ip_checksum(struct ip_hdr *ip_hdr);
uint16_t udp_checksum(struct ip_hdr *ip_hdr, struct udp_hdr *udp_hdr);

// TODO: Move this to a better location
void dumppacket(unsigned char *buff, size_t len);
#endif /* ROS_KERN_NET_H */
