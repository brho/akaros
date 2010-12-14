/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Rimas's Ethernet-Audio device */

#ifndef ROS_KERN_ETH_AUDIO_H
#define ROS_KERN_ETH_AUDIO_H

#include <devfs.h>
#include <net.h>

#define ETH_AUDIO_RCV_PORT 1792				/* where we listen */
#define ETH_AUDIO_SRC_PORT 1792				/* where we send from */
#define ETH_AUDIO_DST_PORT 1793				/* where we send to */
/* 10 channels * 4 bytes/channel * 32 samples/packet = 1280.  On inbound
 * packets, we have + 2 bytes for a * sequence ID.  On outbound packets, we have
 * 4 bytes for control messages.  For now, we'll use the greater of the two,
 * though this does mean we copy 2 extra bytes (that should be 0) into the
 * mmap'd region.  Just don't read them. */
#define ETH_AUDIO_PAYLOAD_SZ 1284
#define ETH_AUDIO_IP_HDR_SZ 20
#define ETH_AUDIO_HEADER_OFF (ETH_HDR_SZ + ETH_AUDIO_IP_HDR_SZ + UDP_HDR_SZ)
#define ETH_AUDIO_FRAME_SZ (ETH_AUDIO_PAYLOAD_SZ + ETH_AUDIO_HEADER_OFF)


/* Ethernet frame for what one of our packets looks like (since we don't have
 * the infrastructure to build one properly).  This implies that there are no
 * IP or ethernet options. */
struct ethaud_udp_packet {
	struct ethernet_hdr			eth_hdr;
	struct ip_hdr				ip_hdr;
	struct udp_hdr				udp_hdr;
	char						payload[ETH_AUDIO_PAYLOAD_SZ];
} __attribute__((packed));

void eth_audio_init(void);
/* This is called by net subsys when it detects an ethernet audio packet */
void eth_audio_newpacket(void *buf);

#endif /* ROS_KERN_ETH_AUDIO_H */
