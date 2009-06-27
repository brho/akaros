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
#include <kmalloc.h>

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
 * TODO: Remove hacky syscall hack stuff (once we get a real stack).
 * TODO: Jumbo frame support
 * TODO: Use high priority transmit ring for syscall stuff.
 * TODO: Discuss panic conditions.
 * TODO: Shutdown cleanup kfrees()
 * TODO: Use onboard timer interrupt to check for packets, instead of writing a bit each time we have a packet.
 * TODO: CONCURRENCY!
 */



struct Descriptor
{
    unsigned int command,  /* command/status dword */
                 vlan,     /* currently unused */
                 low_buf,  /* low 32-bits of physical buffer address */
                 high_buf; /* high 32-bits of physical buffer address */
};


uint32_t io_base_addr = 0;
uint32_t irq = 0;
char device_mac[6];

struct Descriptor *rx_des_kva;
struct Descriptor *rx_des_pa;

struct Descriptor *tx_des_kva;
struct Descriptor *tx_des_pa;

uint32_t rx_des_cur = 0;
uint32_t tx_des_cur = 0;

uint8_t eth_up = 0;

// Hacky stuff for syscall hack. Go away.
int packet_waiting;
int packet_buffer_size;
char* packet_buffer;
char* packet_buffer_orig;
int packet_buffer_pos = 0;
// End hacky stuff

void init_nic() {
	
	if (scan_pci() < 0) return;
	read_mac();
	setup_descriptors();
	configure_nic();
	setup_interrupts();
	eth_up = 1;
	
	//Trigger sw based nic interrupt
	//outb(io_base_addr + 0x38, 0x1);
	//udelay(3000000);
	
	return;
}


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
			
			// TODO Switch to memory mapped instead of IO?
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
	   device_mac[i] = inb(io_base_addr + RL_MAC_OFFSET + i); 
	
	nic_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & device_mac[0], 0xFF & device_mac[1],	
	                                                            0xFF & device_mac[2], 0xFF & device_mac[3],	
                                                                0xFF & device_mac[4], 0xFF & device_mac[5]);
	return;
}

/*
void setup_descriptors() {
	
	nic_debug("-->Setting up tx/rx descriptors.\n");
	
	page_t *rx_des_page = NULL, *tx_des_page = NULL;
			
	if (page_alloc(&rx_des_page) < 0) panic("Can't allocate page for RX Ring");
	
	if (page_alloc(&tx_des_page) < 0) panic("Can't allocate page for TX Ring");
	
	if (page2pa(tx_des_page) == 0x1000)
		if (page_alloc(&tx_des_page) < 0) panic("Can't allocate page for TX Ring");
	
	// extra page_alloc needed because of the strange page_alloc thing
	if (page_alloc(&tx_des_page) < 0) panic("Can't allocate page for TX Ring");
	
	rx_des_kva = page2kva(rx_des_page);
	tx_des_kva = page2kva(tx_des_page);
	
	rx_des_pa = page2pa(rx_des_page);
	tx_des_pa = page2pa(tx_des_page);

	cprintf("rx_des_page: %x\n", rx_des_pa);
	cprintf("tx_des_page: %x\n", tx_des_pa);
	
    for (int i = 0; i < num_of_rx_descriptors; i++) 
		set_rx_descriptor(i);
		
	for (int i = 0; i < num_of_tx_descriptors; i++) 
		set_tx_descriptor(i);
}
*/

void setup_descriptors() {
	
	nic_debug("-->Setting up tx/rx descriptors.\n");
			
	// Allocate room for the buffers. Include an extra ALIGN space.
	// Buffers need to be on 256 byte boundries.
	// Note: Buffers are alligned by kmalloc automatically to powers of 2 less than the size requested
	// We request more than 256, thus they are aligned on 256 byte boundries
	rx_des_kva = kmalloc(NUM_RX_DESCRIPTORS * sizeof(struct Descriptor), 0);
	tx_des_kva = kmalloc(NUM_TX_DESCRIPTORS * sizeof(struct Descriptor), 0);
	
	if (rx_des_kva == NULL) panic("Can't allocate page for RX Ring");
	if (tx_des_kva == NULL) panic("Can't allocate page for TX Ring");
	
	rx_des_pa = (struct Descriptor *)PADDR(rx_des_kva);
	tx_des_pa = (struct Descriptor *)PADDR(tx_des_kva);
	
    for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
		set_rx_descriptor(i, TRUE); // Allocate memory for the descriptor
		
	for (int i = 0; i < NUM_TX_DESCRIPTORS; i++) 
		set_tx_descriptor(i);
		
	return;
}


void set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer) {
	
	// Set the OWN bit on all descriptors. Also set the buffer size.
	rx_des_kva[des_num].command = (DES_OWN_MASK | (RL_RX_MAX_BUFFER_SIZE & DES_RX_SIZE_MASK));
	
	if (des_num == (NUM_RX_DESCRIPTORS - 1)) 
		rx_des_kva[des_num].command = rx_des_kva[des_num].command | DES_EOR_MASK;
	
	if (reset_buffer) {
		// Must be aligned on 8 byte boundries. Taken care of by kmalloc.
		char *rx_buffer = kmalloc(RL_RX_MAX_BUFFER_SIZE, 0);
	
		if (rx_buffer == NULL) panic ("Can't allocate page for RX Buffer");

		rx_des_kva[des_num].low_buf = PADDR(rx_buffer);
		//.high_buf used if we do 64bit.
	}
	
	return;
}

void set_tx_descriptor(uint32_t des_num) {
	
	// Clear the command bits.
	tx_des_kva[des_num].command = 0;
	
	// Set EOR bit on last descriptor
	if (des_num == (NUM_TX_DESCRIPTORS - 1))
		tx_des_kva[des_num].command = DES_EOR_MASK;	
		
	char *tx_buffer = kmalloc(RL_TX_MAX_BUFFER_SIZE, 0);

	if (tx_buffer == NULL) panic ("Can't allocate page for TX Buffer");

	tx_des_kva[des_num].low_buf = PADDR(tx_buffer);
	//.high_buf used if we do 64bit.
		
	return;
}

void configure_nic() {
	
	// TODO: Weigh resetting the nic. Not really needed. Remove?
	// TODO Check ordering of what we set.
	// TODO Remove C+ register setting?
	
	nic_debug("-->Configuring Device.\n");
	reset_nic();

	// Magic to handle the C+ register. Completely undocumented, ripped from the BSE RE driver.
	outl(io_base_addr + RL_CP_CTRL_REG, RL_CP_MAGIC_MASK);

	// Unlock EPPROM CTRL REG
	outb(io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_UL_MASK); 	
	
	// Set max RX Packet Size
    outw(io_base_addr + RL_RX_MXPKT_REG, RL_RX_MAX_SIZE); 	
		
	// Set max TX Packet Size
    outb(io_base_addr + RL_TX_MXPKT_REG, RL_TX_MAX_SIZE); 			

	// Set TX Des Ring Start Addr
    outl(io_base_addr + RL_TX_DES_REG, (unsigned long)tx_des_pa); 
	
	// Set RX Des Ring Start Addr
    outl(io_base_addr + RL_RX_DES_REG, (unsigned long)rx_des_pa); 	

	// Configure TX
	outl(io_base_addr + RL_TX_CFG_REG, RL_TX_CFG_MASK); 
	
	// Configure RX
	outl(io_base_addr + RL_TX_CFG_REG, RL_RX_CFG_MASK); 			

	// Enable RX and TX in the CTRL Reg
	outb(io_base_addr + RL_CTRL_REG, RL_CTRL_RXTX_MASK); 			

	// Lock the EPPROM Ctrl REG
    outl(io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_L_MASK); 		
	
	return;
}

void reset_nic() {
	
	nic_debug("-->Resetting device..... ");
	outb(io_base_addr + RL_CTRL_REG, RL_CTRL_RESET_MASK);
	
	// Wait for NIC to answer "done resetting" before continuing on
	while (inb(io_base_addr + RL_CTRL_REG) & RL_CTRL_RESET_MASK);
	nic_debug(" done.\n");
	
	return;
}

void setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	nic_debug("-->Setting interrupts.\n");
	
	// Enable NIC interrupts
	outw(io_base_addr + RL_IM_REG, RL_INTERRUPT_MASK);
	
	//Clear the current interrupts.
	outw(io_base_addr + RL_IS_REG, RL_INTRRUPT_CLEAR);
	
	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + irq, nic_interrupt_handler, 0);
	pic_unmask_irq(irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	
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
	outw(io_base_addr + RL_IS_REG, RL_INTRRUPT_CLEAR);
	
	return;
}

// TODO: Does a packet too large get dropped or just set the error bits in the descriptor? Find out.
void nic_handle_rx_packet() {
	
	uint32_t current_command = rx_des_kva[rx_des_cur].command;
	uint16_t packet_size;
		
	nic_frame_debug("-->RX Des: %u\n", rx_des_cur);
	
	// Make sure we are processing from the start of a packet segment
	if (!(current_command & DES_FS_MASK)) {
		nic_frame_debug("-->ERR: Current RX descriptor not marked with FS mask. Panic!");
		panic("RX Descriptor Ring FS out of sync");
	}
	
	// NOTE: We are currently configured that the max packet size is large enough to fit inside 1 descriptor buffer,
	// So we should never be in a situation where a packet spans multiple descriptors.
	// When we change this, this should operate in a loop until the LS mask is found
	// Loop would begin here.
	
	uint32_t rx_des_loop_cur = rx_des_cur;
	uint32_t frame_size = 0;
	uint32_t fragment_size = 0;
	uint32_t num_frags = 0;
	
	char *rx_buffer = kmalloc(MAX_FRAME_SIZE, 0);
	
	if (rx_buffer == NULL) panic ("Can't allocate page for incoming packet.");
	
	do {
		current_command =  rx_des_kva[rx_des_loop_cur].command;
		fragment_size = rx_des_kva[rx_des_loop_cur].command & DES_RX_SIZE_MASK;
		
		// If we've looped through the entire ring and not found a terminating packet, bad nic state.
		// Panic or clear all descriptors? This is a nic hardware error. 
		if (num_frags && (rx_des_loop_cur == rx_des_cur)) {
			//for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
			//	set_rx_descriptor(i, FALSE); // Dont reallocate memory for the descriptor
			// rx_des_cur = 0;
			// return;
			nic_frame_debug("-->ERR: No ending segment found in RX buffer.\n");
			panic("RX Descriptor Ring out of sync.");
		}
		
		num_frags++;
		
		
		// Make sure we own the current packet. Kernel ownership is denoted by a 0. Nic by a 1.
		if (current_command & DES_OWN_MASK) {
			nic_frame_debug("-->ERR: Current RX descriptor not owned by kernel. Panic!");
			panic("RX Descriptor Ring OWN out of sync");
		}
		
		// Make sure if we are at the end of the buffer, the des is marked as end
		if ((rx_des_loop_cur == (NUM_RX_DESCRIPTORS - 1)) && !(current_command & DES_EOR_MASK)) {
			nic_frame_debug("-->ERR: Last RX descriptor not marked with EOR mask. Panic!\n");
			panic("RX Descriptor Ring EOR Missing");
		}
		
		// We set a max frame size and the nic violated that. 
		// Panic or clear all desriptors?
		if ((frame_size + fragment_size) > MAX_FRAME_SIZE) {
			//for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
			//	set_rx_descriptor(i, FALSE); // Dont reallocate memory for the descriptor
			// rx_des_cur = 0;
			// return;
			nic_frame_debug("-->ERR: Nic sent %u byte packet. Max is %u\n", frame_size, MAX_FRAME_SIZE);
			panic("NIC Sent packets larger than configured.");
		}
		
		// Move the fragment data into the buffer
		memcpy(rx_buffer + frame_size, KADDR(rx_des_kva[rx_des_loop_cur].low_buf), fragment_size);
		
		// Reset the descriptor. No reuse buffer.
		set_rx_descriptor(rx_des_loop_cur, FALSE);
		
		// Note: We mask out fragment sizes at 0x3FFFF. There can be at most 1024 of them.
		// This can not overflow the uint32_t we allocated for frame size, so
		// we dont need to worry about mallocing too little then overflowing when we read.
		frame_size = frame_size + fragment_size;
		
		// Advance to the next descriptor
		rx_des_loop_cur = (rx_des_loop_cur + 1) % NUM_RX_DESCRIPTORS;
		
	} while (!(current_command & DES_LS_MASK));
	
	// Hack for UDP syscall hack. 
	// This is a quick hack to let us deal with where to put packets coming in. This is not concurrency friendly
	// In the event that we get 2 incoming frames for our syscall test (shouldnt happen)
	// We cant process more until another packet comes in. This is ugly, but this code goes away as soon as we integrate a real stack.
	// This keys off the source port, fix it for dest port. 
	// Also this may access packet regions that are wrong. If someone addresses empty packet for our interface
	// and the bits that happened to be in memory before are the right port, this will trigger. this is bad
	// but since syscalls are a hack for only 1 machine connected, we dont care for now.
	
	if ((current_command & DES_PAM_MASK) && (*((uint16_t*)(rx_buffer + 36)) == 0x9bad)) {
		
		if (packet_waiting) return;

		packet_buffer = rx_buffer + PACKET_HEADER_SIZE;
		
		// So ugly I want to cry
		packet_buffer_size = *((uint16_t*)(rx_buffer + 38)); 
		packet_buffer_size = (((uint16_t)packet_buffer_size & 0xff00) >> 8) |  (((uint16_t)packet_buffer_size & 0x00ff) << 8);		
		packet_buffer_size = packet_buffer_size - 8;

		packet_buffer_orig = rx_buffer;
		packet_buffer_pos = 0;
		
		packet_waiting = 1;
		
		process_frame(rx_buffer, frame_size, current_command);
		
		rx_des_cur = rx_des_loop_cur;
		
		return;
	}
	
	// END HACKY STUFF
	
	// Chew on the frame data. Command bits should be the same for all frags.
	process_frame(rx_buffer, frame_size, current_command);

	rx_des_cur = rx_des_loop_cur;
	
	kfree(rx_buffer);
	
	return;
}

// This is really more of a debug level function. Will probably go away once we get a stack going.
void process_frame(char *frame_buffer, uint32_t frame_size, uint32_t command) {
		
	nic_frame_debug("-->Command: %x\n", command);
	nic_frame_debug("-->Size: %u\n", frame_size);
	
	if (frame_buffer == NULL)
		return;
	
	// This is hacky. Once we know what our stack will look like, change this.
	// Once remove check for 0 size.
	if (frame_size < MINIMUM_PACKET_SIZE) {
		nic_frame_debug("-->Packet too small. Discarding.\n");
		return;
	}
	
	char dest_mac[6];
	char source_mac[6];
	char eth_type[2];
	
	for (int i = 0; i < 6; i++) {
		dest_mac[i] = frame_buffer[i];
	}
	
	for (int i = 0; i < 6; i++) {
		source_mac[i] = frame_buffer[i+6];
	}
	
	eth_type[0] = frame_buffer[12];
	eth_type[1] = frame_buffer[13];
	
	if (command & DES_MAR_MASK) {
		nic_frame_debug("-->Multicast Packet.\n");
	}
	
	if (command & DES_PAM_MASK) {
		nic_frame_debug("-->Physical Address Matched.\n");
	}
	
	if (command & DES_BAR_MASK) {
		nic_frame_debug("-->Broadcast Packet.\n");
	}
	
	// Note: DEST comes before SRC in the ethernet frame, but that 
	
	nic_frame_debug("-->DEST   MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & dest_mac[0], 0xFF & dest_mac[1],	
	                                                                   0xFF & dest_mac[2], 0xFF & dest_mac[3],	
                                                                       0xFF & dest_mac[4], 0xFF & dest_mac[5]);
	
	nic_frame_debug("-->SOURCE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & source_mac[0], 0xFF & source_mac[1],	
	                                                                   0xFF & source_mac[2], 0xFF & source_mac[3],	
                                                                       0xFF & source_mac[4], 0xFF & source_mac[5]);

	nic_frame_debug("-->ETHR MODE: %02x%02x\n", 0xFF & eth_type[0], 0xFF & eth_type[1]);
		
	return;
}

// Main routine to send a frame. Just sends it and goes.
// Card supports sending across multiple fragments.
// Would we want to write a function that takes a larger packet and generates fragments?
// This seems like the stacks responsibility. Leave this for now. may in future
// Remove the max size cap and generate multiple packets.
int send_frame(const char *data, size_t len) {

	if (data == NULL)
		return -1;
	if (len == 0)
		return 0;

	if (tx_des_kva[tx_des_cur].command & DES_OWN_MASK) {
		nic_frame_debug("-->TX Ring Buffer Full!\n");
		return -1;
	}
	
	if (len > MAX_FRAME_SIZE) {
		nic_frame_debug("-->Frame Too Large!\n");
		return -1;
	}
	
	memcpy(KADDR(tx_des_kva[tx_des_cur].low_buf), data, len);

	tx_des_kva[tx_des_cur].command = tx_des_kva[tx_des_cur].command | len | DES_OWN_MASK | DES_FS_MASK | DES_LS_MASK;

	// For this revision of the NIC, the checksum bits get set in the vlan field not the command field.
	// THIS IS A HACK: Need to reach inside the frame we are sending and detect if its of type ip/udp/tcp and set right flag
	// For now, for the syscall hack, force ip checksum on. (we dont care about udp checksum).
	// Add an argument to function to specify packet type?
	tx_des_kva[tx_des_cur].vlan = DES_TX_IP_CHK_MASK;
	
	tx_des_cur = (tx_des_cur + 1) % NUM_TX_DESCRIPTORS;
	
	//nic_frame_debug("-->Sent packet.\n");
	
	outb(io_base_addr + RL_TX_CTRL_REG, RL_TX_SEND_MASK);
	
	return len;
}

// This function is a complete hack for syscalls until we get a stack.
// the day I delete this monstrosity of code I will be a happy man --Paul
const char *packet_wrap(const char* data, size_t len) {
	
	#define htons(A) ((((uint16_t)(A) & 0xff00) >> 8) | \
	                    (((uint16_t)(A) & 0x00ff) << 8))
	#define htonl(A) ((((uint32_t)(A) & 0xff000000) >> 24) | \
	                    (((uint32_t)(A) & 0x00ff0000) >> 8)  | \
	                    (((uint32_t)(A) & 0x0000ff00) << 8)  | \
	                    (((uint32_t)(A) & 0x000000ff) << 24))

	#define ntohs  htons
	#define ntohl  htohl

	if ((len == 0) || (data == NULL))
		return NULL;
	
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
	//Format for Makelocal file: -DUSER_MAC_ADDRESS="{0x00, 0x23, 0x32, 0xd5, 0xae, 0x82}"
	char dest_mac_address[6] = USER_MAC_ADDRESS;
	
	
	uint32_t source_ip = 0xC0A8000A; // 192.168.0.10
	uint32_t dest_ip   = 0xC0A8000B; // 192.168.0.11
 
	
	if (len > MAX_PACKET_DATA) {
		nic_frame_debug("Bad packet size for packet wrapping");
		return NULL;
	}
	
	char* wrap_buffer = kmalloc(MAX_PACKET_SIZE, 0);
	
	if (wrap_buffer == NULL) {
		nic_frame_debug("Can't allocate page for packet wrapping");
		return NULL;
	}
	
	struct ETH_Header *eth_header = (struct ETH_Header*) wrap_buffer;
	struct IP_Header *ip_header = (struct IP_Header*) (wrap_buffer + sizeof(struct ETH_Header));
	struct UDP_Header *udp_header = (struct UDP_Header*) (wrap_buffer + sizeof(struct ETH_Header) + sizeof(struct IP_Header));
	
	// Setup eth data
	for (int i = 0; i < 6; i++) 
		eth_header->dest_mac[i] = dest_mac_address[i];
		
	for (int i = 0; i < 6; i++) 
		eth_header->source_mac[i] = device_mac[i];
		
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
	
	memcpy (wrap_buffer + PACKET_HEADER_SIZE, data, len);
	
	return wrap_buffer;	
}
