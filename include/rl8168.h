#ifndef ROS_INC_REALTEK_H
#define ROS_INC_REALTEK_H

#include <arch/types.h>
#include <trap.h>
#include <pmap.h>

#define nic_debug(...)  cprintf(__VA_ARGS__)  
#define nic_interrupt_debug(...)  cprintf(__VA_ARGS__)  
#define nic_packet_debug(...)  cprintf(__VA_ARGS__)  


#define MK_CONFIG_ADDR(BUS, DEV, FUNC, REG) (unsigned long)( (BUS << 16) | (DEV << 11) | \
                                                             (FUNC << 8) | REG  | \
                                                             ((uint32_t)0x80000000))
#define PCI_CONFIG_ADDR     0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define REALTEK_VENDOR_ID   0x10ec
#define INVALID_VENDOR_ID   0xFFFF
#define REALTEK_DEV_ID      0x8168

#define PCI_IO_MASK         0xFFF8
#define PCI_MEM_MASK        0xFFFFFFF0
#define PCI_IRQ_MASK		0xFF
#define PCI_VENDOR_MASK		0xFFFF
#define PCI_DEVICE_OFFSET	0x10
#define PCI_IRQ_REG			0x3c

#define PCI_MAX_BUS			256
#define PCI_MAX_DEV			32
#define PCI_BAR_IO_MASK		0x1

#define RL_CTRL_RESET_MASK  0x10
#define RL_CTRL_RXTX_MASK	0x0C

#define RL_HWREV_REG		0x40
#define RL_HWREV_MASK		0x7C800000

#define RL_MAC_OFFSET		0x0

#define RL_CTRL_REG         0x37
#define RL_IM_REG			0x3c
#define RL_IS_REG			0x3E
#define RL_EP_CTRL_REG		0x50
#define RL_RX_CFG_REG 		0x44
#define RL_TX_CFG_REG		0x40
#define RL_RX_MXPKT_REG     0xDA
#define RL_TX_MXPKT_REG     0xEC
#define RL_RX_DES_REG       0xE4
#define RL_TX_DES_REG       0x20
#define RL_TX_CTRL_REG		0x38			

#define RL_RX_MAX_SIZE		0x1000 // This is in units of bytes. 0x1000 = 4096

// !!!!!!!!! need to verify the 128byte nature of this field. Spec says it could be 32 for some chips.
#define RL_TX_MAX_SIZE		0x20   // This is in units of 128bytes, 128 * 0x20 = 4096
#define RL_EP_CTRL_UL_MASK	0xC0
#define RL_EP_CTRL_L_MASK	0x00

#define RL_TX_SEND_MASK		0x40

// NOTE: THESE SHOULD BE BROKEN DOWN INTO A SERIES OF BITS TO REPERSENT THE VARIOUS OPTIONS
// AND THEN THE MASK SHOULD BE DEFINED TO BE AN OR OF THOSE BITS. THIS IS A QUICK HACK JOB.
#define RL_RX_CFG_MASK		0x0000E70F  // RXFTH: unlimited, MXDMA: unlimited, AAP: set (promisc. mode set)
#define RL_TX_CFG_MASK		0x3000700  // IFG: normal, MXDMA: unlimited.  crc appended


#define RL_INT_SERR			0x8000
#define RL_INT_TIMEOUT		0x4000
#define RL_INT_SWINT		0x0100
#define RL_INT_TDU			0x0080
#define RL_INT_FOVW			0x0040
#define RL_INT_LINKCHG		0x0020
#define RL_INT_RDU			0x0010
#define RL_INT_TERR			0x0008
#define RL_INT_TOK			0x0004
#define RL_INT_RERR			0x0002
#define RL_INT_ROK			0x0001

#define RL_INTERRUPT_MASK 	RL_INT_LINKCHG | RL_INT_TOK | RL_INT_ROK | RL_INT_SWINT

#define DES_OWN_MASK		0x80000000
#define DES_EOR_MASK		0x40000000
#define DES_RX_SIZE_MASK	0x3FFF
#define DES_FS_MASK			0x20000000
#define DES_LS_MASK			0x10000000

#define DES_MAR_MASK		0x08000000
#define DES_PAM_MASK		0x04000000
#define DES_BAR_MASK		0x02000000

#define DES_TX_IP_CHK_MASK  0x40000
#define DES_TX_UDP_CHK_MASK 0x20000
#define DES_TX_TCP_CHK_MASK 0x10000


#define KERNEL_IRQ_OFFSET	32

#define MINIMUM_PACKET_SIZE 14
#define MAX_PACKET_SIZE		PGSIZE
#define PACKET_HEADER_SIZE  20 + 8 + 18 //IP UDP ETH
#define MAX_PACKET_DATA		PGSIZE - PACKET_HEADER_SIZE
// This number needs verification! Also, this is a huge hack, as the driver shouldnt care about UDP/IP etc.

void init_nic(void);
void nic_interrupt_handler(trapframe_t *tf, void* data);
int scan_pci(void);
void read_mac(void);
void setup_interrupts(void);
void setup_descriptors(void);
void configure_nic(void);
void poll_rx_descriptors(void);
void nic_handle_rx_packet(void);
void set_rx_descriptor(uint32_t des_num);
void set_tx_descriptor(uint32_t des_num);
void process_packet(page_t *packet, uint16_t packet_size, uint32_t command);
int send_packet(const char *data, size_t len);
const char *packet_wrap(const char* data, size_t len);
void zero_page(page_t *page);
void dump_page(page_t *page);

#endif /* !ROS_INC_REALTEK_H */
