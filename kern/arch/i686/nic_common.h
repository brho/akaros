#ifndef ROS_INC_NIC_COMMON_H
#define ROS_INC_NIC_COMMON_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>

// Host to network format conversions and vice-versa
static inline uint16_t htons(uint16_t x)
{
	__asm__ ("xchgb %%al,%%ah" : "=a" (x) : "a" (x));
	return x;
}

static inline uint32_t htonl(uint32_t x)
{
	__asm__ ("bswapl %0" : "=r" (x) : "0" (x));
	return x;
}
#define ntohs htons
#define ntohl htonl

#ifdef __CONFIG_OSDI__
#define PACKETIZER_ETH_TYPE 0xabcd
#define PACKETIZER_MAX_PAYLOAD 1024
struct packetizer_packet
{
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint16_t seqno;
    uint32_t payload_size;
    char payload[PACKETIZER_MAX_PAYLOAD];
};

static void print_packetizer_packet(struct packetizer_packet *p)
{
	printk("packetizer_packet:\n");
	printk("  dst_mac: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	        p->dst_mac[0],p->dst_mac[1],p->dst_mac[2],
	        p->dst_mac[3],p->dst_mac[4],p->dst_mac[5]);
	printk("  src_mac: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	        p->src_mac[0],p->src_mac[1],p->src_mac[2],
	        p->src_mac[3],p->src_mac[4],p->src_mac[5]);
	printk("  ethertype: 0x%02x\n", ntohs(p->ethertype));
	printk("  seqno: %u\n", ntohs(p->seqno));
	printk("  payload_size: %u\n", ntohl(p->payload_size));
}

struct fillmeup {
	struct proc *proc;
	uint8_t *bufs;
	uint16_t num_bufs;
	int32_t *last_written;
};
extern struct fillmeup fillmeup_data;
#endif

// Packet sizes
#define MTU              1500
#define MAX_FRAME_SIZE   (MTU + 14)
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
