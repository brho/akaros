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


#define SET_PAGE_0() (inb(ne2k_io_base_addr + NE2K_PG0_RW_CR) & 0x3F)

uint32_t ne2k_irq;      // Fix this
uint32_t ne2k_io_base_addr;

void* base_page;
uint32_t num_pages = 0;

void ne2k_init() {
	
	if (ne2k_scan_pci() < 0) return;
	ne2k_mem_alloc();
	ne2k_configure_nic();
	ne2k_read_mac();
	printk("Network Card MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	   device_mac[0],device_mac[1],device_mac[2],
	   device_mac[3],device_mac[4],device_mac[5]);
	//ne2k_test_interrupts();
	send_frame = &ne2k_send_frame;

	ne2k_setup_interrupts();

	eth_up = 1;

	return;
}


int ne2k_scan_pci() {
	struct pci_device *pcidev;
	uint32_t result;
	printk("Searching for NE2000 Network device...");
	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* Ignore non NE2K Devices */
		if ((pcidev->ven_id != NE2K_VENDOR_ID) ||
		   (pcidev->dev_id != NE2K_DEV_ID))
			continue;
		printk(" found on BUS %x DEV %x\n", pcidev->bus, pcidev->dev);
		/* Find the IRQ */
		ne2k_irq = pcidev->irqline;
		ne2k_debug("-->IRQ: %u\n", ne2k_irq);
		/* Loop over the BARs */
		for (int k = 0; k <= 5; k++) {
			int reg = 4 + k;
	        result = pcidev_read32(pcidev, reg << 2);	// SHAME!
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
	printk(" not found. No device configured.\n");
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
	

	//uint8_t isr = inb(ne2k_io_base_addr + NE2K_PG0_RW_ISR);
	//cprintf("isr: %x\n", isr);

	
	return;
}

void ne2k_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	ne2k_debug("-->Setting interrupts.\n");
	
	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + ne2k_irq, ne2k_interrupt_handler, (void *)0);
	
#ifdef __CONFIG_ENABLE_MPTABLES__
	ioapic_route_irq(ne2k_irq, 0);	
#else
	pic_unmask_irq(ne2k_irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	enable_irq();
#endif
	
	SET_PAGE_0();

        outb(ne2k_io_base_addr + NE2K_PG0_W_IMR,  0xBF);

	outb(ne2k_io_base_addr + NE2K_PG0_RW_ISR, 0xFF);
	return;
}

void ne2k_mem_alloc() {
	
	num_pages = ROUNDUP(NE2K_NUM_PAGES * NE2K_PAGE_SIZE, PGSIZE) / PGSIZE;
	base_page = get_cont_pages(LOG2_UP(num_pages), 0);	
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

	SET_PAGE_0();

	uint8_t isr= inb(ne2k_io_base_addr + NE2K_PG0_RW_ISR);
	ne2k_interrupt_debug("isr: %x\n", isr);
	
	while (isr != 0x0) {

		// TODO: Other interrupt cases.

		if (isr & 0x1) {
			ne2k_interrupt_debug("-->Packet received.\n");
			ne2k_handle_rx_packet();
		}
		
		SET_PAGE_0();

		// Clear interrupts
		isr = inb(ne2k_io_base_addr + NE2K_PG0_RW_ISR);
		outb(ne2k_io_base_addr + NE2K_PG0_RW_ISR, isr);

	}

	ne2k_handle_rx_packet();
	
	return;				
}

// @TODO: Is this broken? Didn't change it after kmalloc changed
void ne2k_handle_rx_packet() {
	
	SET_PAGE_0();

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
		ne2k_debug("NO PACKET TO PROCESS\n");
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

	uint8_t header[4];
	uint16_t packet_len = 0xFFFF;
	uint16_t page_count = 0;
	for (int i = 0, n = 0; i < (MAX_FRAME_SIZE / NE2K_PAGE_SIZE); i++) {
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
				header[j] = val;
			} else { 
				rx_buffer[n++] = val;
			}
		}

		if (i == 0) {
			packet_len = ((uint16_t)header[3] << 8) | (uint16_t)header[2];
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
	outb(ne2k_io_base_addr + NE2K_PG0_RW_BNRY, curr);

	if (packet_len == 0) {
		ne2k_debug("Triggered on an empty packet.\n");
		return;
	}

#ifdef __CONFIG_APPSERVER__
	// Treat as a syscall frontend response packet if eth_type says so
	// Will eventually go away, so not too worried about elegance here...
	#include <frontend.h>
	#include <arch/frontend.h>
	uint16_t eth_type = htons(*(uint16_t*)(rx_buffer + 12));
	if(eth_type == APPSERVER_ETH_TYPE) {
		handle_appserver_packet(rx_buffer, packet_len);
		kfree(rx_buffer);
		return;
	}
#endif

	spin_lock(&packet_buffers_lock);

	if (num_packet_buffers >= MAX_PACKET_BUFFERS) {
		printk("WARNING: DROPPING PACKET!\n");
		spin_unlock(&packet_buffers_lock);
		kfree(rx_buffer);
		return;
	}

	packet_buffers[packet_buffers_tail] = rx_buffer;
	packet_buffers_sizes[packet_buffers_tail] = packet_len;

	packet_buffers_tail = (packet_buffers_tail + 1) % MAX_PACKET_BUFFERS;
	num_packet_buffers++;

	spin_unlock(&packet_buffers_lock);
	
	return;
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
		//ne2k_debug("sent: %x\n", *(uint8_t*)(data + i));
	}
	
	while(( inb(ne2k_io_base_addr + NE2K_PG0_RW_ISR) & 0x40) == 0);

        outb(ne2k_io_base_addr + NE2K_PG0_RW_ISR,  0x40);

        outb(ne2k_io_base_addr + NE2K_PG0_W_IMR,  0xBF);

        outb(ne2k_io_base_addr + NE2K_PG0_RW_CR,  0x1E);
	
	
	return len;
}

