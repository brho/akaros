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
 *
 * One of the big things we need to change is the page allocation. Right now we use a page as the max
 * size of a packet. This needs to change. We do it this way right now because that is really our only 
 * allocation mechnism. I don't want to write some sort of kalloc just for the net driver, so until
 * that exists, I'll use a page per packet buffer.
 */



struct Descriptor
{
    unsigned int command,  /* command/status dword */
                 vlan,     /* currently unused */
                 low_buf,  /* low 32-bits of physical buffer address */
                 high_buf; /* high 32-bits of physical buffer address */
};

int rx_buffer_len = PGSIZE;
int num_of_rx_descriptors = PGSIZE/sizeof(struct Descriptor);

int tx_buffer_len = PGSIZE;
int num_of_tx_descriptors = PGSIZE/sizeof(struct Descriptor);


uint32_t io_base_addr = 0;
uint32_t irq = 0;
char mac_address[6];

struct Descriptor *rx_des_kva;
struct Descriptor *rx_des_pa;

struct Descriptor *tx_des_kva;
struct Descriptor *tx_des_pa;

uint32_t rx_des_cur = 0;
uint32_t tx_des_cur = 0;

int eth_up = 0;

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
	
	nic_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & mac_address[0], 0xFF & mac_address[1],	
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

void setup_descriptors() {
	
	nic_debug("-->Setting up tx/rx descriptors.\n");
	
	page_t *rx_des_page = NULL, *tx_des_page = NULL;
			
	if (page_alloc(&rx_des_page) < 0) panic("Can't allocate page for RX Ring");
	if (page_alloc(&tx_des_page) < 0) panic("Can't allocate page for TX Ring");
	
	// extra page_alloc needed because of the strange page_alloc thing
	if (page_alloc(&tx_des_page) < 0) panic("Can't allocate page for TX Ring");
	
	rx_des_kva = page2kva(rx_des_page);
	tx_des_kva = page2kva(tx_des_page);
	
	rx_des_pa = page2pa(rx_des_page);
	tx_des_pa = page2pa(tx_des_page);


    for (int i = 0; i < num_of_rx_descriptors; i++) 
		set_rx_descriptor(i);
		
	for (int i = 0; i < num_of_tx_descriptors; i++) 
		set_tx_descriptor(i);
}

void set_rx_descriptor(uint32_t des_num) {
	
	if (des_num == (num_of_rx_descriptors - 1)) /* Last descriptor? if so, set the EOR bit */
		rx_des_kva[des_num].command = (DES_OWN_MASK | DES_EOR_MASK | (rx_buffer_len & DES_RX_SIZE_MASK));
	else
		rx_des_kva[des_num].command = (DES_OWN_MASK | (rx_buffer_len & DES_RX_SIZE_MASK));
		
	page_t *rx_buf_page;
	if (page_alloc(&rx_buf_page) < 0) panic ("Can't allocate page for RX Buffer");

	rx_des_kva[des_num].low_buf = page2pa(rx_buf_page);
	//.high_buf used if we do 64bit.
}

void set_tx_descriptor(uint32_t des_num) {
	
	tx_des_kva[des_num].command = 0;
	
	if (des_num == (num_of_tx_descriptors - 1)) /* Last descriptor? if so, set the EOR bit */
		tx_des_kva[des_num].command = DES_EOR_MASK;
}

void configure_nic() {
	
	nic_debug("-->Configuring Device.\n");

	outb(io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_UL_MASK); 		// Unlock EPPROM CTRL REG
	outl(io_base_addr + RL_TX_CFG_REG, RL_RX_CFG_MASK); 			// Configure RX
	
    outw(io_base_addr + RL_RX_MXPKT_REG, RL_RX_MAX_SIZE); 			// Set max RX Packet Size
    outb(io_base_addr + RL_TX_MXPKT_REG, RL_TX_MAX_SIZE); 			// Set max TX Packet Size

	// Note: These are the addresses the physical device will use, so we need the physical addresses of the rings
    outl(io_base_addr + RL_TX_DES_REG, (unsigned long)tx_des_pa); 	// Set TX Des Ring Start Addr

    outl(io_base_addr + RL_RX_DES_REG, (unsigned long)rx_des_pa); 	// Set RX Des Ring Start Addr

	// Can't configure TX until enabled in ctrl reg. From spec sheet.
	outb(io_base_addr + RL_CTRL_REG, RL_CTRL_RXTX_MASK); 			// Enable RX and TX in the CTRL Reg
	outl(io_base_addr + RL_TX_CFG_REG, RL_TX_CFG_MASK); 			// Configure TX

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
										rx_des_kva[i].command & DES_RX_SIZE_MASK);				
			}
		}
	}
	
	return;
}

void init_nic() {
	
	if (scan_pci() < 0) return;
	eth_up = 1;
	read_mac();
	setup_interrupts();
	setup_descriptors();
	configure_nic();

/*
	udelay(3000000);
	for (int i = 0; i < 10; i++)  {
		send_packet("                    HELLO THERE", 32);
		udelay(3000000);
	}
*/

/*  This code is for nic stats
	page_t *p;
	page_alloc(&p);
	
	uint32_t low = 0;
	uint32_t high = 0;
	
	low = page2pa(p);
	low = low | 0x8;
	
	outl(io_base_addr + 0x10, low);
	outl(io_base_addr + 0x14, 0x00);
	
	udelay(3000000);
	dump_page(p);
*/

	//poll_rx_descriptors();
	//udelay(3000000);
				
	// Trigger sw based nic interrupt
	//outb(io_base_addr + 0x38, 0x1);
	//udelay(3000000);
	
	//nic_debug("Triggered NIC interrupt.\n");
	
	return;
	
}

// We need to evaluate this routine in terms of concurrency.
// We also need to figure out whats up with different core interrupts
void nic_interrupt_handler(trapframe_t *tf, void* data) {
	
	nic_interrupt_debug("\nNic interrupt on core %u!\n", lapic_get_id());
	
	// Read the offending interrupt(s)
	uint16_t interrupt_status = inw(io_base_addr + RL_IS_REG);

	// We can have multiple interrupts fire at once. I've personally seen this.
	// This means we need to handle this as a series of independent if's
	
	if (interrupt_status & RL_INT_ROK) {
		nic_interrupt_debug("-->RX OK\n");
		nic_handle_rx_packet();
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
	outw(io_base_addr + RL_IS_REG, 0xFFFF);
	return;
}

void process_packet(page_t *packet, uint16_t packet_size, uint32_t command) {
	uint32_t packet_kva = page2kva(packet);
	
	nic_packet_debug("-->Command: %x\n", command);
	nic_packet_debug("-->Size: %u\n", packet_size);
	
	if (packet_size < MINIMUM_PACKET_SIZE) {
		nic_packet_debug("-->Packet too small. Discarding.\n");
		
		page_free(packet);
		return;
	}
	
	char dest_mac[6];
	char source_mac[6];
	char eth_type[2];
	
	for (int i = 0; i < 6; i++) {
		dest_mac[i] = ((char*)packet_kva)[i];
	}
	
	for (int i = 0; i < 6; i++) {
		source_mac[i] = ((char*)packet_kva)[i+6];
	}
	
	eth_type[0] = ((char*)packet_kva)[12];
	eth_type[1] = ((char*)packet_kva)[13];
	
	if (command & DES_MAR_MASK) {
		nic_packet_debug("-->Multicast Packet.\n");
	}
	
	if (command & DES_PAM_MASK) {
		nic_packet_debug("-->Physical Address Matched.\n");
	}
	
	if (command & DES_BAR_MASK) {
		nic_packet_debug("-->Broadcast Packet.\n");
	}
	
	// Note: DEST comes before SRC in the ethernet frame, but that 
	
	nic_packet_debug("-->DEST   MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & dest_mac[0], 0xFF & dest_mac[1],	
	                                                                   0xFF & dest_mac[2], 0xFF & dest_mac[3],	
                                                                       0xFF & dest_mac[4], 0xFF & dest_mac[5]);
	
	nic_packet_debug("-->SOURCE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & source_mac[0], 0xFF & source_mac[1],	
	                                                                   0xFF & source_mac[2], 0xFF & source_mac[3],	
                                                                       0xFF & source_mac[4], 0xFF & source_mac[5]);

	nic_packet_debug("-->ETHR MODE: %02x%02x\n", 0xFF & eth_type[0], 0xFF & eth_type[1]);
		
	return;
}

void nic_handle_rx_packet() {
	
	uint32_t current_command = rx_des_kva[rx_des_cur].command;
	uint16_t packet_size;
	page_t *rx_buf_page_dirty; 		// Packets page with data
	
	nic_packet_debug("-->RX Des: %u\n", rx_des_cur);
	
	// Make sure we are processing from the start of a packet segment
	if (!(current_command & DES_FS_MASK)) {
		nic_packet_debug("-->ERR: Current RX descriptor not marked with FS mask. Panic!");
		panic("RX Descriptor Ring FS out of sync");
	}
	
	// NOTE: We are currently configured that the max packet size is large enough to fit inside 1 descriptor buffer,
	// So we should never be in a situation where a packet spans multiple descriptors.
	// When we change this, this should operate in a loop until the LS mask is found
	// Loop would begin here.
			
	// Make sure we own the current packet. Kernel ownership is denoted by a 0. Nic by a 1.
	if (current_command & DES_OWN_MASK) {
		nic_packet_debug("-->ERR: Current RX descriptor not owned by kernel. Panic!");
		panic("RX Descriptor Ring OWN out of sync");
	}
		
	// Make sure if we are at the end of the buffer, the des is marked as end
	if ((rx_des_cur == (num_of_rx_descriptors - 1)) && !(current_command & DES_EOR_MASK)) {
		nic_packet_debug("-->ERR: Last RX descriptor not marked with EOR mask. Panic!\n");
		panic("RX Descriptor Ring EOR Missing");
	}
	
	// This is ensuring packets only span 1 descriptor, since our packet size cant exceed the buffer of 1 descriptor
	if (!(current_command & DES_LS_MASK)) {
		nic_packet_debug("-->ERR: Current RX descriptor not marked with LS mask. Panic!");
		panic("RX Descriptor Ring LS out of sync");
	}
	
	// Get the packet data
	rx_buf_page_dirty = pa2page(rx_des_kva[rx_des_cur].low_buf);
	packet_size = rx_des_kva[rx_des_cur].command & DES_RX_SIZE_MASK;
	
	// Reset the command register, allocate a new page, and set it
	set_rx_descriptor(rx_des_cur);
	
	// Advance to the next descriptor
	rx_des_cur = (rx_des_cur + 1) % num_of_rx_descriptors;
	
	// Chew on the packet data. This function is responsible for deallocating the memory space.
	process_packet(rx_buf_page_dirty, packet_size, current_command);
		
	return;
}

// Idealy, once we get the packet sizing right, we'd generate multiple descriptors to fit the current packet.
// However, we can't do that now since the max packet size is 1 page, and thats the max buffer size, too.
int send_packet(const char *data, size_t len) {

	if (tx_des_kva[tx_des_cur].command & DES_OWN_MASK) {
		nic_packet_debug("-->TX Ring Buffer Full!\n");
		return -1;
	}
	
	// THIS IS A HACK. MAX PACKET SIZE NEEDS TO BE FIXED BASED ON THE 128/32 QUESTION DEFINED AT THE TOP
	// AS WELL AS THE FACT THAT THE MAX PACKET SIZE INCLUDES THINGS LIKE CRC, AND MAY INCLUDE SRC MAC DEST MAC AND PREAMBLE!
	// IF THE MAX PACKET SIZE INCLUDES THOSE, THIS NEEDS TO BE ADJUSTED BASED ON THAT. 
	// IN OTHER WORDS, PLEASE PLEASE PLEASE FIX THIS.
	if (len > MAX_PACKET_SIZE) {
		nic_packet_debug("-->Packet Too Large!\n");
		return -1;
	}
	
	page_t *tx_buf_page;
	if (page_alloc(&tx_buf_page) < 0) {
		nic_packet_debug("Can't allocate page for TX Buffer");
		return -1;
	}

	tx_des_kva[tx_des_cur].low_buf = page2pa(tx_buf_page);
	
	memcpy(page2kva(tx_buf_page), data, len);

	tx_des_kva[tx_des_cur].command = tx_des_kva[tx_des_cur].command | len | DES_OWN_MASK | DES_FS_MASK | DES_LS_MASK;
	tx_des_kva[tx_des_cur].command = tx_des_kva[tx_des_cur].command | DES_TX_IP_CHK_MASK | DES_TX_UDP_CHK_MASK | DES_TX_TCP_CHK_MASK;
	
	
	tx_des_cur = (tx_des_cur + 1) % num_of_tx_descriptors;
	
	nic_packet_debug("-->Sent packet.\n");
	
	outb(io_base_addr + RL_TX_CTRL_REG, RL_TX_SEND_MASK);
	
	return len;
}

// This function is a complete temp hack
const char *packet_wrap(const char* data, size_t len) {
	
	#define htons(A) ((((uint16_t)(A) & 0xff00) >> 8) | \
	                    (((uint16_t)(A) & 0x00ff) << 8))
	#define htonl(A) ((((uint32_t)(A) & 0xff000000) >> 24) | \
	                    (((uint32_t)(A) & 0x00ff0000) >> 8)  | \
	                    (((uint32_t)(A) & 0x0000ff00) << 8)  | \
	                    (((uint32_t)(A) & 0x000000ff) << 24))

	#define ntohs  htons
	#define ntohl  htohl

	
	
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
	
	// Hard coded to paul's laptop's mac
	char dest_mac_address[6] = {0x00, 0x23, 0x32, 0xd5, 0xae, 0x82};
	
	uint32_t source_ip = 0xC0A8000A; // 192.168.0.10
	uint32_t dest_ip   = 0xC0A8000B; // 192.168.0.11
 
	
	if (len > MAX_PACKET_DATA) {
		nic_packet_debug("Bad packet size for packet wrapping");
		return NULL;
	}
	
	page_t *wrap_page;
	char* wrap_kva;
	
	if (page_alloc(&wrap_page) < 0) {
		nic_packet_debug("Can't allocate page for packet wrapping");
		return NULL;
	}
	
	wrap_kva = page2kva(wrap_page);
	
	struct ETH_Header *eth_header = (struct ETH_Header*) wrap_kva;
	struct IP_Header *ip_header = (struct IP_Header*) (wrap_kva + sizeof(struct ETH_Header));
	struct UDP_Header *udp_header = (struct UDP_Header*) (wrap_kva + sizeof(struct ETH_Header) + sizeof(struct IP_Header));
	
	// Setup eth data
	for (int i = 0; i < 6; i++) 
		eth_header->dest_mac[i] = dest_mac_address[i];
		
	for (int i = 0; i < 6; i++) 
		eth_header->source_mac[i] = mac_address[i];
		
	eth_header->eth_type = htons(0x0800);
	
	// Setup IP data
	ip_header->ip_opts0 = htonl((4<<28) | (5 << 24) | (len + 28));
	ip_header->ip_opts1 = 0;
	ip_header->ip_opts2 = 0x00110a;
	ip_header->source_ip = htonl(source_ip);
	ip_header->dest_ip = htonl(dest_ip);
	
	// Setup UDP Data
	udp_header->source_port = htons(44443);
	udp_header->dest_port = htons(44444);
	udp_header->length = htons(8 + len);
	udp_header->checksum = 0;
	
	memcpy (wrap_kva + PACKET_HEADER_SIZE, data, len);
		
	return wrap_kva;	
}

void zero_page(page_t *page) {
	char *page_kva = page2kva(page);
	
	for (int i = 0; i < PGSIZE; i++ ) 
		page_kva[i] = 0;
}

void dump_page(page_t *page) {
	char *page_kva = page2kva(page);
	
	for (int i = 0; i < PGSIZE; i++ ) 
		cprintf("%02x", page_kva[i]);	
	cprintf("\n");
}