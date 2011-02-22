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


// Global variables for the device
uint32_t e1000_mmio_base_addr = 0;
uint32_t e1000_io_base_addr = 0;
uint32_t e1000_irq = 0;
uint32_t e1000_addr_size = 0;

// The device's MAC address (read from the device)
unsigned char device_mac[6];

// Vars relating to the receive descriptor ring
struct e1000_rx_desc *rx_des_kva;
unsigned long rx_des_pa;
uint32_t e1000_rx_index = 0;


// Vars relating to the transmit descriptor ring
struct e1000_tx_desc *tx_des_kva;
unsigned long tx_des_pa;
uint32_t e1000_tx_index = 0;

extern uint8_t eth_up;

// The PCI device ID we detect
// This is used for quark behavior
uint16_t device_id;


// Hacky variables relating to delivering packets
extern uint32_t packet_buffer_count;
extern char* packet_buffers[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_sizes[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_head;
extern uint32_t packet_buffers_tail;
spinlock_t packet_buffers_lock;

// Allow us to register our send_frame as the global send_frameuint16_t device_id;
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

// Main init sequence. This is whats called to configure the device
// This includes detection of the device.
void e1000_init() {

	// Detect if the device is present
	if (e1000_scan_pci() < 0) return;

	// Allocate and program the descriptors for the ring
	// Note: Does not tell the device to use them, yet.
	e1000_setup_descriptors();
	e1000_configure();
	printk("Network Card MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n", 
	   device_mac[0],device_mac[1],device_mac[2],
	   device_mac[3],device_mac[4],device_mac[5]);

	e1000_setup_interrupts();

	// "Register" our send_frame with the global system
	send_frame = &e1000_send_frame;

	// sudo /sbin/ifconfig eth0 up
	eth_up = 1;
	
	return;
}

/* Given a addr read from bar0, determine the IO mode,
 * determine the addr range, and map the MMIO region in.
 *
 * Note: This must be called from a scan_pci() context, as it
 * relies on the state of the PCI_CONFIG_ADDR register.
 */
void e1000_handle_bar0(uint32_t addr) {

	if (addr & PCI_BAR_IO_MASK) {
		e1000_debug("-->IO PORT MODE\n");
		panic("IO PORT MODE NOT SUPPORTED\n");
	} else {
		e1000_debug("-->MMIO Mode\n");
		
		// Now we do magic to find the size
		// The first non zero bit after we
                // write all 1's denotes the size
		outl(PCI_CONFIG_DATA, 0xFFFFFFFF);
		uint32_t result = inl(PCI_CONFIG_DATA);
		result = result & PCI_MEM_MASK;
		result = (result ^ 0xFFFFFFFF) + 1;
		e1000_addr_size = result;
		e1000_debug("-->MMIO Size %x\n", e1000_addr_size);
                
		// Restore the MMIO addr to the device (unchanged)
		outl(PCI_CONFIG_DATA, addr);

		// Map the page in.
		e1000_mmio_base_addr = (uint32_t)mmio_alloc(addr, e1000_addr_size);
		if (e1000_mmio_base_addr == 0x00) {
			panic("Could not map in E1000 MMIO space\n");
		}
	}
	return;
}

// Scan the PCI data structures for our device.
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
					// Deal with the MMIO base, mapping, and size.
					e1000_handle_bar0(result);
				}						
			}
				
			// We found the device and configured it (if we get here). Return OK
			return 0;
		}
	}
	printk(" not found. No device configured.\n");
	return -1;
}

/* E1000 Read Register 32bit
 * Read a 32 bit value from a register at the given offset in the 
 * E1000 MMIO range.
 *
 * This function has IOPORT support, but is not used.
 */
uint32_t e1000_rr32(uint32_t offset) {

	if (e1000_mmio_base_addr) {
		return read_mmreg32(e1000_mmio_base_addr + offset);
	} else {
		return inl(e1000_io_base_addr + offset);
	}
}


/* E1000 Write Register 32bit
 * Write a 32 bit value from a register at the given offset in the 
 * E1000 MMIO range.
 *
 * This function has IOPORT support, but is not used.
 */
void e1000_wr32(uint32_t offset, uint32_t val) {

	if (e1000_mmio_base_addr) {
		write_mmreg32(e1000_mmio_base_addr + offset, val);
	} else {
		outl(e1000_io_base_addr + offset, val);
	}
}


/* E1000 Read From EEPROM
 * Read a 16 bit value from the EEPROM at the given offset 
 * in the EEPROM.
 *
 * WARNING: USE CAREFULLY. THIS HAS A WHILE LOOP IN IT.
 * if MMIO is not configured correctly, this will lock the kernel.
 */
uint16_t e1000_read_eeprom(uint32_t offset) {

	uint16_t eeprom_data;
	uint32_t eeprom_reg_val = e1000_rr32(E1000_EECD);

	// Request access to the EEPROM, then wait for access
	eeprom_reg_val = eeprom_reg_val | E1000_EECD_REQ;
	e1000_wr32(E1000_EECD, eeprom_reg_val);
        while((e1000_rr32(E1000_EECD) & E1000_EECD_GNT) == 0);

	// We now have access, write what value we want to read, then wait for access
	eeprom_reg_val = E1000_EERD_START | (offset << E1000_EERD_ADDR_SHIFT);
	e1000_wr32(E1000_EERD, eeprom_reg_val);
	while(((eeprom_reg_val = e1000_rr32(E1000_EERD)) & E1000_EERD_DONE) == 0);
	eeprom_data = (eeprom_reg_val & E1000_EERD_DATA_MASK) >> E1000_EERD_DATA_SHIFT;
	
	// Read the value (finally)
	eeprom_reg_val = e1000_rr32(E1000_EECD);

	// Tell the EEPROM we are done.
	e1000_wr32(E1000_EECD, eeprom_reg_val & ~E1000_EECD_REQ);

	return eeprom_data;

}

// Discover and record the MAC address for this device.
void e1000_setup_mac() {

	uint16_t eeprom_data = 0;
        uint32_t mmio_data = 0;

	/* TODO: WARNING - EXTREMELY GHETTO */
	e1000_debug("-->Setting up MAC addr\n");

	// Quark: For ID0 type, we read from the EEPROm. Else we read from RAL/RAH.
	if (device_id == INTEL_DEV_ID0) {

		// This is ungodly slow. Like, person perceivable time slow.
		for (int i = 0; i < 3; i++) {

			eeprom_data = e1000_read_eeprom(i);
			device_mac[2*i] = eeprom_data & 0x00FF;
			device_mac[2*i + 1] = (eeprom_data & 0xFF00) >> 8;

		}

	} else {
		
		// Get the data from RAL
	        mmio_data = e1000_rr32(E1000_RAL);

		// Do the big magic rain dance
	        device_mac[0] = mmio_data & 0xFF;
       		device_mac[1] = (mmio_data >> 8) & 0xFF;
        	device_mac[2] = (mmio_data >> 16) & 0xFF;
        	device_mac[3] = (mmio_data >> 24) & 0xFF;

		// Get the rest of the MAC data from RAH.
        	mmio_data = e1000_rr32(E1000_RAH);
		
		// Continue magic dance.
        	device_mac[4] = mmio_data & 0xFF;
        	device_mac[5] = (mmio_data >> 8) & 0xFF;
	}

	// Check if we need to invert the higher order bits (some E1000's)
	// Got this behavior from Barrelfish.
	// It's worth noting that if MMIO is screwed up, this is generally
	// the first warning sign.
	mmio_data = e1000_rr32(E1000_STATUS);
	if (mmio_data & E1000_STATUS_FUNC_MASK) {
		printk("UNTESTED LANB FUNCTIONALITY! MAY BE BREAKING MAC\n");
		device_mac[5] ^= 0x0100;
	}	

	// Program the device to use this mac (really only needed for ID0 type)
	e1000_wr32(E1000_RAH, 0x00000); // Set MAC invalid
	e1000_wr32(E1000_RAL, *(uint32_t*)device_mac);
	e1000_wr32(E1000_RAH, *(uint16_t*)(device_mac + 4) | 0x80000000);
	
	// Now make sure it read back out.
	// This is done to make sure everything is working correctly with the NIC
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
	// TODO: Get the right number of filters. Not sure how.
	//       however they SHOULD all default to all 0's, so
	//       this shouldnt be needed.
	for (int i = 1; i < 16; i++) {
		e1000_wr32(E1000_RAH + 8 * i, 0x0);
		e1000_wr32(E1000_RAL + 8 * i, 0x0);
	}


	// Clear MTA Table
	// TODO: Get the right number of filters. See above.
	for (int i = 0; i < 0x7F; i++) {
		e1000_wr32(E1000_MTA + 4 * i, 0x0);
	}

	e1000_debug("-->DEVICE MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	            0xFF & device_mac[0], 0xFF & device_mac[1],
	            0xFF & device_mac[2], 0xFF & device_mac[3],
	            0xFF & device_mac[4], 0xFF & device_mac[5]);
	return;
}

// Allocate and configure all the transmit and receive descriptors.
void e1000_setup_descriptors() {

	e1000_debug("-->Setting up tx/rx descriptors.\n");

	// Allocate room for the buffers. 
	// Must be 16 byte aligned

	// How many pages do we need?
        uint32_t num_rx_pages = ROUNDUP(NUM_RX_DESCRIPTORS * sizeof(struct e1000_rx_desc), PGSIZE) / PGSIZE;
        uint32_t num_tx_pages = ROUNDUP(NUM_TX_DESCRIPTORS * sizeof(struct e1000_tx_desc), PGSIZE) / PGSIZE;
	
	// Get the pages
	rx_des_kva = get_cont_pages(LOG2_UP(num_rx_pages), 0);
	tx_des_kva = get_cont_pages(LOG2_UP(num_tx_pages), 0);

	// +1 point for checking malloc result
	if (rx_des_kva == NULL) panic("Can't allocate page for RX Ring");
	if (tx_des_kva == NULL) panic("Can't allocate page for TX Ring");
	
	// Get the phys addr
	rx_des_pa = PADDR(rx_des_kva);
	tx_des_pa = PADDR(tx_des_kva);
	
	// Configure each descriptor.
	for (int i = 0; i < NUM_RX_DESCRIPTORS; i++) 
		e1000_set_rx_descriptor(i, TRUE); // True == Allocate memory for the descriptor
		
	for (int i = 0; i < NUM_TX_DESCRIPTORS; i++) 
		e1000_set_tx_descriptor(i);

	return;
}

// Configure a specific RX descriptor.
// Serves as a reset, too (with reset_buffer set to FALSE).
void e1000_set_rx_descriptor(uint32_t des_num, uint8_t reset_buffer) {
	
	//memset(&rx_des_kva[des_num], 0x00, sizeof(struct e1000_rx_desc));
	rx_des_kva[des_num].length = 0;
	rx_des_kva[des_num].csum = 0;
	rx_des_kva[des_num].status = 0;
	rx_des_kva[des_num].errors = 0;
	rx_des_kva[des_num].special = 0;
	
	// Check if we are allocating a buffer.
	// Note: setting this to TRUE not at boot time results in a memory leak.
	if (reset_buffer) {
		
		// Alloc a buffer
		char *rx_buffer = kmalloc(E1000_RX_MAX_BUFFER_SIZE, 0);
		if (rx_buffer == NULL) panic ("Can't allocate page for RX Buffer");
		
		// Set the buffer addr
		rx_des_kva[des_num].buffer_addr = PADDR(rx_buffer);
	}

	return;
}

// Configure a specific TX descriptor.
// Calling not at boot results in a memory leak.
void e1000_set_tx_descriptor(uint32_t des_num) {
	
	// Clear the bits.
	memset(&tx_des_kva[des_num], 0x00, sizeof(struct e1000_tx_desc));
	
	// Alloc space for the buffer
	char *tx_buffer = kmalloc(E1000_TX_MAX_BUFFER_SIZE, 0);
	if (tx_buffer == NULL) panic ("Can't allocate page for TX Buffer");

	// Set it.
	tx_des_kva[des_num].buffer_addr = PADDR(tx_buffer);
	return;
}

/* Actually configure the device.
 * This goes through the painstaking process of actually configuring the device
 * and preparing it for use. After this function, the device is turned on
 * in a good and functioning state (except interrupts are off).
 *
 * I give a brief description of what each bit of code does, but
 * the details of the bits are in most cases from the spec sheet
 * and exact details are a bit obfuscated. 
 *
 * This startup sequence is taken with love from BarrelFish.
 * <3 the multikernel.
 */
void e1000_configure() {
	
	uint32_t data;

	e1000_debug("-->Configuring Device.\n");
	
	// Clear interrupts
	e1000_wr32(E1000_IMC, E1000_IMC_ALL);

	// Disable receiver and transmitter
	e1000_wr32(E1000_RCTL, 0x00);
	e1000_wr32(E1000_TCTL, 0x00);

	// Reset the device
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

	// Setup MAC address
	e1000_setup_mac();	

        // Set RX Ring
        e1000_wr32(E1000_RDBAL, rx_des_pa);
        e1000_wr32(E1000_RDBAH, 0x00);

        // Set RX Ring Size
        // Size in bytes.
        e1000_wr32(E1000_RDLEN, (NUM_RX_DESCRIPTORS / 8) << 7);
       
	// Disablie the split and replication control queue 
	e1000_wr32(E1000_SRRCTRL, 0x00);

	// Set head and tail pointers.
        e1000_wr32(E1000_RDH, 0x00);
        e1000_wr32(E1000_RDT, 0x00);

        // Receive descriptor control
	e1000_wr32(E1000_RXDCTL, E1000_RXDCTL_ENABLE | E1000_RXDCTL_WBT | E1000_RXDCTL_MAGIC);

	// Disable packet splitting.
        data = e1000_rr32(E1000_RFCTL);
        data = data & ~E1000_RFCTL_EXTEN;
        e1000_wr32(E1000_RFCTL, data);

	// Enable packet reception
	data = e1000_rr32(E1000_RCTL);
	data = data | E1000_RCTL_EN | E1000_RCTL_BAM;
	e1000_wr32(E1000_RCTL, data);

	// Bump the tail pointer. This MUST be done at this point 
	// _AFTER_ packet receiption is enabled. See 85276 spec sheet.
        e1000_wr32(E1000_RDT, NUM_RX_DESCRIPTORS - 1);

	// Set TX Ring
	e1000_wr32(E1000_TDBAL, tx_des_pa);
	e1000_wr32(E1000_TDBAH, 0x00);

	// Set TX Des Size.
	// This is the number of 8 descriptor sets, it starts at the 7th bit.
	e1000_wr32(E1000_TDLEN, ((NUM_TX_DESCRIPTORS / 8) << 7));

        // Transmit inter packet gap register
        // XXX: Recomended magic. See 13.4.34
        e1000_wr32(E1000_TIPG, 0x00702008);

	// Set head and tail pointers.
	e1000_wr32(E1000_TDH, 0x00);
	e1000_wr32(E1000_TDT, 0x00);

        // Tansmit desc control
        e1000_wr32(E1000_TXDCTL, E1000_TXDCTL_MAGIC | E1000_TXDCTL_ENABLE);

	// Enable transmit
	// Enable + pad short packets + Back off time + Collision thresh
	// The 0x0F000 is the back off time, and 0x0010 is the collision thresh.
	e1000_wr32(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | 0x0F010);

	return;
}

// Reset the device.
void e1000_reset() {
	e1000_debug("-->Resetting device..... ");

	// Get control
	uint32_t ctrl = e1000_rr32(E1000_CTRL);

	// Set the reset bit
	ctrl = ctrl & E1000_CTRL_RST;

	// Write the reset bit
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

// Configure and enable interrupts
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

// Code that is executed when an interrupt comes in on IRQ e1000_irq
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

		// Check to see if the interrupt was packet based.
		if ((interrupt_status & E1000_ICR_INT_ASSERTED) && (interrupt_status & E1000_ICR_RXT0)) {
			e1000_interrupt_debug("---->Packet Received\n");
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

// Check to see if a packet arrived, and process the packet.
void e1000_handle_rx_packet() {
	
	uint16_t packet_size;
	uint32_t status;
	uint32_t head = e1000_rr32(E1000_RDH);

	//printk("Current head is: %x\n", e1000_rr32(E1000_RDH));
	//printk("Current tail is: %x\n", e1000_rr32(E1000_RDT));
	
	// If the HEAD is where we last processed, no new packets.
	if (head == e1000_rx_index) {
		e1000_frame_debug("-->Nothing to process. Returning.");
		return;
	}
	
	// Set our current descriptor to where we last left off.
	uint32_t rx_des_loop_cur = e1000_rx_index;
	uint32_t frame_size = 0;
	uint32_t fragment_size = 0;
	uint32_t num_frags = 0;
	
	// Grab a buffer for this packet.
	char *rx_buffer = kmalloc(MAX_FRAME_SIZE, 0);
	if (rx_buffer == NULL) panic ("Can't allocate page for incoming packet.");
	
	
	do {
		// Get the descriptor status
		status = rx_des_kva[rx_des_loop_cur].status;

		// If the status is 0x00, it means we are somehow trying to process 
		//  a packet that hasnt been written by the NIC yet.
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
	
		// See how big this fragment? is.
		fragment_size = rx_des_kva[rx_des_loop_cur].length;
		
		// If we've looped through the entire ring and not found a terminating packet, bad nic state.
		// Panic or clear all descriptors? This is a nic hardware error. 
		if (num_frags && (rx_des_loop_cur == head)) {
			e1000_frame_debug("-->ERR: No ending segment found in RX buffer.\n");
			panic("RX Descriptor Ring out of sync.");
		}
		
		// Denote that we have at least 1 fragment.
		num_frags++;
		
		// Make sure ownership is correct. Packet owned by the NIC (ready for kernel reading)
		//  is denoted by a 1. Packet owned by the kernel (ready for NIC use) is denoted by 0.
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
		
		// Reset the descriptor. Reuse current buffer (False means don't realloc).
		e1000_set_rx_descriptor(rx_des_loop_cur, FALSE);
		
		// Note: We mask out fragment sizes at 0x3FFFF. There can be at most 2048 of them.
		// This can not overflow the uint32_t we allocated for frame size, so
		// we dont need to worry about mallocing too little then overflowing when we read.
		frame_size = frame_size + fragment_size;
		
		// Advance to the next descriptor
		rx_des_loop_cur = (rx_des_loop_cur + 1) % NUM_RX_DESCRIPTORS;

	} while ((status & E1000_RXD_STAT_EOP) == 0); // Check to see if we are at the final fragment

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

	// Paul:Mildly hacky stuff for LWIP
	// TODO: Why was this necessary for LWIP?
	spin_lock(&packet_buffers_lock);

	if (num_packet_buffers >= MAX_PACKET_BUFFERS) {
		printd("WARNING: DROPPING PACKET!\n");
		spin_unlock(&packet_buffers_lock);
		kfree(rx_buffer);
	
		// Advance the tail pointer				
		e1000_rx_index = rx_des_loop_cur;
        e1000_wr32(E1000_RDT, (e1000_rx_index -1) % NUM_RX_DESCRIPTORS);
		return;
	}

	packet_buffers[packet_buffers_tail] = rx_buffer;
	packet_buffers_sizes[packet_buffers_tail] = frame_size;
		
	packet_buffers_tail = (packet_buffers_tail + 1) % MAX_PACKET_BUFFERS;
	num_packet_buffers++;

	spin_unlock(&packet_buffers_lock);
	// End mildy hacky stuff for LWIP

	//Log where we should start reading from next time we trap				
	e1000_rx_index = rx_des_loop_cur;
	
	// Bump the tail pointer. It should be 1 behind where we start reading from.
	e1000_wr32(E1000_RDT, (e1000_rx_index -1) % NUM_RX_DESCRIPTORS);
				
	// Chew on the frame data. Command bits should be the same for all frags.
	//e1000_process_frame(rx_buffer, frame_size, current_command);
	
	return;
}

// Main routine to send a frame. Just sends it and goes.
// Card supports sending across multiple fragments, we don't.
// Would we want to write a function that takes a larger packet and generates fragments?
// This seems like the stacks responsibility. Leave this for now. may in future
// Remove the max size cap and generate multiple packets.
int e1000_send_frame(const char *data, size_t len) {

	if (data == NULL)
		return -1;
	if (len == 0)
		return 0;

	// Find where we want to write
	uint32_t head = e1000_rr32(E1000_TDH);

	
	// Fail if we are out of space
	if (((e1000_tx_index + 1) % NUM_TX_DESCRIPTORS) == head) {
		e1000_frame_debug("-->TX Ring Buffer Full!\n");
		return -1;
	}
	
	// Fail if we are too large
	if (len > MAX_FRAME_SIZE) {
		e1000_frame_debug("-->Frame Too Large!\n");
		return -1;
	}
	
	// Move the data
	memcpy(KADDR(tx_des_kva[e1000_tx_index].buffer_addr), data, len);

	// Set the length
	tx_des_kva[e1000_tx_index].lower.flags.length = len;
	
	// Magic that means send 1 fragment and report.
	tx_des_kva[e1000_tx_index].lower.flags.cmd = 0x0B;

	// Track our location
	e1000_tx_index = (e1000_tx_index + 1) % NUM_TX_DESCRIPTORS;
	
	// Bump the tail.
	e1000_wr32(E1000_TDT, e1000_tx_index);

	e1000_frame_debug("-->Sent packet.\n");
	
return len;
}
