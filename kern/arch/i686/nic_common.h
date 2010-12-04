#ifndef ROS_INC_NIC_COMMON_H
#define ROS_INC_NIC_COMMON_H

#include <ros/common.h>
#include <trap.h>
#include <net.h>
#include <pmap.h>

// Packet sizes
#define MTU              1500
/* 14 for the header, 4 for something else.  It's either the CRC at the end
 * (which seems to be 0'd), or for the optional 802.1q tag. */
#define MAX_FRAME_SIZE   (MTU + 18)
#define MIN_FRAME_SIZE   60 // See the spec...

// Maximum packet buffers we can handle at any given time
#define MAX_PACKET_BUFFERS    32 //1024
 
// Global send_frame function pointer
// Means we can only have one network card per system right now...
extern int (*send_frame)(const char *data, size_t len);

// Global variables for managing ethernet packets over a nic
// Again, since these are global for all network cards we are 
// limited to only one for now
extern unsigned char device_mac[6];
extern uint8_t eth_up;
extern uint32_t num_packet_buffers;
extern char* packet_buffers[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_sizes[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_head;
extern uint32_t packet_buffers_tail;
extern spinlock_t packet_buffers_lock; 

// Creates a new ethernet packet and puts the header on it
char* eth_wrap(const char* data, size_t len, char src_mac[6], 
               char dest_mac[6], uint16_t eth_type);

struct ETH_Header {
	char dest_mac[6];
	char source_mac[6];
	uint16_t eth_type;
};

struct eth_frame {
	struct ETH_Header eth_head;
	char data[MTU];
} __attribute__((packed));

#endif /* !ROS_INC_NIC_COMMON_H */
