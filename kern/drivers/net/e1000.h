#ifndef ROS_INC_E1000_H
#define ROS_INC_E1000_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>
#include <net/nic_common.h>
#include <net/pbuf.h>
#if 1
#define e1000_debug(...) 		printk(__VA_ARGS__)  
#define e1000_interrupt_debug(...)	printk(__VA_ARGS__)  
#define e1000_frame_debug(...)		printk(__VA_ARGS__)  
#else
#define e1000_debug(...)
#define e1000_interrupt_debug(...)
#define e1000_frame_debug(...)
#endif

#define E1000_IRQ_CPU		0

#define INTEL_VENDOR_ID		0x8086
/* e1000s.  For some info (and lists of more of these, check out
 * http://pci-ids.ucw.cz/read/PC/8086/  
 * Some notes:
 * 	- in 2009, paul mentioned that the 82576{,NS} are supported by the igb
 * 	driver in Linux, since they support a more advanced feature set.
 * 	- There are many more e1000s.  We could import the list from pci-ids, or
 * 	something more clever.  This list mostly just tracks devices we've seen
 * 	before. */

#define INTEL_82543GC_ID	0x1004
#define INTEL_82540EM_ID	0x100e		/* qemu's device */
#define INTEL_82545EM_ID	0x100f
#define INTEL_82576_ID		0x10c9
#define INTEL_82576NS_ID	0x150a

#include "e1000_hw.h"

// Intel Descriptor Related Sizing
#define E1000_NUM_TX_DESCRIPTORS	2048
#define E1000_NUM_RX_DESCRIPTORS	2048

// This should be in line with the setting of BSIZE in RCTL
#define E1000_RX_MAX_BUFFER_SIZE 2048
#define E1000_TX_MAX_BUFFER_SIZE 2048
#if 0
struct e1000_tx_ring {
	/* pointer to the descriptor ring memory */
	void *desc;
	/* physical address of the descriptor ring */
	dma_addr_t dma;
	/* length of descriptor ring in bytes */
	unsigned int size;
	/* number of descriptors in the ring */
	unsigned int count;
	/* next descriptor to associate a buffer with */
	unsigned int next_to_use;
	/* next descriptor to check for DD status bit */
	unsigned int next_to_clean;
	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;

	spinlock_t tx_lock;
	uint16_t tdh;
	uint16_t tdt;
	boolean_t last_tx_tso;
};

struct e1000_rx_ring {
	/* pointer to the descriptor ring memory */
	void *desc;
	/* physical address of the descriptor ring */
	dma_addr_t dma;
	/* length of descriptor ring in bytes */
	unsigned int size;
	/* number of descriptors in the ring */
	unsigned int count;
	/* next descriptor to associate a buffer with */
	unsigned int next_to_use;
	/* next descriptor to check for DD status bit */
	unsigned int next_to_clean;
	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;
	/* arrays of page information for packet split */
	struct e1000_ps_page *ps_page;
	struct e1000_ps_page_dma *ps_page_dma;

	/* cpu for rx queue */
	int cpu;

	uint16_t rdh;
	uint16_t rdt;
};
struct e1000_adaptor{
	struct e1000_tx_ring tx_ring;
	struct e1000_rx_ring rx_ring;


}
#endif

/* driver private functions */
static uint32_t e1000_rr32(uint32_t offset);
static void e1000_wr32(uint32_t offset, uint32_t val);
#define E1000_WRITE_FLUSH() e1000_rr32(E1000_STATUS)

void e1000_init(void);
void e1000_reset(void);
void e1000_interrupt_handler(struct hw_trapframe *hw_tf, void *data);
int  e1000_scan_pci(void);
void e1000_setup_interrupts(void);
void e1000_setup_descriptors(void);
void e1000_configure(void);
void e1000_handle_rx_packet(void);
void e1000_set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer);
void e1000_set_tx_descriptor(uint32_t des_num);
int  e1000_send_frame(const char* data, size_t len);
int e1000_send_pbuf(struct pbuf *p);
static void e1000_clean_rx_irq();
/* returns a chain of pbuf from the driver */
struct pbuf* e1000_recv_pbuf();
void process_pbuf(uint32_t srcid, long a0, long a1, long a2);
static void schedule_pb(struct pbuf* pb);
#endif /* !ROS_INC_E1000_H */
