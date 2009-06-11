#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/smp.h>
#include <arch/apic.h>

#include <ros/memlayout.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <rl8168.h>
#include <trap.h>

#include <pmap.h>

/* RealTek 8168d (8111d) NIC Driver
 *
 * Written by Paul Pearce.
 *
 * This is a really rough "driver". Really, its not a driver, just a kernel hack to give
 * the kernel a way to receive and send packets. The basis of the init code is the OSDEV
 * page on the 8169 chipset, which is a varient of this chipset (most 8169 drivers work 
 * on the 8168d). http://wiki.osdev.org/RTL8169
 * 
 * Basic ideas (although no direct code) were gleamed from the OpenBSD re(4) driver,
 * which can be found in sys/dev/ic/re.c. sys/dev/ic/rtl81x9reg.h is needed to make
 * sense of the constants used in re.c.
 *
 * This is an ongoing work in progress. Main thing is we need a kernel interface for PCI
 * devices and network devices, that we can hook into, instead of providing arbitary functions
 */

#define nic_debug(...)  cprintf(__VA_ARGS__)  
#define nic_interrupt_debug(...)  cprintf(__VA_ARGS__)  


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
#define RL_RX_CTRL_REG      0x44
#define RL_TX_CTRL_REG      0x40
#define RL_RX_MXPKT_REG     0xDA
#define RL_TX_MXPKT_REG     0xEC
#define RL_RX_DES_REG       0xE4
#define RL_TX_DES_REG       0x20

#define RL_RX_MAX_SIZE		0x1000 // This is in units of bytes. 0x1000 = 4096
#define RL_TX_MAX_SIZE		0x20   // This is in units of 128bytes, 128 * 0x20 = 4096
#define RL_EP_CTRL_UL_MASK	0xC0
#define RL_EP_CTRL_L_MASK	0x00

// NOTE: THESE SHOULD BE BROKEN DOWN INTO A SERIES OF BITS TO REPERSENT THE VARIOUS OPTIONS
// AND THEN THE MASK SHOULD BE DEFINED TO BE AN OR OF THOSE BITS. THIS IS A QUICK HACK JOB.
#define RL_RX_CFG_MASK		0x0000E70F  // RXFTH: unlimited, MXDMA: unlimited, AAP: set (promisc. mode set)
#define RL_TX_CFG_MASK		0x03000700  // IFG: normal, MXDMA: unlimited
#define RL_INTERRUPT_MASK	0xFFFF      // All enabled



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


#define DES_OWN_MASK		0x80000000
#define DES_EOR_MASK		0x40000000
#define DES_SIZE_MASK		0x3FFF
#define DES_FS_MASK			0x20000000
#define DES_LS_MASK			0x10000000

#define KERNEL_IRQ_OFFSET	32


struct Descriptor
{
    unsigned int command,  /* command/status dword */
                 vlan,     /* currently unused */
                 low_buf,  /* low 32-bits of physical buffer address */
                 high_buf; /* high 32-bits of physical buffer address */
};


uint32_t io_base_addr = 0;
uint32_t irq = 0;
char mac_address[6];

int rx_buffer_len = PGSIZE;
int num_of_rx_descriptors = PGSIZE/sizeof(struct Descriptor);

struct Descriptor *rx_des_kva;
struct Descriptor *rx_des_pa;

struct Descriptor *tx_des_kva;
struct Descriptor *tx_des_pa;

struct Descriptor *rx_des_cur;
struct Descriptor *tx_des_cur;


int scan_pci() {
	
	uint32_t address;
	uint32_t lbus = 0;
	uint32_t ldev = 0;
	uint32_t lfunc = 0; // We only look at function 0 for now.
	uint32_t lreg = 0; 
	uint32_t result  = 0;
 
	cprintf("Searching for RealTek 8168 Network device......");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++) {

		lbus = i;
		ldev = j;
		lreg = 0; // PCI REGISTER 0

		address = MK_CONFIG_ADDR(lbus, ldev, lfunc, lreg); 

		// Probe current bus/dev
		outl(PCI_CONFIG_ADDR, address);
		result = inl(PCI_CONFIG_DATA);
	
		uint16_t dev_id = result >> PCI_DEVICE_OFFSET;
		uint16_t ven_id = result & PCI_VENDOR_MASK;

		// Vender DNE
		if (ven_id == INVALID_VENDOR_ID) 
			continue;

		// Ignore non RealTek 8168 Devices
		if (ven_id != REALTEK_VENDOR_ID || dev_id != REALTEK_DEV_ID)
			continue;
		cprintf(" found on BUS %x DEV %x\n", i, j);

		// Find the IRQ
		address = MK_CONFIG_ADDR(lbus, ldev, lfunc, PCI_IRQ_REG);
		outl(PCI_CONFIG_ADDR, address);
		irq = inl(PCI_CONFIG_DATA) & PCI_IRQ_MASK;
		nic_debug("-->IRQ: %u\n", irq);

		// Loop over the BARs
		for (int k = 0; k <= 5; k++) {
			lreg = 4 + k;
			address = MK_CONFIG_ADDR(lbus, ldev, lfunc, lreg << 2);	
	        outl(PCI_CONFIG_ADDR, address);
	        result = inl(PCI_CONFIG_DATA);
					
			if (result == 0) // (0 denotes no valid data)
				continue;

			// Read the bottom bit of the BAR. 
			if (result & PCI_BAR_IO_MASK) {
				result = result & PCI_IO_MASK;
				nic_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
			} else {
				result = result & PCI_MEM_MASK;
				nic_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
			}

			if (k == 0) // BAR0 denotes the IO Addr for the device
				io_base_addr = result;						
		}
		
		nic_debug("-->hwrev: %x\n", inl(io_base_addr + RL_HWREV_REG) & RL_HWREV_MASK);
		
		return 0;
	}
	cprintf(" not found. No device configured.\n");
	
	return -1;
}

void read_mac() {
	
	for (int i = 0; i < 6; i++)
	   mac_address[i] = inb(io_base_addr + RL_MAC_OFFSET + i); 
	
	nic_debug("-->DEVICE MAC: %x:%x:%x:%x:%x:%x\n", 0xFF & mac_address[0], 0xFF & mac_address[1],	
	                                                0xFF & mac_address[2], 0xFF & mac_address[3],	
                                                    0xFF & mac_address[4], 0xFF & mac_address[5]);
	return;
}

void reset_nic() {
	
	nic_debug("-->Resetting device..... ");
	outb(io_base_addr + RL_CTRL_REG, RL_CTRL_RESET_MASK);
	while (inb(io_base_addr + RL_CTRL_REG) & RL_CTRL_RESET_MASK);
	nic_debug(" done.\n");
}

void setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	nic_debug("-->Setting interrupts.\n");
	
	// Enable all NIC interrupts only
	outw(io_base_addr + RL_IM_REG, RL_INTERRUPT_MASK);
	
	//Clear the current interrupts.
	outw(io_base_addr + RL_IS_REG, RL_INTERRUPT_MASK);
	
	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + irq, nic_interrupt_handler, 0);
	pic_unmask_irq(irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	enable_irq();
}

void setup_rx_descriptors() {
	
	nic_debug("-->Setting up rx descriptors.\n");
	
	page_t *rx_des_page = NULL, *tx_des_page = NULL;
			
	page_alloc(&rx_des_page);
	page_alloc(&tx_des_page);
	
	rx_des_kva = page2kva(rx_des_page);
	tx_des_kva = page2kva(tx_des_page);
	
	rx_des_pa = page2pa(rx_des_page);
	tx_des_pa = page2pa(tx_des_page);

	
    for (int i = 0; i < num_of_rx_descriptors; i++) 
    {
        if (i == (num_of_rx_descriptors - 1)) /* Last descriptor? if so, set the EOR bit */
			rx_des_kva[i].command = (DES_OWN_MASK | DES_EOR_MASK | (rx_buffer_len & DES_SIZE_MASK));
        else
			rx_des_kva[i].command = (DES_OWN_MASK | (rx_buffer_len & DES_SIZE_MASK));
		
		page_t *rx_buf_page;
		page_alloc(&rx_buf_page);
        rx_des_kva[i].low_buf = page2pa(rx_buf_page); 
		//.high_buf used if we do 64bit.
    }

	rx_des_cur = rx_des_kva;
}

void configure_nic() {
	
	nic_debug("-->Configuring Device.\n");

	outb(io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_UL_MASK); 		// Unlock EPPROM CTRL REG
	outl(io_base_addr + RL_RX_CTRL_REG, RL_RX_CFG_MASK); 			// Configure RX
	outl(io_base_addr + RL_TX_CTRL_REG, RL_TX_CFG_MASK); 			// Configure TX
    outw(io_base_addr + RL_RX_MXPKT_REG, RL_RX_MAX_SIZE); 			// Set max RX Packet Size
    outb(io_base_addr + RL_TX_MXPKT_REG, RL_TX_MAX_SIZE); 			// Set max TX Packet Size

	// Note: These are the addresses the physical device will use, so we need the physical addresses of the rings
    outl(io_base_addr + RL_TX_DES_REG, (unsigned long)tx_des_pa); 	// Set TX Des Ring Start Addr
    outl(io_base_addr + RL_RX_DES_REG, (unsigned long)rx_des_pa); 	// Set RX Des Ring Start Addr

    outb(io_base_addr + RL_CTRL_REG, RL_CTRL_RXTX_MASK); 			// Enable RX and TX in the CTRL Reg
    outl(io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_L_MASK); 		// Unlock the EPPROM Ctrl REG
	
	return;
}

void poll_rx_descriptors() {
	
	while (1) {
		udelay(3000000);
		int seen = 0;
		for (int i = 0; i < num_of_rx_descriptors; i++) {
			
			if ((rx_des_kva[i].command & DES_OWN_MASK) == 0) {
				cprintf("des: %u Status: %x OWN: %u FS: %u LS: %u SIZE: %u\n", 
										i, 
										rx_des_kva[i].command, 
										rx_des_kva[i].command & DES_OWN_MASK, 
										((rx_des_kva[i].command & DES_FS_MASK) != 0),
										((rx_des_kva[i].command & DES_LS_MASK) != 0),
										rx_des_kva[i].command & DES_SIZE_MASK);				
			}
		}
	}
	
	return;
}

void init_nic() {
	
	if (scan_pci() < 0) return;
	read_mac();
	setup_interrupts();
	setup_rx_descriptors();
	configure_nic();

	poll_rx_descriptors();
				
	// Trigger sw based nic interrupt
	//outb(io_base_addr + 0x38, 0x1);
	//nic_debug("Triggered NIC interrupt.\n");
	
	return;
	
}
// We need to evaluate this routine in terms of concurrency.
// We also need to figure out whats up with different core interrupts
void nic_interrupt_handler(trapframe_t *tf, void* data) {
	
	nic_interrupt_debug("\nNic interrupt!\n");
	
	// Read the offending interrupt(s)
	uint16_t interrupt_status = inw(io_base_addr + RL_IS_REG);

	// We can have multiple interrupts fire at once
	// I've personally seen this. I saw RL_INT_LINKCHG and RL_INT_ROK fire at the same time.
	// This means we need to handle this as a series of independent if's
	
	if (interrupt_status & RL_INT_ROK) {
		nic_interrupt_debug("-->RX OK\n");
	}	
	
	if (interrupt_status & RL_INT_RERR) {
		nic_interrupt_debug("-->RX ERR\n");
	}
	
	if (interrupt_status & RL_INT_TOK) {
		nic_interrupt_debug("-->TX OK\n");
	}
	
	if (interrupt_status & RL_INT_TERR) {
		nic_interrupt_debug("-->TX ERR\n");
	}
	
	if (interrupt_status & RL_INT_RDU) {
		nic_interrupt_debug("-->RX Descriptor Unavailable\n");
	}
	
	if (interrupt_status & RL_INT_LINKCHG) {
		nic_interrupt_debug("-->Link Status Changed\n");
	}
	
	if (interrupt_status & RL_INT_FOVW) {
		nic_interrupt_debug("-->RX Fifo Overflow\n");
	}
	
	if (interrupt_status & RL_INT_TDU) {
		nic_interrupt_debug("-->TX Descriptor Unavailable\n");
	}
	
	if (interrupt_status & RL_INT_SWINT) {
		nic_interrupt_debug("-->Software Generated Interrupt\n");
	}
	
	if (interrupt_status & RL_INT_TIMEOUT) {
		nic_interrupt_debug("-->Timer Expired\n");
	}
	
	if (interrupt_status & RL_INT_SERR) {
		nic_interrupt_debug("-->PCI Bus System Error\n");
	}
	
	nic_interrupt_debug("\n");
		
	// Clear interrupts	
	outw(io_base_addr + RL_IS_REG, RL_INTERRUPT_MASK);
	return;
}

void nic_handle_rx_packet() {
	return;
}
