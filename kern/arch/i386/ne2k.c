/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/smp.h>
#include <arch/apic.h>
#include <arch/pci.h>
#include <arch/ne2k.h>

#include <ros/memlayout.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <kmalloc.h>

#include <pmap.h>
#include <timing.h>

/** @file
 * @brief NE2K Driver Sketch
 *
 * EXPERIMENTAL.
 *
 * Rough driver. Appears to work in QEMU. Probably completely broken under heavy load.
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Everything
 */

#define NE2K_RESET_R_ADDR 0x1f
#define NE2K_PG0_RW_CR	0x00
#define NE2K_PG0_RW_ISR 0x07
#define NE2K_PG0_W_IMR	0x0F
#define NE2K_PG0_W_PSTRT 0x1
#define NE2K_PG0_W_PSTP 0x2
#define NE2K_PG0_W_RCR 0xC
#define NE2K_PG0_R_RSR 0xC
#define NE2K_PG0_R_TSR 0x4
#define NE2K_PG0_W_TCR 0xD
#define NE2K_PG1_RW_PAR 0x1
#define NE2K_PG0_W_RSAR0 0x08
#define NE2K_PG0_W_RSAR1 0x09
#define NE2K_PG0_W_RBCR0 0x0A
#define NE2K_PG0_W_RBCR1 0x0B
#define NE2K_PG0_W_TBCR0 0x05
#define NE2K_PG0_W_TBCR1 0x06
#define NE2K_PG0_W_TPSR  0x04
#define NE2K_PG0_W_DCR 0x0E
#define NE2K_PG1_RW_CURR 0x07
#define NE2K_PG0_RW_BNRY 0x03

#define NE2K_PAGE_SIZE 256

#define NE2K_PMEM_START   (16*1024)
#define NE2K_PMEM_SIZE	  (32*1024)
#define NE2K_NUM_PAGES 		((NE2K_PMEM_SIZE - NE2K_PMEM_START) / NE2K_PAGE_SIZE)
#define NE2K_NUM_RECV_PAGES 	(NE2K_NUM_PAGES / 2)
#define NE2K_NUM_SEND_PAGES	(NE2K_NUM_PAGES / 2)
#define NE2K_FIRST_RECV_PAGE	(NE2K_PMEM_START / NE2K_PAGE_SIZE)
#define NE2K_LAST_RECV_PAGE	NE2K_FIRST_RECV_PAGE + NE2K_NUM_RECV_PAGES
#define NE2K_FIRST_SEND_PAGE	NE2K_LAST_RECV_PAGE + 1


extern uint32_t eth_up; // Fix this                               
uint32_t ne2k_irq;      // And this
uint32_t ne2k_io_base_addr;
char device_mac[6];

void* base_page;
uint32_t num_pages = 0;

extern char* (*packet_wrap)(const char *CT(len) data, size_t len);
extern int (*send_frame)(const char *CT(len) data, size_t len);


void ne2k_init() {
	
	if (ne2k_scan_pci() < 0) return;
	ne2k_mem_alloc();
	ne2k_configure_nic();
	ne2k_read_mac();
	//ne2k_test_interrupts();

	packet_wrap = &ne2k_packet_wrap;
	send_frame = &ne2k_send_frame;

        ne2k_setup_interrupts();

	eth_up = 1;
	
	return;
}


int ne2k_scan_pci() {
	
	extern pci_dev_entry_t pci_dev_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];
	extern uint16_t pci_irq_map[PCI_MAX_BUS][PCI_MAX_DEV][PCI_MAX_FUNC];

	cprintf("Searching for NE2000 Network device...");

	for (int i = 0; i < PCI_MAX_BUS; i++)
		for (int j = 0; j < PCI_MAX_DEV; j++)
			for (int k = 0; k < PCI_MAX_FUNC; k++) {
				uint32_t address;
				uint32_t lbus = i;
				uint32_t ldev = j;
				uint32_t lfunc = k;
				uint32_t lreg = 0; 
				uint32_t result  = 0;
	
				uint16_t dev_id = pci_dev_map[i][j][k].dev_id;
				uint16_t ven_id = pci_dev_map[i][j][k].ven_id;

				// Vender DNE
				if (ven_id == INVALID_VENDOR_ID) 
					continue;

				// Ignore non RealTek 8168 Devices
				if (ven_id != NE2K_VENDOR_ID || dev_id != NE2K_DEV_ID)
					continue;
				cprintf(" found on BUS %x DEV %x\n", i, j);

				// Find the IRQ
				ne2k_irq = pci_irq_map[i][j][k];
				ne2k_debug("-->IRQ: %u\n", ne2k_irq);

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
						ne2k_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
					} else {
						result = result & PCI_MEM_MASK;
						ne2k_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
					}
			
					// TODO Switch to memory mapped instead of IO?
					if (k == 0) // BAR0 denotes the IO Addr for the device
						ne2k_io_base_addr = result;						
				}
				
		return 0;
	}
	cprintf(" not found. No device configured.\n");
	
	return -1;
}

void ne2k_configure_nic() {
	
	ne2k_debug("-->Configuring Device.\n");
	
	// Reset. Yes reading from this addr resets it
	inb(ne2k_io_base_addr + NE2K_RESET_R_ADDR);

	// Configure
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR,  0x22);
	outb(ne2k_io_base_addr + NE2K_PG0_W_PSTRT,  NE2K_FIRST_RECV_PAGE);
        outb(ne2k_io_base_addr + NE2K_PG0_RW_BNRY, NE2K_FIRST_RECV_PAGE + 1);

        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, (0x22 & 0x3F) | 0x40);
	outb(ne2k_io_base_addr + NE2K_PG1_RW_CURR, NE2K_FIRST_RECV_PAGE + 2);
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, 0x22);
	outb(ne2k_io_base_addr + NE2K_PG0_W_PSTP, NE2K_LAST_RECV_PAGE);
	outb(ne2k_io_base_addr + NE2K_PG0_W_DCR, 0x94);	
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR,  0x22);
	
	outb(ne2k_io_base_addr + NE2K_PG0_W_RCR,  0xDF);
	outb(ne2k_io_base_addr + NE2K_PG0_W_TCR,  0xE0);
	

	//uint8_t isr = inb(ne2k_io_base_addr + 0x07);
	//cprintf("isr: %x\n", isr);

	
	return;
}

void ne2k_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	ne2k_debug("-->Setting interrupts.\n");
	
	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + ne2k_irq, ne2k_interrupt_handler, (void *)0);
	
	ioapic_route_irq(ne2k_irq, 6);	
	
	outb(ne2k_io_base_addr + NE2K_PG0_RW_ISR, 0xFF);
	outb(ne2k_io_base_addr + NE2K_PG0_W_IMR,  0xFF);
	
	return;
}

void ne2k_mem_alloc() {
	
	num_pages = NE2K_NUM_PAGES;
	base_page = kmalloc(num_pages * NE2K_PAGE_SIZE, 0);
	
}

void ne2k_read_mac() {

	uint8_t cr = inb(ne2k_io_base_addr + NE2K_PG0_RW_CR);
	
	// Set correct bits
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, 0xA);
	outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR0, 0x0);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR1, 0x0);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR0, 0x6);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR1, 0x0);


	for (int i = 0; i < 6; i++)
		device_mac[i] = inb(ne2k_io_base_addr + 0x10) & inb(ne2k_io_base_addr + 0x10);

	// Set page 1
        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, (cr & 0x3F) | 0x40);

	for (int i = 0; i < 6; i++) 
           outb(ne2k_io_base_addr + NE2K_PG1_RW_PAR + i, device_mac[i]);

	
	ne2k_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 0xFF & device_mac[0], 0xFF & device_mac[1],	
	                                                            0xFF & device_mac[2], 0xFF & device_mac[3],	
                                                                0xFF & device_mac[4], 0xFF & device_mac[5]);
	// Restore old setting.
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, cr);
	return;
}

void ne2k_test_interrupts() {
	
	cprintf("Generating Interrupt...\n");
	outb(ne2k_io_base_addr + 0x0A, 0x00);
	outb(ne2k_io_base_addr + 0x0B, 0x00);
	outb(ne2k_io_base_addr + 0x00, 0x0A);
	udelay(10000000);

	cprintf("Generating Interrupt again...\n");
	outb(ne2k_io_base_addr + 0x0A, 0x00);
	outb(ne2k_io_base_addr + 0x0B, 0x00);
	outb(ne2k_io_base_addr + 0x00, 0x0A);
	udelay(10000000);
	
}

// We need to evaluate this routine in terms of concurrency.
// We also need to figure out whats up with different core interrupts
void ne2k_interrupt_handler(trapframe_t *tf, void* data) {
	
	ne2k_interrupt_debug("\nNE2K interrupt on core %u!\n", lapic_get_id());
	uint8_t isr= inb(ne2k_io_base_addr + 0x07);
	ne2k_interrupt_debug("isr: %x\n", isr);
	
	if (isr & 0x1) {
		ne2k_interrupt_debug("-->Packet received.\n");
		ne2k_handle_rx_packet();
	}


	outb(ne2k_io_base_addr + 0x07, isr);
	//ne2k_handle_rx_packet();
	return;				
}

void ne2k_handle_rx_packet() {

        uint8_t bound = inb(ne2k_io_base_addr + NE2K_PG0_RW_BNRY);

        uint8_t cr = inb(ne2k_io_base_addr + NE2K_PG0_RW_CR);
        // Set page 1
        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, (cr & 0x3F) | 0x40);

	uint8_t next = inb(ne2k_io_base_addr + NE2K_PG1_RW_CURR);

	// Restore old setting.
        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, cr);

        uint8_t start = NE2K_FIRST_RECV_PAGE;
        uint8_t stop = NE2K_LAST_RECV_PAGE;

	// Broken mult packets?
	if (((bound + 1) == next) || (((bound + 1) == stop) && (start == next))) {
		return;
	}

	uint32_t kmalloc_size;

	if (MAX_FRAME_SIZE % NE2K_PAGE_SIZE) {
		kmalloc_size = ((MAX_FRAME_SIZE / NE2K_PAGE_SIZE) + 1) * NE2K_PAGE_SIZE;
	} else {
		kmalloc_size = MAX_FRAME_SIZE;
	}
	
	char *rx_buffer = kmalloc(kmalloc_size, 0);
	
	if (rx_buffer == NULL) panic ("Can't allocate page for incoming packet.");

        uint8_t curr = bound + 1;

//cprintf("reading from: %x\n", curr);

	uint8_t header[4];
	uint16_t packet_len = 0xFFFF;
	uint16_t page_count = 0;
	for (int i = 0; i < (MAX_FRAME_SIZE / NE2K_PAGE_SIZE); i++) {
		if (curr == stop)
			curr = start;			

		outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR0, 0x0);
		outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR1, curr);


		// Fix this. Its hard coded to 256
        	outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR0, 0);
        	outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR1, 0x1);
	

		outb(ne2k_io_base_addr + NE2K_PG0_RW_CR, 0x0A);

		for (int j = 0; j < NE2K_PAGE_SIZE; j++) {
			uint8_t val = inb(ne2k_io_base_addr + 0x10);
			if ((i == 0) && (j < 4)) {
//				cprintf("header %u %x\n", j, (uint8_t)val);
				header[j] = val;
			} 
			rx_buffer[i*NE2K_PAGE_SIZE + j] = val;
		}


		if (i == 0) {
			packet_len = ((uint16_t)header[3] << 8) | (uint16_t)header[2];
//		cprintf("header 2 %u header 3 %u header 3 shifted %u\n", (uint16_t)header[2], (uint16_t)header[3], (uint16_t)header[3] << 8);
//		cprintf("packet len: %u\n", packet_len);
			if (packet_len % NE2K_PAGE_SIZE) {
				page_count = (packet_len / NE2K_PAGE_SIZE) + 1;
			} else {
				page_count = (packet_len / NE2K_PAGE_SIZE);
			}
		}
		
		if ((i + 1) == page_count)
			break;

		curr++;

		
	}
//cprintf("last page: %x\n", curr);
	outb(ne2k_io_base_addr + NE2K_PG0_RW_BNRY, curr);
	

	// Hack for UDP syscall hack. 
	// This is a quick hack to let us deal with where to put packets coming in. This is not concurrency friendly
	// In the event that we get 2 incoming frames for our syscall test (shouldnt happen)
	// We cant process more until another packet comes in. This is ugly, but this code goes away as soon as we integrate a real stack.
	// This keys off the source port, fix it for dest port. 
	// Also this may access packet regions that are wrong. If someone addresses empty packet for our interface
	// and the bits that happened to be in memory before are the right port, this will trigger. this is bad
	// but since syscalls are a hack for only 1 machine connected, we dont care for now.
	
	if (*((uint16_t*)(rx_buffer + 40)) == 0x9bad) {
		

		extern int packet_waiting;	
		extern int packet_buffer_size;
		extern int packet_buffer_pos;
		extern char* packet_buffer;
		extern char* packet_buffer_orig;

		if (packet_waiting) return;
		
		// So ugly I want to cry
		packet_buffer_size = *((uint16_t*)(rx_buffer + 42)); 
		packet_buffer_size = (((uint16_t)packet_buffer_size & 0xff00) >> 8) |  (((uint16_t)packet_buffer_size & 0x00ff) << 8);		
		packet_buffer_size = packet_buffer_size - 8;	

		packet_buffer = rx_buffer + PACKET_HEADER_SIZE + 4;

		packet_buffer_orig = rx_buffer;
		packet_buffer_pos = 0;
		
		packet_waiting = 1;
						
		return;
	}
	
	// END HACKY STUFF
		
	kfree(rx_buffer);
	
	return;
}

// copied with love (and modification) from tcp/ip illistrated vl 2 1995 pg 236
// bsd licenced
uint16_t cksum(char *ip, int len) {
	
	uint32_t sum = 0;  /* assume 32 bit long, 16 bit short */

	while(len > 1){
             sum += *((uint16_t*) ip);
	     ip = ip + 2;
             if(sum & 0x80000000)   /* if high order bit set, fold */
               sum = (sum & 0xFFFF) + (sum >> 16);
             len -= 2;
           }

           if(len)       /* take care of left over byte */
             sum += (uint16_t) *(uint8_t *)ip;
          
           while(sum>>16)
             sum = (sum & 0xFFFF) + (sum >> 16);

           return ~sum;
         }


// Main routine to send a frame. May be completely broken.
int ne2k_send_frame(const char *data, size_t len) {


	if (data == NULL)
		return -1;
	if (len == 0)
		return 0;


	if (len > MAX_FRAME_SIZE) {
		ne2k_frame_debug("-->Frame Too Large!\n");
		return -1;
	}

        outb(ne2k_io_base_addr + NE2K_PG0_W_IMR,  0x00);


	// The TPSR takes a page #
	// The RSAR takes a byte offset, but since a page is 256 bits
	// and we are writing on page boundries, the low bits are 0, and
	// the high bits are a page #
	outb(ne2k_io_base_addr + NE2K_PG0_W_TPSR, NE2K_FIRST_SEND_PAGE);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR0, 0x0);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RSAR1, NE2K_FIRST_SEND_PAGE);

	
	outb(ne2k_io_base_addr + NE2K_PG0_W_TBCR0, len & 0xFF);
        outb(ne2k_io_base_addr + NE2K_PG0_W_TBCR1, len >> 8);

        outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR0, len & 0xFF);
        outb(ne2k_io_base_addr + NE2K_PG0_W_RBCR1, len >> 8);

	
	outb(ne2k_io_base_addr + NE2K_PG0_RW_CR,  0x12);
	

	for (int i = 0; i<len; i = i + 1) {
		outb(ne2k_io_base_addr + 0x10, *(uint8_t*)(data + i));
		//printk("sent: %x\n", *(uint8_t*)(data + i));
	}
	
	while(( inb(ne2k_io_base_addr + 0x07) & 0x40) == 0);

        outb(ne2k_io_base_addr + 0x07,  0x40);

        outb(ne2k_io_base_addr + NE2K_PG0_W_IMR,  0xFF);

        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR,  0x1E);
	
	
	return len;
}

// This function is a complete hack for syscalls until we get a stack.
// the day I delete this monstrosity of code I will be a happy man --Paul
char *ne2k_packet_wrap(const char* data, size_t len) {
 	
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
 		ne2k_frame_debug("Bad packet size for packet wrapping");
 		return NULL;
 	}
 	
 	struct eth_packet* wrap_buffer = kmalloc(MAX_PACKET_SIZE, 0);
 	
 	if (wrap_buffer == NULL) {
 		ne2k_frame_debug("Can't allocate page for packet wrapping");
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
 	//ip_header->ip_opts2 = 0x4f2f110a;
        ip_header->ip_opts2 = 0x0000110a;

	ip_header->source_ip = htonl(source_ip);
 	ip_header->dest_ip = htonl(dest_ip);
 	

	ip_header->ip_opts2 = 	ip_header->ip_opts2 | 
				((uint32_t)cksum((char*)ip_header, sizeof(struct IP_Header)) << 16);
 	// Setup UDP Data
 	udp_header->source_port = htons(44443);
 	udp_header->dest_port = htons(44444);
 	udp_header->length = htons(8 + len);
 	udp_header->checksum = 0;
 	
 	memcpy (&wrap_buffer->data[0], data, len);
 	
 	return (char *CT(PACKET_HEADER_SIZE + len))wrap_buffer;	
}
