#ifndef ROS_INC_NIC_COMMON_H
#define ROS_INC_NIC_COMMON_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>


// Basic packet sizing
// TODO handle jumbo packets
#define ETHERNET_ENCAP_SIZE	18
#define MTU			1500	
#define MAX_FRAME_SIZE		ETHERNET_ENCAP_SIZE + MTU	
	
// This is to make it simply compile when not in __NETWORK__ mode.
#ifndef USER_MAC_ADDRESS
#define USER_MAC_ADDRESS {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#endif

// v----- Evil line ------v
// Hacky stuff for syscalls go away.

struct ETH_Header
{
	char dest_mac[6];
	char source_mac[6];
	uint16_t eth_type;
};


struct IP_Header
{
	uint32_t ip_opts0;
	uint32_t ip_opts1;
	uint32_t ip_opts2;
	uint32_t source_ip;
	uint32_t dest_ip;
};

struct UDP_Header
{
	uint16_t source_port;
	uint16_t dest_port;
	uint16_t length;
	uint16_t checksum;
};	

#define MINIMUM_PACKET_SIZE 14 // kinda evil. probably evil.
#define MAX_PACKET_SIZE		MTU

#define PACKET_HEADER_SIZE  sizeof(struct packet_header) //IP UDP ETH
#define MAX_PACKET_DATA		MAX_FRAME_SIZE - PACKET_HEADER_SIZE
// This number needs verification! Also, this is a huge hack, as the driver shouldnt care about UDP/IP etc.

struct packet_header {
	struct ETH_Header eth_head;
	struct IP_Header ip_head;
	struct UDP_Header udp_head;
} __attribute__((packed));

struct eth_packet {
	struct packet_header eth_head;
	char data[MTU-PACKET_HEADER_SIZE];
} __attribute__((packed));


// ^----- Evil line ------^

#endif /* !ROS_INC_NIC_COMMON_H */
