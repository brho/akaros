/** @filec
 * @brief RL8168 Driver       
 *
 * EXPERIMENTAL. DO NOT USE IF YOU DONT KNOW WHAT YOU ARE DOING
 *
 * See Info below 
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/smp.h>
#include <arch/apic.h>
#include <arch/pci.h>
#include <arch/rl8168.h>

#include <ros/memlayout.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <kmalloc.h>

#include <pmap.h>

/** @file
 * @brief Realtek RL8168 Driver
 *
 * EXPERIMENTAL. DO NOT USE IF YOU DONT KNOW WHAT YOU ARE DOING
 *
 * This is a function rl8168 driver, that uses some really ugly hacks to achieve
 * UDP communication with a remote syscall server, without a network stack.
 *
 * To enable use, define __NETWORK__ in your Makelocal
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Move documention below into doxygen format.
 * @todo See list in code
 */


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


uint32_t rl8168_io_base_addr = 0;
uint32_t rl8168_irq = 0;
char device_mac[6];

struct Descriptor *CT(NUM_RX_DESCRIPTORS) rx_des_kva;
unsigned long rx_des_pa;

struct Descriptor *CT(NUM_TX_DESCRIPTORS) tx_des_kva;
unsigned long tx_des_pa;

uint32_t rx_des_cur = 0;
uint32_t tx_des_cur = 0;

extern int eth_up;
extern int packet_waiting;
extern int packet_buffer_size;
extern char *CT(MAX_FRAME_SIZE - PACKET_HEADER_SIZE) packet_buffer;
extern char *CT(MAX_FRAME_SIZE) packet_buffer_orig;
extern int packet_buffer_pos;

extern char *CT(PACKET_HEADER_SIZE + len) (*packet_wrap)(const char *CT(len) data, size_t len);
extern int (*send_frame)(const char *CT(len) data, size_t len);


void rl8168_init() {

	if (rl8168_scan_pci() < 0) return;
	rl8168_read_mac();
	rl8168_setup_descriptors();
	rl8168_configure();
	rl8168_setup_interrupts();
      	packet_wrap = &rl8168_packet_wrap;
        send_frame = &rl8168_send_frame;

	eth_up = 1;
	
	//Trigger sw based nic interrupt
/*	cprintf("Generating interrupt...\n");
	outb(rl8168_io_base_addr + 0x38, 0x1);
	cprintf("sleeping\n");
	udelay(3000000);
	cprintf("done\n");
*/
	return;
}


int rl8168_scan_pci() {
	
	extern pci_dev_entry_t pci_dev_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];
	extern uint16_t pci_irq_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

	cprintf("Searching for RealTek 8168 Network device...");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {
				uint32_t address;
				uint32_t bus = i;
				uint32_t dev = j;
				uint32_t func = k;
				uint32_t reg = 0; 
				uint32_t result  = 0;
	
				uint16_t dev_id = pci_dev_map[i][j][k].dev_id;
				uint16_t ven_id = pci_dev_map[i][j][k].ven_id;

				// Vender DNE
				if (ven_id == INVALID_VENDOR_ID) 
					continue;

				// Ignore non RealTek 8168 Devices
				if (ven_id != REALTEK_VENDOR_ID || dev_id != REALTEK_DEV_ID)
					continue;
				cprintf(" found on BUS %x DEV %x\n", i, j);

				// Find the IRQ
				rl8168_irq = pci_irq_map[i][j][k];
				rl8168_debug("-->IRQ: %u\n", rl8168_irq);

				// Loop over the BARs
				for (int k = 0; k <= 5; k++) {
					reg = 4 + k;
					address = MK_CONFIG_ADDR(bus, dev, func, reg << 2);	
			        outl(PCI_CONFIG_ADDR, address);
			        result = inl(PCI_CONFIG_DATA);
					
					if (result == 0) // (0 denotes no valid data)
						continue;

					// Read the bottom bit of the BAR. 
					if (result & PCI_BAR_IO_MASK) {
						result = result & PCI_IO_MASK;
						rl8168_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
					} else {
						result = result & PCI_MEM_MASK;
						rl8168_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
					}
			
					// TODO Switch to memory mapped instead of IO?
					if (k == 0) // BAR0 denotes the IO Addr for the device
						rl8168_io_base_addr = result;						
				}
		
		rl8168_debug("-->hwrev: %x\n", inl(rl8168_io_base_addr + RL_HWREV_REG) & RL_HWREV_MASK);
		
		return 0;
	}
	cprintf(" not found. No device configured.\n");
	
	return -1;
}

void rl8168_read_mac() {
	
	for (int i = 0; i < 6; i++)
	   device_mac[i] = inb(rl8168_io_base_addr + RL_MAC_OFFSET + i); 
	
	rl8168_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & device_mac[0], 0xFF & device_mac[1],	
	                                                            0xFF & device_mac[2], 0xFF & device_mac[3],	
                                                                0xFF & device_mac[4], 0xFF & device_mac[5]);
	return;
}

void rl8168_setup_descriptors() {
	
	rl8168_debug("-->Setting up tx/rx descriptors.\n");
			
	// Allocate room for the buffers. 
	// Buffers need to be on 256 byte boundries.
	// Note: We use get_cont_pages to force page alignment, and thus 256 byte aligned

        uint32_t num_rx_pages = ROUNDUP(NUM_RX_DESCRIPTORS * sizeof(struct Descriptor), PGSIZE) / PGSIZE;
        uint32_t num_tx_pages = ROUNDUP(NUM_TX_DESCRIPTORS * sizeof(struct Descriptor), PGSIZE) / PGSIZE;
	
	rx_des_kva = get_cont_pages(LOG2_UP(num_rx_pages), 0);
	tx_des_kva = get_cont_pages(LOG2_UP(num_tx_pages), 0);

	if (rx_des_kva == NULL) panic("Can't allocate page for RX Ring");
	if (tx_des_kva == NULL) panic("Can't allocate page for TX Ring");
	
	rx_des_pa = PADDR(rx_des_kva);
	tx_des_pa = PADDR(tx_des_kva);
	
    for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
		rl8168_set_rx_descriptor(i, TRUE); // Allocate memory for the descriptor
		
	for (int i = 0; i < NUM_TX_DESCRIPTORS; i++) 
		rl8168_set_tx_descriptor(i);
		
	return;
}


void rl8168_set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer) {
	
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

void rl8168_set_tx_descriptor(uint32_t des_num) {
	
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

void rl8168_configure() {
	
	// TODO: Weigh resetting the nic. Not really needed. Remove?
	// TODO Check ordering of what we set.
	// TODO Remove C+ register setting?
	
	rl8168_debug("-->Configuring Device.\n");
	rl8168_reset();

	// Magic to handle the C+ register. Completely undocumented, ripped from the BSE RE driver.
	outl(rl8168_io_base_addr + RL_CP_CTRL_REG, RL_CP_MAGIC_MASK);

	// Unlock EPPROM CTRL REG
	outb(rl8168_io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_UL_MASK); 	
	
	// Set max RX Packet Size
    outw(rl8168_io_base_addr + RL_RX_MXPKT_REG, RL_RX_MAX_SIZE); 	
		
	// Set max TX Packet Size
    outb(rl8168_io_base_addr + RL_TX_MXPKT_REG, RL_TX_MAX_SIZE); 			

	// Set TX Des Ring Start Addr
    outl(rl8168_io_base_addr + RL_TX_DES_REG, (unsigned long)tx_des_pa); 
	
	// Set RX Des Ring Start Addr
    outl(rl8168_io_base_addr + RL_RX_DES_REG, (unsigned long)rx_des_pa); 	

	// Configure TX
	outl(rl8168_io_base_addr + RL_TX_CFG_REG, RL_TX_CFG_MASK); 
	
	// Configure RX
	outl(rl8168_io_base_addr + RL_TX_CFG_REG, RL_RX_CFG_MASK); 			

	// Enable RX and TX in the CTRL Reg
	outb(rl8168_io_base_addr + RL_CTRL_REG, RL_CTRL_RXTX_MASK); 			

	// Lock the EPPROM Ctrl REG
    outl(rl8168_io_base_addr + RL_EP_CTRL_REG, RL_EP_CTRL_L_MASK); 		
	
	return;
}

void rl8168_reset() {
	
	rl8168_debug("-->Resetting device..... ");
	outb(rl8168_io_base_addr + RL_CTRL_REG, RL_CTRL_RESET_MASK);
	
	// Wait for NIC to answer "done resetting" before continuing on
	while (inb(rl8168_io_base_addr + RL_CTRL_REG) & RL_CTRL_RESET_MASK);
	rl8168_debug(" done.\n");
	
	return;
}

void rl8168_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	rl8168_debug("-->Setting interrupts.\n");
	
	// Enable NIC interrupts
	outw(rl8168_io_base_addr + RL_IM_REG, RL_INTERRUPT_MASK);
	
	//Clear the current interrupts.
	outw(rl8168_io_base_addr + RL_IS_REG, RL_INTRRUPT_CLEAR);
	
	// Kernel based interrupt stuff
#ifdef __IVY__
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + rl8168_irq, rl8168_interrupt_handler, (void *)0);
#else
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + rl8168_irq, rl8168_interrupt_handler, 0);
#endif
	ioapic_route_irq(rl8168_irq, NE2K_IRQ_CPU);	
	
	return;
}

// We need to evaluate this routine in terms of concurrency.
// We also need to figure out whats up with different core interrupts
void rl8168_interrupt_handler(trapframe_t *tf, void* data) {
	
	rl8168_interrupt_debug("\nNic interrupt on core %u!\n", lapic_get_id());
				
	// Read the offending interrupt(s)
	uint16_t interrupt_status = inw(rl8168_io_base_addr + RL_IS_REG);

	// Clear interrupts immediately so we can get the flag raised again.
	outw(rl8168_io_base_addr + RL_IS_REG, interrupt_status);
	
	// Loop to deal with TOCTOU 
	while (interrupt_status != 0x0000) {
		// We can have multiple interrupts fire at once. I've personally seen this.
		// This means we need to handle this as a series of independent if's
		if (interrupt_status & RL_INT_ROK) {
			rl8168_interrupt_debug("-->RX OK\n");
			rl8168_handle_rx_packet();
		}	
	
		if (interrupt_status & RL_INT_RERR) {
			rl8168_interrupt_debug("-->RX ERR\n");			
		}
	
		if (interrupt_status & RL_INT_TOK) {
			rl8168_interrupt_debug("-->TX OK\n");
		}
	
		if (interrupt_status & RL_INT_TERR) {
			rl8168_interrupt_debug("-->TX ERR\n");			
		}
	
		if (interrupt_status & RL_INT_RDU) {
			rl8168_interrupt_debug("-->RX Descriptor Unavailable\n");			
		}
	
		if (interrupt_status & RL_INT_LINKCHG) {
			rl8168_interrupt_debug("-->Link Status Changed\n");			
		}
	
		if (interrupt_status & RL_INT_FOVW) {
			rl8168_interrupt_debug("-->RX Fifo Overflow\n");			
		}
	
		if (interrupt_status & RL_INT_TDU) {
			rl8168_interrupt_debug("-->TX Descriptor Unavailable\n");			
		}
	
		if (interrupt_status & RL_INT_SWINT) {
			rl8168_interrupt_debug("-->Software Generated Interrupt\n");
		}
	
		if (interrupt_status & RL_INT_TIMEOUT) {
			rl8168_interrupt_debug("-->Timer Expired\n");
		}
	
		if (interrupt_status & RL_INT_SERR) {
			rl8168_interrupt_debug("-->PCI Bus System Error\n");			
		}
	
		rl8168_interrupt_debug("\n");
		
		// Clear interrupts	
		interrupt_status = inw(rl8168_io_base_addr + RL_IS_REG);
		outw(rl8168_io_base_addr + RL_IS_REG, interrupt_status);
	}
	
	// In the event that we got really unlucky and more data arrived after we set 
	//  set the bit last, try one more check
	rl8168_handle_rx_packet();
	return;
}

// TODO: Does a packet too large get dropped or just set the error bits in the descriptor? Find out.
// TODO: Should we move on to look for the next descriptor? is it safe? TOCTOU
void rl8168_handle_rx_packet() {
	
	uint32_t current_command = rx_des_kva[rx_des_cur].command;
	uint16_t packet_size;
	
	if (current_command & DES_OWN_MASK) {
		rl8168_frame_debug("-->Nothing to process. Returning.");
		return;
	}
		
	rl8168_frame_debug("-->RX Des: %u\n", rx_des_cur);
	
	// Make sure we are processing from the start of a packet segment
	if (!(current_command & DES_FS_MASK)) {
		rl8168_frame_debug("-->ERR: Current RX descriptor not marked with FS mask. Panic!");
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
			rl8168_frame_debug("-->ERR: No ending segment found in RX buffer.\n");
			panic("RX Descriptor Ring out of sync.");
		}
		
		num_frags++;
		
		
		// Make sure we own the current packet. Kernel ownership is denoted by a 0. Nic by a 1.
		if (current_command & DES_OWN_MASK) {
			rl8168_frame_debug("-->ERR: Current RX descriptor not owned by kernel. Panic!");
			panic("RX Descriptor Ring OWN out of sync");
		}
		
		// Make sure if we are at the end of the buffer, the des is marked as end
		if ((rx_des_loop_cur == (NUM_RX_DESCRIPTORS - 1)) && !(current_command & DES_EOR_MASK)) {
			rl8168_frame_debug("-->ERR: Last RX descriptor not marked with EOR mask. Panic!\n");
			panic("RX Descriptor Ring EOR Missing");
		}
		
		// We set a max frame size and the nic violated that. 
		// Panic or clear all desriptors?
		if ((frame_size + fragment_size) > MAX_FRAME_SIZE) {
			//for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
			//	set_rx_descriptor(i, FALSE); // Dont reallocate memory for the descriptor
			// rx_des_cur = 0;
			// return;
			rl8168_frame_debug("-->ERR: Nic sent %u byte packet. Max is %u\n", frame_size, MAX_FRAME_SIZE);
			panic("NIC Sent packets larger than configured.");
		}
		
		// Move the fragment data into the buffer
		memcpy(rx_buffer + frame_size, KADDR(rx_des_kva[rx_des_loop_cur].low_buf), fragment_size);
		
		// Reset the descriptor. No reuse buffer.
		rl8168_set_rx_descriptor(rx_des_loop_cur, FALSE);
		
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
		
		// So ugly I want to cry
		packet_buffer_size = *((uint16_t*)(rx_buffer + 38)); 
		packet_buffer_size = (((uint16_t)packet_buffer_size & 0xff00) >> 8) |  (((uint16_t)packet_buffer_size & 0x00ff) << 8);		
		packet_buffer_size = packet_buffer_size - 8;

		packet_buffer = rx_buffer + PACKET_HEADER_SIZE;

		packet_buffer_orig = rx_buffer;
		packet_buffer_pos = 0;
		
		packet_waiting = 1;
		
		rl8168_process_frame(rx_buffer, frame_size, current_command);
		
		rx_des_cur = rx_des_loop_cur;
		
		return;
	}
	
	// END HACKY STUFF
	
	// Chew on the frame data. Command bits should be the same for all frags.
	rl8168_process_frame(rx_buffer, frame_size, current_command);

	rx_des_cur = rx_des_loop_cur;
	
	kfree(rx_buffer);
	
	return;
}

// This is really more of a debug level function. Will probably go away once we get a stack going.
void rl8168_process_frame(char *frame_buffer, uint32_t frame_size, uint32_t command) {
		
	rl8168_frame_debug("-->Command: %x\n", command);
	rl8168_frame_debug("-->Size: %u\n", frame_size);
	
	if (frame_buffer == NULL)
		return;
	
	// This is hacky. Once we know what our stack will look like, change this.
	// Once remove check for 0 size.
	if (frame_size < MINIMUM_PACKET_SIZE) {
		rl8168_frame_debug("-->Packet too small. Discarding.\n");
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
		rl8168_frame_debug("-->Multicast Packet.\n");
	}
	
	if (command & DES_PAM_MASK) {
		rl8168_frame_debug("-->Physical Address Matched.\n");
	}
	
	if (command & DES_BAR_MASK) {
		rl8168_frame_debug("-->Broadcast Packet.\n");
	}
	
	// Note: DEST comes before SRC in the ethernet frame, but that 
	
	rl8168_frame_debug("-->DEST   MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & dest_mac[0], 0xFF & dest_mac[1],	
	                                                                     0xFF & dest_mac[2], 0xFF & dest_mac[3],	
                                                                             0xFF & dest_mac[4], 0xFF & dest_mac[5]);
	
	rl8168_frame_debug("-->SOURCE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & source_mac[0], 0xFF & source_mac[1],	
	                                                                     0xFF & source_mac[2], 0xFF & source_mac[3],	
                                                                             0xFF & source_mac[4], 0xFF & source_mac[5]);

	rl8168_frame_debug("-->ETHR MODE: %02x%02x\n", 0xFF & eth_type[0], 0xFF & eth_type[1]);
		
	return;
}

// Main routine to send a frame. Just sends it and goes.
// Card supports sending across multiple fragments.
// Would we want to write a function that takes a larger packet and generates fragments?
// This seems like the stacks responsibility. Leave this for now. may in future
// Remove the max size cap and generate multiple packets.
int rl8168_send_frame(const char *data, size_t len) {

	if (data == NULL)
		return -1;
	if (len == 0)
		return 0;

	if (tx_des_kva[tx_des_cur].command & DES_OWN_MASK) {
		rl8168_frame_debug("-->TX Ring Buffer Full!\n");
		return -1;
	}
	
	if (len > MAX_FRAME_SIZE) {
		rl8168_frame_debug("-->Frame Too Large!\n");
		return -1;
	}
	
	memcpy(KADDR(tx_des_kva[tx_des_cur].low_buf), data, len);

	tx_des_kva[tx_des_cur].command = tx_des_kva[tx_des_cur].command | len | DES_OWN_MASK | DES_FS_MASK | DES_LS_MASK;

	// For this revision of the NIC, the checksum bits get set in the vlan field not the command field.
	// THIS IS A HACK: Need to reach inside the frame we are sending and detect if its of type ip/udp/tcp and set right flag
	// For now, for the syscall hack, force ip checksum on. (we dont care about udp checksum).
	// Add an argument to function to specify packet type?
	//tx_des_kva[tx_des_cur].vlan = DES_TX_IP_CHK_MASK;
	tx_des_kva[tx_des_cur].vlan = 0;


	tx_des_cur = (tx_des_cur + 1) % NUM_TX_DESCRIPTORS;
	
	//rl8168_frame_debug("-->Sent packet.\n");
	
	outb(rl8168_io_base_addr + RL_TX_CTRL_REG, RL_TX_SEND_MASK);
	
	return len;
}

// This function is a complete hack for syscalls until we get a stack.
// the day I delete this monstrosity of code I will be a happy man --Paul
char *CT(PACKET_HEADER_SIZE + len) rl8168_packet_wrap(const char* data, size_t len) {
	
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

	// Hard coded to paul's laptop's mac
	//Format for Makelocal file: -DUSER_MAC_ADDRESS="{0x00, 0x23, 0x32, 0xd5, 0xae, 0x82}"
	char dest_mac_address[6] = USER_MAC_ADDRESS;
	
	
	uint32_t source_ip = 0xC0A8000A; // 192.168.0.10
	uint32_t dest_ip   = 0xC0A8000B; // 192.168.0.11
 
	
	if (len > MAX_PACKET_DATA) {
		rl8168_frame_debug("Bad packet size for packet wrapping");
		return NULL;
	}
	
	struct eth_packet* wrap_buffer = kmalloc(MAX_PACKET_SIZE, 0);
	
	if (wrap_buffer == NULL) {
		rl8168_frame_debug("Can't allocate page for packet wrapping");
		return NULL;
	}
	

	struct ETH_Header *eth_header = &wrap_buffer->eth_head.eth_head;
	struct IP_Header *ip_header = &wrap_buffer->eth_head.ip_head;
	struct UDP_Header *udp_header = &wrap_buffer->eth_head.udp_head;
	
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
	
	memcpy (&wrap_buffer->data[0], data, len);
	
	return (char *CT(PACKET_HEADER_SIZE + len))wrap_buffer;	
}
