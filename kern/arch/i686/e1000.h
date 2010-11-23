#ifndef ROS_INC_E1000_H
#define ROS_INC_E1000_H

#include <ros/common.h>
#include <trap.h>
#include <pmap.h>
#include <arch/nic_common.h>

#define e1000_debug(...) 		//printk(__VA_ARGS__)  
#define e1000_interrupt_debug(...)	//printk(__VA_ARGS__)  
#define e1000_frame_debug(...)		//printk(__VA_ARGS__)  

#define E1000_IRQ_CPU		0

#define INTEL_VENDOR_ID		0x8086
#define INTEL_DEV_ID0		0x100E	// Real E1000
#define INTEL_DEV_ID1		0x10c9	// 82576
#define INTEL_DEV_ID2		0x150a	// 82576 NS


/********** THIS NEXT FILE IS GPL'D! **************/
#include <arch/e1000_hw.h>
/********** Back to our regularly scheduled BSD **/

// Offset used for indexing IRQs
#define KERNEL_IRQ_OFFSET	32

// Intel Descriptor Related Sizing
#define E1000_NUM_TX_DESCRIPTORS	2048
#define E1000_NUM_RX_DESCRIPTORS	2048

// This should be in line with the setting of BSIZE in RCTL
#define E1000_RX_MAX_BUFFER_SIZE 2048
#define E1000_TX_MAX_BUFFER_SIZE 2048

uint32_t e1000_rr32(uint32_t offset);
void e1000_wr32(uint32_t offset, uint32_t val);

void e1000_init(void);
void e1000_reset(void);
void e1000_interrupt_handler(trapframe_t* tf, void* data);
int  e1000_scan_pci(void);
void e1000_setup_interrupts(void);
void e1000_setup_descriptors(void);
void e1000_configure(void);
void e1000_handle_rx_packet(void);
void e1000_set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer);
void e1000_set_tx_descriptor(uint32_t des_num);
int  e1000_send_frame(const char* data, size_t len);

#endif /* !ROS_INC_E1000_H */
