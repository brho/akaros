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

#include <eth_audio.h>

/** @file
 * @brief Realtek RL8168 Driver
 *
 * EXPERIMENTAL. DO NOT USE IF YOU DONT KNOW WHAT YOU ARE DOING
 *
 * This is a function rl8168 driver, that uses some really ugly hacks to achieve
 * UDP communication with a remote syscall server, without a network stack.
 *
 * To enable use, define __CONFIG_NETWORKING__ in your Makelocal
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

struct Descriptor *CT(NUM_RX_DESCRIPTORS) rx_des_kva;
unsigned long rx_des_pa;

struct Descriptor *CT(NUM_TX_DESCRIPTORS) tx_des_kva;
unsigned long tx_des_pa;

uint32_t rx_des_cur = 0;
uint32_t tx_des_cur = 0;



void rl8168_init() {

	if (rl8168_scan_pci() < 0) return;
	rl8168_read_mac();
	printk("Network Card MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	   device_mac[0],device_mac[1],device_mac[2],
	   device_mac[3],device_mac[4],device_mac[5]);
	rl8168_setup_descriptors();
	rl8168_configure();
	rl8168_setup_interrupts();
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
	struct pci_device *pcidev;
	uint32_t result;
	printk("Searching for RealTek 8168 Network device...");
	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* Ignore non RealTek 8168 Devices */
		if ((pcidev->ven_id != REALTEK_VENDOR_ID) ||
		   (pcidev->dev_id != REALTEK_DEV_ID))
			continue;
		printk(" found on BUS %x DEV %x\n", pcidev->bus, pcidev->dev);
		/* Find the IRQ */
		rl8168_irq = pcidev->irqline;
		rl8168_debug("-->IRQ: %u\n", rl8168_irq);
		/* Loop over the BARs */
		for (int k = 0; k <= 5; k++) {
			int reg = 4 + k;
	        result = pcidev_read32(pcidev, reg << 2);	// SHAME!
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
		rl8168_debug("-->hwrev: %x\n",
		             inl(rl8168_io_base_addr + RL_HWREV_REG) & RL_HWREV_MASK);
		return 0;
	}
	printk(" not found. No device configured.\n");
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
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + rl8168_irq, rl8168_interrupt_handler, 0);
#ifdef __CONFIG_ENABLE_MPTABLES__
	ioapic_route_irq(rl8168_irq, 1);	
#else
	pic_unmask_irq(rl8168_irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	enable_irq();
#endif
	
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

#ifdef __CONFIG_APPSERVER__
	// Treat as a syscall frontend response packet if eth_type says so
	// Will eventually go away, so not too worried about elegance here...
	#include <frontend.h>
	#include <arch/frontend.h>
	uint16_t eth_type = htons(*(uint16_t*)(rx_buffer + 12));
	if(eth_type == APPSERVER_ETH_TYPE) {
		rx_des_cur = rx_des_loop_cur;
		rl8168_process_frame(rx_buffer, frame_size, current_command);
		handle_appserver_packet(rx_buffer, frame_size);
		kfree(rx_buffer);
		return;
	}
#endif
#ifdef __CONFIG_ETH_AUDIO__
	/* TODO: move this, and all packet processing, out of this driver (including
	 * the ghetto buffer).  Note we don't handle IP fragment reassembly (though
	 * this isn't an issue for the eth_audio). */
	struct ethaud_udp_packet *packet = (struct ethaud_udp_packet*)rx_buffer;
	uint8_t protocol = packet->ip_hdr.protocol;
	uint16_t udp_port = ntohs(packet->udp_hdr.dst_port);
	if (protocol == IPPROTO_UDP && udp_port == ETH_AUDIO_RCV_PORT) {
		rx_des_cur = rx_des_loop_cur;
		eth_audio_newpacket(packet);
		kfree(rx_buffer);
		return;
	}
#endif /* __CONFIG_ETH_AUDIO__ */

	spin_lock(&packet_buffers_lock);

	if (num_packet_buffers >= MAX_PACKET_BUFFERS) {
		//printk("WARNING: DROPPING PACKET!\n");
		spin_unlock(&packet_buffers_lock);
		rx_des_cur = rx_des_loop_cur;
		kfree(rx_buffer);
		return;
	}

	packet_buffers[packet_buffers_tail] = rx_buffer;
	packet_buffers_sizes[packet_buffers_tail] = frame_size;
		
	packet_buffers_tail = (packet_buffers_tail + 1) % MAX_PACKET_BUFFERS;
	num_packet_buffers++;

	spin_unlock(&packet_buffers_lock);
				
	rx_des_cur = rx_des_loop_cur;

	// Chew on the frame data. Command bits should be the same for all frags.
	rl8168_process_frame(rx_buffer, frame_size, current_command);
	
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
	if (frame_size < MIN_FRAME_SIZE) {
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
	
	rl8168_frame_debug("--> Sending Packet\n");
	for(int i=0; i<len; i++)
		rl8168_frame_debug("%x ", (unsigned int)(unsigned char)(data[i]));
	rl8168_frame_debug("\n");
	rl8168_frame_debug("--> Sent packet.\n");
	
	outb(rl8168_io_base_addr + RL_TX_CTRL_REG, RL_TX_SEND_MASK);
	
	return len;
}

