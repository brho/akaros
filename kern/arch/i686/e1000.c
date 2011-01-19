/** @file
 * @brief E1000 Driver       
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
#include <arch/e1000.h>

#include <ros/memlayout.h>

#include <atomic.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <kmalloc.h>

#include <pmap.h>
#include <frontend.h>
#include <arch/frontend.h>

#include <eth_audio.h>

#define NUM_TX_DESCRIPTORS E1000_NUM_TX_DESCRIPTORS
#define NUM_RX_DESCRIPTORS E1000_NUM_RX_DESCRIPTORS

/** @file
 * @brief Intel E1000 Driver
 *
 * EXPERIMENTAL. DO NOT USE IF YOU DONT KNOW WHAT YOU ARE DOING
 *
 * To enable use, define __NETWORK__ in your Makelocal
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Move documention below into doxygen format.
 * @todo See list in code
 */

// TODO REWRITE THIS 
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
 */

uint32_t e1000_mmio_base_addr = 0;
uint32_t e1000_io_base_addr = 0;
uint32_t e1000_irq = 0;
uint32_t e1000_addr_size = 0;
unsigned char device_mac[6];

struct e1000_rx_desc *rx_des_kva;
unsigned long rx_des_pa;

struct e1000_tx_desc *tx_des_kva;
unsigned long tx_des_pa;

uint32_t e1000_rx_index = 0;
uint32_t e1000_tx_index = 0;

extern uint8_t eth_up;
extern uint32_t packet_buffer_count;
extern char* packet_buffers[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_sizes[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_head;
extern uint32_t packet_buffers_tail;
spinlock_t packet_buffers_lock;

uint16_t device_id;

extern int (*send_frame)(const char *CT(len) data, size_t len);

void e1000_dump_rx() {

	for (int i = 0; i < 10; i++) {
		
		printk("%u:  %lx%lx\n", i, *(uint64_t*)(&rx_des_kva[i]), *((uint64_t*)(&rx_des_kva[i]) + 1));
		printk("%ud: %lx\n", i, *(uint64_t*)(KADDR(rx_des_kva[i].buffer_addr)));	
	}

}

void e1000_dump_stats() {

	uint32_t offset = 0x04000;
	
	while (offset <= 0x040FC) {
		if ((offset % 16) == 0)
			printk("\n");
                printk("%x:%d ", offset,e1000_rr32(offset));

		offset = offset + 4;
	}
}

void e1000_init() {

	if (e1000_scan_pci() < 0) return;

	e1000_setup_descriptors();
	e1000_configure();
	printk("Network Card MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	   device_mac[0],device_mac[1],device_mac[2],
	   device_mac[3],device_mac[4],device_mac[5]);

	//e1000_dump_rx();
	e1000_setup_interrupts();

	send_frame = &e1000_send_frame;

	eth_up = 1;
	
	return;
}

int e1000_scan_pci() {
	struct pci_device *pcidev;
	uint32_t result;
	unsigned int once = 0;
	printk("Searching for Intel E1000 Network device...");
	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* Ignore non Intel Devices */
		if (pcidev->ven_id != INTEL_VENDOR_ID)
			continue;
		/* Ignore non E1000 devices */
		if ((pcidev->dev_id != INTEL_DEV_ID0) && 
		    (pcidev->dev_id != INTEL_DEV_ID1) &&
		    (pcidev->dev_id != INTEL_DEV_ID2))
			continue;
		printk(" found on BUS %x DEV %x FUNC %x\n", pcidev->bus, pcidev->dev,
		       pcidev->func);
		/* TODO: (ghetto) Skip the management nic on the 16 core box.  It is
		 * probably the first one found (check this) */
		if ((pcidev->dev_id == INTEL_DEV_ID1) && (once++ == 0)) 
			continue;
		/* TODO: WARNING - EXTREMELY GHETTO */
		device_id = pcidev->dev_id;
		/* Find the IRQ */
		e1000_irq = pcidev->irqline;
		e1000_debug("-->IRQ: %u\n", e1000_irq);
		/* Loop over the BARs */
		for (int k = 0; k <= 5; k++) {
			int reg = 4 + k;
	        result = pcidev_read32(pcidev, reg << 2);	// SHAME!
			if (result == 0) // (0 denotes no valid data)
				continue;
			// Read the bottom bit of the BAR. 
			if (result & PCI_BAR_IO_MASK) {
				result = result & PCI_IO_MASK;
				e1000_debug("-->BAR%u: %s --> %x\n", k, "IO", result);
			} else {
				result = result & PCI_MEM_MASK;
				e1000_debug("-->BAR%u: %s --> %x\n", k, "MEM", result);
			}
			if (k == 0) { // BAR0 denotes the IO Addr for the device
				if (result & PCI_BAR_IO_MASK) {
					e1000_debug("-->IO PORT MODE\n");
					panic("IO PORT MODE NOT SUPPORTED\n");
				} else {
					e1000_debug("-->MMIO Mode, Base: %08p\n", result);
					e1000_mmio_base_addr = result;
					// Now we do magic to find the size
					// The first non zero bit after we
					// write all 1's denotes the size
					outl(PCI_CONFIG_DATA, 0xFFFFFFFF);
					result = inl(PCI_CONFIG_DATA);
					result = result & PCI_MEM_MASK;
					result = (result ^ 0xFFFFFFFF) + 1;
					e1000_addr_size = result;
                    e1000_debug("-->MMIO Size %x\n", e1000_addr_size);
					outl(PCI_CONFIG_DATA, e1000_mmio_base_addr);
		
					#ifdef __CONFIG_E1000_MMIO_HACK__
					/* TODO: WARNING - EXTREMELY GHETTO */
					// Map the page in.
					e1000_debug("HACK FOR BROKEN MMIO\n");
					e1000_mmio_base_addr = E1000_MMIO_ADDR;
					outl(PCI_CONFIG_DATA, e1000_mmio_base_addr);
					e1000_mmio_base_addr = 0xfee00000 + 0x1000;
					#endif
				}
			}						
		}
		return 0;
	}
	printk(" not found. No device configured.\n");
	return -1;
}

uint32_t e1000_rr32(uint32_t offset) {

	if (e1000_mmio_base_addr) {
		return read_mmreg32(e1000_mmio_base_addr + offset);
	} else {
		return inl(e1000_io_base_addr + offset);
	}
}

void e1000_wr32(uint32_t offset, uint32_t val) {

	if (e1000_mmio_base_addr) {
		write_mmreg32(e1000_mmio_base_addr + offset, val);
	} else {
		outl(e1000_io_base_addr + offset, val);
	}
}


uint16_t e1000_read_eeprom(uint32_t offset) {

	uint16_t eeprom_data;
	uint32_t eeprom_reg_val = e1000_rr32(E1000_EECD);

	eeprom_reg_val = eeprom_reg_val | E1000_EECD_REQ;
	e1000_wr32(E1000_EECD, eeprom_reg_val);
        while((e1000_rr32(E1000_EECD) & E1000_EECD_GNT) == 0);

	eeprom_reg_val = E1000_EERD_START | (offset << E1000_EERD_ADDR_SHIFT);
	e1000_wr32(E1000_EERD, eeprom_reg_val);
	while(((eeprom_reg_val = e1000_rr32(E1000_EERD)) & E1000_EERD_DONE) == 0);
	eeprom_data = (eeprom_reg_val & E1000_EERD_DATA_MASK) >> E1000_EERD_DATA_SHIFT;
	
	eeprom_reg_val = e1000_rr32(E1000_EECD);
	e1000_wr32(E1000_EECD, eeprom_reg_val & ~E1000_EECD_REQ);

	return eeprom_data;

}

void e1000_setup_mac() {

	uint16_t eeprom_data = 0;
        uint32_t mmio_data = 0;

	/* TODO: WARNING - EXTREMELY GHETTO */
	if (device_id == INTEL_DEV_ID0) {

		for (int i = 0; i < 3; i++) {

			eeprom_data = e1000_read_eeprom(i);
			device_mac[2*i] = eeprom_data & 0x00FF;
			device_mac[2*i + 1] = (eeprom_data & 0xFF00) >> 8;

		}

	} else {

	        mmio_data = e1000_rr32(E1000_RAL);
	        device_mac[0] = mmio_data & 0xFF;
       		device_mac[1] = (mmio_data >> 8) & 0xFF;
        	device_mac[2] = (mmio_data >> 16) & 0xFF;
        	device_mac[3] = (mmio_data >> 24) & 0xFF;
        	mmio_data = e1000_rr32(E1000_RAH);
        	device_mac[4] = mmio_data & 0xFF;
        	device_mac[5] = (mmio_data >> 8) & 0xFF;
	}

	// Check if we need to invert the higher order bits (some E1000's)
	mmio_data = e1000_rr32(E1000_STATUS);

	if (mmio_data & E1000_STATUS_FUNC_MASK) {
		printk("UNTESTED LANB FUNCTIONALITY! MAY BE BREAKING MAC\n");
		device_mac[5] ^= 0x0100;
	}	

	// Program the device to use this mac
	e1000_wr32(E1000_RAH, 0x00000); // Set MAC invalid
	e1000_wr32(E1000_RAL, *(uint32_t*)device_mac);
	e1000_wr32(E1000_RAH, *(uint16_t*)(device_mac + 4) | 0x80000000);
	
	// Now make sure it read back out.
	mmio_data = e1000_rr32(E1000_RAL);
	device_mac[0] = mmio_data & 0xFF;
	device_mac[1] = (mmio_data >> 8) & 0xFF;
	device_mac[2] = (mmio_data >> 16) & 0xFF;
	device_mac[3] = (mmio_data >> 24) & 0xFF;
	mmio_data = e1000_rr32(E1000_RAH);
	device_mac[4] = mmio_data & 0xFF;
	device_mac[5] = (mmio_data >> 8) & 0xFF;

	// Clear the MAC's from all the other filters
	// Must clear high to low.
	// TODO: Get the right number of filters
	for (int i = 1; i < 16; i++) {
		e1000_wr32(E1000_RAH + 8 * i, 0x0);
		e1000_wr32(E1000_RAL + 8 * i, 0x0);
	}


	// Clear MTA Table
	// TODO: Get the right number of filters
	for (int i = 0; i < 0x7F; i++) {
		e1000_wr32(E1000_MTA + 4 * i, 0x0);
	}

	e1000_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	            0xFF & device_mac[0], 0xFF & device_mac[1],
	            0xFF & device_mac[2], 0xFF & device_mac[3],
	            0xFF & device_mac[4], 0xFF & device_mac[5]);
	return;
}

void e1000_setup_descriptors() {

	e1000_debug("-->Setting up tx/rx descriptors.\n");

	// Allocate room for the buffers. 
	// Must be 16 byte aligned
        uint32_t num_rx_pages = ROUNDUP(NUM_RX_DESCRIPTORS * sizeof(struct e1000_rx_desc), PGSIZE) / PGSIZE;
        uint32_t num_tx_pages = ROUNDUP(NUM_TX_DESCRIPTORS * sizeof(struct e1000_tx_desc), PGSIZE) / PGSIZE;
	
	rx_des_kva = get_cont_pages(LOG2_UP(num_rx_pages), 0);
	tx_des_kva = get_cont_pages(LOG2_UP(num_tx_pages), 0);

	if (rx_des_kva == NULL) panic("Can't allocate page for RX Ring");
	if (tx_des_kva == NULL) panic("Can't allocate page for TX Ring");
	
	rx_des_pa = PADDR(rx_des_kva);
	tx_des_pa = PADDR(tx_des_kva);
	
	for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
		e1000_set_rx_descriptor(i, TRUE); // Allocate memory for the descriptor
		
	for (int i = 0; i < NUM_TX_DESCRIPTORS; i++) 
		e1000_set_tx_descriptor(i);
	return;
}


void e1000_set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer) {
	
	//memset(&rx_des_kva[des_num], 0x00, sizeof(struct e1000_rx_desc));
	rx_des_kva[des_num].length = 0;
	rx_des_kva[des_num].csum = 0;
	rx_des_kva[des_num].status = 0;
	rx_des_kva[des_num].errors = 0;
	rx_des_kva[des_num].special = 0;
	
	if (reset_buffer) {
		char *rx_buffer = kmalloc(E1000_RX_MAX_BUFFER_SIZE, 0);
		if (rx_buffer == NULL) panic ("Can't allocate page for RX Buffer");

		rx_des_kva[des_num].buffer_addr = PADDR(rx_buffer);
	}

	return;
}

void e1000_set_tx_descriptor(uint32_t des_num) {
	
	// Clear the bits.
	memset(&tx_des_kva[des_num], 0x00, sizeof(struct e1000_tx_desc));
	
	char *tx_buffer = kmalloc(E1000_TX_MAX_BUFFER_SIZE, 0);

	if (tx_buffer == NULL) panic ("Can't allocate page for TX Buffer");

	tx_des_kva[des_num].buffer_addr = PADDR(tx_buffer);
	return;
}

// This startup sequence is taken with love from BarrelFish.
// <3 the multikernel.
void e1000_configure() {
	
	uint32_t data;

	e1000_debug("-->Configuring Device.\n");
	
	// Clear interrupts
	e1000_wr32(E1000_IMC, E1000_IMC_ALL);

	// Disable receiver and transmitter
	e1000_wr32(E1000_RCTL, 0x00);
	e1000_wr32(E1000_TCTL, 0x00);

	// Reset
	e1000_reset();	

	// Clear interrupts
	e1000_wr32(E1000_IMC, E1000_IMC_ALL);

	// Fix PHY_RESET
	data = e1000_rr32(E1000_CTRL);
	data = data & ~E1000_CTRL_PHY_RST;
	e1000_wr32(E1000_CTRL, data);
	data = e1000_rr32(E1000_STATUS);
	data = data & ~E1000_STATUS_MTXCKOK; // XXX: Against spec
	e1000_wr32(E1000_STATUS, data);

	// Link MAC and PHY
	data = e1000_rr32(E1000_CTRL);
	data = data & E1000_CTRL_SLU;
	e1000_wr32(E1000_CTRL, data);

	// Set PHY mode
	data = e1000_rr32(E1000_CTRL_EXT);
	data = (data & ~E1000_CTRL_EXT_LINK_MODE_MASK) | E1000_CTRL_EXT_LINK_MODE_GMII;
	e1000_wr32(E1000_CTRL_EXT, data);

	// Set full-duplex
	data = e1000_rr32(E1000_CTRL);
	data = data & E1000_CTRL_FD;
	e1000_wr32(E1000_CTRL, data);

	// Set CTRL speed (from STATUS speed)
	{
		data = e1000_rr32(E1000_CTRL);
		uint32_t status = e1000_rr32(E1000_STATUS);
		status = (status & E1000_STATUS_SPEED_MASK) >> 6;
		data = (data & ~E1000_CTRL_SPD_SEL) | (status << 8);
		e1000_wr32(E1000_CTRL, data);
	}

	// Turn off flow control
	e1000_wr32(E1000_FCAL, 0x00);
	e1000_wr32(E1000_FCAH, 0x00);
	e1000_wr32(E1000_FCT,  0x00);

	// Set stat counters

	// Setup MAC address
	e1000_setup_mac();	

        // Set RX Ring
        e1000_wr32(E1000_RDBAL, rx_des_pa);
        e1000_wr32(E1000_RDBAH, 0x00);

        // Set RX Ring Size
        // This is the number of desc's, divided by 8. It starts
        // at bit 7.
        e1000_wr32(E1000_RDLEN, NUM_RX_DESCRIPTORS * 16);
        
	e1000_wr32(0x0280C, 0x00);

	// Set head and tail pointers.
        e1000_wr32(E1000_RDH, 0x00);
        e1000_wr32(E1000_RDT, 0x00);

        // Receive descriptor control
	e1000_wr32(E1000_RXDCTL, 0x02000000 | 0x01010000);

        data = e1000_rr32(E1000_RFCTL);
        data = data & ~E1000_RFCTL_EXTEN;
        e1000_wr32(E1000_RFCTL, data);

	// Enable packet reception
	data = e1000_rr32(E1000_RCTL);
	data = data | E1000_RCTL_EN | E1000_RCTL_BAM;
	e1000_wr32(E1000_RCTL, data);

        e1000_wr32(E1000_RDT, NUM_RX_DESCRIPTORS - 1);

	// Set TX Ring
	e1000_wr32(E1000_TDBAL, tx_des_pa);
	e1000_wr32(E1000_TDBAH, 0x00);

	// Set TX Des Size
	e1000_wr32(E1000_TDLEN, ((NUM_TX_DESCRIPTORS / 8) << 7));

        // Transmit inter packet gap register
        // XXX: Recomended magic. See 13.4.34
        e1000_wr32(E1000_TIPG, 0x00702008);

	// Set head and tail pointers.
	e1000_wr32(E1000_TDH, 0x00);
	e1000_wr32(E1000_TDT, 0x00);

        // Tansmit desc control
        e1000_wr32(E1000_TXDCTL, 0x01000000 | 0x02000000);

        e1000_wr32(E1000_TDT, NUM_TX_DESCRIPTORS - 1);

	// Enable transmit
	// XXX: MAGIC (not really paul just hasn't enumerated yet)
	e1000_wr32(E1000_TCTL, 0x0F01A);

	return;
}

void e1000_reset() {
	e1000_debug("-->Resetting device..... ");

	uint32_t ctrl = e1000_rr32(E1000_CTRL);

	ctrl = ctrl & E1000_CTRL_RST;

	e1000_wr32(E1000_CTRL, ctrl);

	e1000_debug(" done.\n");

	return;
}

void enable_e1000_irq(struct trapframe *tf, uint32_t src_id, 
                                void* a0, void* a1, void* a2)
{
	pic_unmask_irq(e1000_irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	enable_irq();
}

void e1000_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	e1000_debug("-->Setting interrupts.\n");
	
	// Set throttle register
	e1000_wr32(E1000_ITR, 0x0000);
	
	// Clear interrupts
	e1000_wr32(E1000_IMS, 0xFFFFFFFF);
	e1000_wr32(E1000_IMC, 0xFFFFFFFF);
	
	// Set interrupts
	// TODO: Make this only enable stuff we want
	e1000_wr32(E1000_IMS, 0xFFFFFFFF); 

	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + e1000_irq, e1000_interrupt_handler, 0);

	// Enable irqs for the e1000
#ifdef __CONFIG_ENABLE_MPTABLES__
	/* TODO: this should be for any IOAPIC EOI, not just MPTABLES */
	ioapic_route_irq(e1000_irq, E1000_IRQ_CPU);	
#else 
	// This will route the interrupts automatically to CORE 0
	// Call send_kernel_message if you want to route them somewhere else
	enable_e1000_irq(NULL,0,0,0,0);
#endif

	return;
}

void e1000_interrupt_handler(trapframe_t *tf, void* data) {

//	printk("About to spam to mac addr: 00:14:4F:D1:EC:6C\n");
//	while(1) {
//		appserver_packet_t p;
//		p.header.dst_mac[0] = 0x00;
//		p.header.dst_mac[1] = 0x14;
//		p.header.dst_mac[2] = 0x4f;
//		p.header.dst_mac[3] = 0xd1;
//		p.header.dst_mac[4] = 0xec;
//		p.header.dst_mac[5] = 0x6c;
//		p.header.src_mac[0] = 0x00;
//		p.header.src_mac[1] = 0x23;
//		p.header.src_mac[2] = 0x8b;
//		p.header.src_mac[3] = 0x42;
//		p.header.src_mac[4] = 0x80;
//		p.header.src_mac[5] = 0xb8;
//		p.header.ethertype = 0x8888;
//		send_frame((char*)&p,0);
//	}

	e1000_interrupt_debug("\nNic interrupt on core %u!\n", lapic_get_id());
				
	// Read the offending interrupt(s)
	// Note: Reading clears the interrupts
	uint32_t interrupt_status = e1000_rr32(E1000_ICR);

	// Loop to deal with TOCTOU 
	while (interrupt_status != 0x0000) {

		//printk("Interrupt status: %x\n", interrupt_status);

		if ((interrupt_status & E1000_ICR_INT_ASSERTED) && (interrupt_status & E1000_ICR_RXT0)) {
			e1000_debug("---->Packet Received\n");
			e1000_handle_rx_packet();
		}	
		// Clear interrupts	
		interrupt_status = e1000_rr32(E1000_ICR);
	}
	
	// In the event that we got really unlucky and more data arrived after we set 
	//  set the bit last, try one more check
	e1000_handle_rx_packet();

	return;
}

void e1000_handle_rx_packet() {
	
	uint16_t packet_size;
	uint32_t status;
	uint32_t head = e1000_rr32(E1000_RDH);

	//printk("Current head is: %x\n", e1000_rr32(E1000_RDH));
	//printk("Current tail is: %x\n", e1000_rr32(E1000_RDT));
	
	if (head == e1000_rx_index) {
		e1000_frame_debug("-->Nothing to process. Returning.");
		return;
	}
	
	uint32_t rx_des_loop_cur = e1000_rx_index;
	uint32_t frame_size = 0;
	uint32_t fragment_size = 0;
	uint32_t num_frags = 0;
	
	char *rx_buffer = kmalloc(MAX_FRAME_SIZE, 0);

	if (rx_buffer == NULL) panic ("Can't allocate page for incoming packet.");
	
	do {
		status =  rx_des_kva[rx_des_loop_cur].status;

		if (status == 0x0) {
			warn("ERROR: E1000: Packet owned by hardware has 0 status value\n");
			/* It's possible we are processing a packet that is a fragment
			 * before the entire packet arrives.  The code currently assumes
			 * that all of the packets fragments are there, so it assumes the
			 * next one is ready.  We'll spin until it shows up...  This could
			 * deadlock, and sucks in general, but will help us diagnose the
			 * driver's issues.  TODO: determine root cause and fix this shit.*/
			while(rx_des_kva[rx_des_loop_cur].status == 0x0)
				cpu_relax();
			status = rx_des_kva[rx_des_loop_cur].status;
		}
	
		fragment_size = rx_des_kva[rx_des_loop_cur].length;
		
		// If we've looped through the entire ring and not found a terminating packet, bad nic state.
		// Panic or clear all descriptors? This is a nic hardware error. 
		if (num_frags && (rx_des_loop_cur == head)) {
			e1000_frame_debug("-->ERR: No ending segment found in RX buffer.\n");
			panic("RX Descriptor Ring out of sync.");
		}
		
		num_frags++;
		
		// Make sure we own the current packet. Kernel ownership is denoted by a 0. Nic by a 1.
		if ((status & E1000_RXD_STAT_DD) == 0x0) {
			e1000_frame_debug("-->ERR: Current RX descriptor not owned by software. Panic!");
			panic("RX Descriptor Ring OWN out of sync");
		}
		
		// Deal with packets too large
		if ((frame_size + fragment_size) > MAX_FRAME_SIZE) {
			e1000_frame_debug("-->ERR: Nic sent %u byte packet. Max is %u\n", frame_size, MAX_FRAME_SIZE);
			panic("NIC Sent packets larger than configured.");
		}
		
		// Move the fragment data into the buffer
		memcpy(rx_buffer + frame_size, KADDR(rx_des_kva[rx_des_loop_cur].buffer_addr), fragment_size);
		
		// Reset the descriptor. No reuse buffer.
		e1000_set_rx_descriptor(rx_des_loop_cur, FALSE);
		
		// Note: We mask out fragment sizes at 0x3FFFF. There can be at most 1024 of them.
		// This can not overflow the uint32_t we allocated for frame size, so
		// we dont need to worry about mallocing too little then overflowing when we read.
		frame_size = frame_size + fragment_size;
		
		// Advance to the next descriptor
		rx_des_loop_cur = (rx_des_loop_cur + 1) % NUM_RX_DESCRIPTORS;

	} while ((status & E1000_RXD_STAT_EOP) == 0);

#ifdef __CONFIG_APPSERVER__
	// Treat as a syscall frontend response packet if eth_type says so
	// Will eventually go away, so not too worried about elegance here...
	uint16_t eth_type = htons(*(uint16_t*)(rx_buffer + 12));
	if(eth_type == APPSERVER_ETH_TYPE) {
		handle_appserver_packet(rx_buffer, frame_size);
		kfree(rx_buffer);

		// Advance the tail pointer				
		e1000_rx_index = rx_des_loop_cur;
		e1000_wr32(E1000_RDT, e1000_rx_index);
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
		eth_audio_newpacket(packet);
		// Advance the tail pointer				
		e1000_rx_index = rx_des_loop_cur;
		e1000_wr32(E1000_RDT, e1000_rx_index);
		return;
	}
#endif /* __CONFIG_ETH_AUDIO__ */

	spin_lock(&packet_buffers_lock);

	if (num_packet_buffers >= MAX_PACKET_BUFFERS) {
		printd("WARNING: DROPPING PACKET!\n");
		spin_unlock(&packet_buffers_lock);
		kfree(rx_buffer);
	
		// Advance the tail pointer				
		e1000_rx_index = rx_des_loop_cur;
		e1000_wr32(E1000_RDT, e1000_rx_index);
		return;
	}

	packet_buffers[packet_buffers_tail] = rx_buffer;
	packet_buffers_sizes[packet_buffers_tail] = frame_size;
		
	packet_buffers_tail = (packet_buffers_tail + 1) % MAX_PACKET_BUFFERS;
	num_packet_buffers++;

	spin_unlock(&packet_buffers_lock);

	// Advance the tail pointer				
	e1000_rx_index = rx_des_loop_cur;
	e1000_wr32(E1000_RDT, e1000_rx_index);
				
	// Chew on the frame data. Command bits should be the same for all frags.
	//e1000_process_frame(rx_buffer, frame_size, current_command);
	
	return;
}

// Main routine to send a frame. Just sends it and goes.
// Card supports sending across multiple fragments.
// Would we want to write a function that takes a larger packet and generates fragments?
// This seems like the stacks responsibility. Leave this for now. may in future
// Remove the max size cap and generate multiple packets.
int e1000_send_frame(const char *data, size_t len) {

	if (data == NULL)
		return -1;
	if (len == 0)
		return 0;

	uint32_t head = e1000_rr32(E1000_TDH);

	if (((e1000_tx_index + 1) % NUM_TX_DESCRIPTORS) == head) {
		e1000_frame_debug("-->TX Ring Buffer Full!\n");
		return -1;
	}
	
	if (len > MAX_FRAME_SIZE) {
		e1000_frame_debug("-->Frame Too Large!\n");
		return -1;
	}
	
	memcpy(KADDR(tx_des_kva[e1000_tx_index].buffer_addr), data, len);

	tx_des_kva[e1000_tx_index].lower.flags.length = len;
	tx_des_kva[e1000_tx_index].lower.flags.cmd = 0x0B;

	e1000_tx_index = (e1000_tx_index + 1) % NUM_TX_DESCRIPTORS;
	e1000_wr32(E1000_TDT, e1000_tx_index);

	e1000_frame_debug("-->Sent packet.\n");
	
return len;
}
