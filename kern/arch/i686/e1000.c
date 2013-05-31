/** @file
 * @brief E1000 Driver       
 *
 * EXPERIMENTAL. DO NOT USE IF YOU DONT KNOW WHAT YOU ARE DOING
 *
 * See Info below 
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 * @author David Zhu <yuzhu@cs.berkeley.edu>
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
#include <net/nic_common.h>
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
#include <net/ip.h>

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
// pointer to receive descriptors
struct e1000_rx_desc *rx_des_kva;
unsigned long rx_des_pa;
// current rx index
uint32_t e1000_rx_index = 0;


// Vars relating to the transmit descriptor ring
struct e1000_tx_desc *tx_des_kva;
unsigned long tx_des_pa;
uint32_t e1000_tx_index = 0;

extern uint8_t eth_up;

// The PCI device ID we detect
// This is used for quark behavior
uint16_t device_id = 0;


// Hacky variables relating to delivering packets
extern uint32_t packet_buffer_count;
extern char* packet_buffers[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_sizes[MAX_PACKET_BUFFERS];
extern uint32_t packet_buffers_head;
extern uint32_t packet_buffers_tail;
spinlock_t packet_buffers_lock;

// Allow us to register our send_frame as the global send_frame
extern int (*send_frame)(const char *CT(len) data, size_t len);

// compat defines that make transitioning easier
#define E1000_RX_DESC(x) rx_des_kva[x]

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
	send_pbuf = &e1000_send_pbuf;
	recv_pbuf = &e1000_recv_pbuf;

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

		/* Get a virt address chunk */
		e1000_mmio_base_addr = get_vmap_segment(e1000_addr_size >> PGSHIFT);
		if (!e1000_mmio_base_addr)
			panic("Could not aquire VM space for e1000 MMIO\n");
		/* Map the pages in */
		if (map_vmap_segment(e1000_mmio_base_addr, addr,
		                     e1000_addr_size >> PGSHIFT, PTE_P | PTE_KERN_RW))
			panic("Unable to map e1000 MMIO\n");
	}
	return;
}

// Scan the PCI data structures for our device.
int e1000_scan_pci(void)
{
	struct pci_device *pcidev;
	uint32_t result;
	printk("Searching for Intel E1000 Network device...");
	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* Ignore non Intel Devices */
		if (pcidev->ven_id != INTEL_VENDOR_ID)
			continue;
		/* Ignore non E1000 devices */
		switch (pcidev->dev_id) {
			case (INTEL_82543GC_ID):
			case (INTEL_82540EM_ID):
			case (INTEL_82545EM_ID):
			case (INTEL_82576_ID):
			case (INTEL_82576NS_ID):
				break;
			default:
				continue;
		}
		printk(" found on BUS %x DEV %x FUNC %x\n", pcidev->bus, pcidev->dev,
		       pcidev->func);
		/* TODO: WARNING - EXTREMELY GHETTO  (can only handle 1 NIC) */
		if (device_id) {
			printk("[e1000] Already configured a device, won't do another\n");
			continue;
		}
		device_id = pcidev->dev_id;
		/* Find the IRQ */
		e1000_irq = pcidev->irqline;
		e1000_debug("-->IRQ: %u\n", e1000_irq);
		/* Loop over the BARs */
		/* TODO: pci layer should scan these things and put them in a pci_dev
		 * struct */
		/* SelectBars based on the IORESOURCE_MEM */
		for (int k = 0; k <= 5; k++) {
	    	/* TODO: clarify this magic */
			int reg = 4 + k;
			result = pcidev_read32(pcidev, reg << 2);

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
					e1000_debug("-->MMIO Mode, Base: %p\n", result);
					// Deal with the MMIO base, mapping, and size.
					e1000_handle_bar0(result);
				}						
			}
		}
	}
	if (device_id)
		return 0;
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
	if (device_id == INTEL_82540EM_ID) {

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
	
	// Clear Interrupts
	e1000_wr32(E1000_IMC, E1000_IMC_ALL);
	E1000_WRITE_FLUSH();

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

	e1000_wr32(E1000_CTRL, e1000_rr32(E1000_CTRL) | E1000_CTRL_RST);

	e1000_debug(" done.\n");

	return;
}

void e1000_irq_enable() {
	printk("e1000 enabled\n");
	e1000_wr32(E1000_IMS, IMS_ENABLE_MASK);
	E1000_WRITE_FLUSH();
}

// Configure and enable interrupts
void e1000_setup_interrupts() {
	
	extern handler_t interrupt_handlers[];
	
	e1000_debug("-->Setting interrupts.\n");
	
	// Set throttle register
	e1000_wr32(E1000_ITR, 0x0000);
	
	// Clear interrupts
	e1000_wr32(E1000_IMS, 0xFFFFFFFF);
	e1000_wr32(E1000_IMC, E1000_IMC_ALL);
	
	// Set interrupts
	e1000_irq_enable();

	// Kernel based interrupt stuff
	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + e1000_irq, e1000_interrupt_handler, 0);

	// Enable irqs for the e1000
	// TODO: figure out where the interrupts are actually going..
#ifdef CONFIG_ENABLE_MPTABLES
	/* TODO: this should be for any IOAPIC EOI, not just MPTABLES */
	ioapic_route_irq(e1000_irq, E1000_IRQ_CPU);	
	printk("ioapic rout\n");

#else 
	// This will route the interrupts automatically to CORE 0
	// Call send_kernel_message if you want to route them somewhere else
	pic_unmask_irq(e1000_irq);
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	enable_irq();
	printk("picroute\n");
#endif

	return;
}

// Code that is executed when an interrupt comes in on IRQ e1000_irq
void e1000_interrupt_handler(struct hw_trapframe *hw_tf, void *data)
{
	e1000_interrupt_debug("\nNic interrupt on core %u!\n", lapic_get_id());

	// Read the offending interrupt(s)
	// Note: Reading clears the interrupts
	uint32_t icr = e1000_rr32(E1000_ICR);

	
	/* Interrupt did not come from our card.., handle one interrupt per isr */
	if (!icr) return; 
	/* disable interrupts, this may not be necessary as AUTOMASK of interrupts
	 * is enabled on some cards
	 * but we do it anyways to be safe..
	 */
	e1000_wr32(E1000_IMC, ~0);
	E1000_WRITE_FLUSH();

	//printk("Interrupt status: %x\n", icr);

	if ((icr & E1000_ICR_INT_ASSERTED) && (icr & E1000_ICR_RXT0)){
		e1000_interrupt_debug("---->Packet Received\n");
#ifdef CONFIG_SOCKET
//#if 0
		e1000_clean_rx_irq();
		// e1000_recv_pbuf(); // really it is now performing the function of rx_clean
#else
		e1000_handle_rx_packet();
#endif
	}	
	e1000_irq_enable();
}

void process_pbuf(uint32_t srcid, long a0, long a1, long a2)
{
	if (srcid != core_id())
		warn("pbuf came from a different core\n");
	/* assume it is an ip packet */
	struct pbuf* pb = (struct pbuf*) a0;
	//printk("processing pbuf \n");
	/*TODO: check checksum and drop */
	/*check packet type*/
	struct ethernet_hdr *ethhdr = (struct ethernet_hdr *) pb->payload;
	//printk("start of eth %p \n", pb->payload);
	//print_pbuf(pb);
	if (memcmp(ethhdr->dst_mac, device_mac, 6)){
		e1000_debug("mac address do not match, pbuf freed \n");
		pbuf_free(pb);
	}
	switch(htons(ethhdr->eth_type)){
		case ETHTYPE_IP:
			if (!pbuf_header(pb, -(ETH_HDR_SZ)))
				ip_input(pb);
			else
				warn("moving ethernet header in pbuf failed..\n");
			break;
		case ETHTYPE_ARP:
			break;
		default:
			//warn("packet type unknown");
			pbuf_free(pb);
	}
}

static void schedule_pb(struct pbuf* pb) {
	/* routine kernel message is kind of heavy weight, because it records src/dst etc */
	/* TODO: consider a core-local chain of pbufs */
	// using core 3 for network stuff..XXX
	send_kernel_message(3, (amr_t) process_pbuf, (long)pb, 0, 0, KMSG_ROUTINE);
	// send_kernel_message(core_id(), (amr_t) process_pbuf, (long)pb, 0, 0, KMSG_ROUTINE);
	return;
}
// Check to see if a packet arrived, and process the packet.
void e1000_handle_rx_packet() {
	
	uint16_t packet_size;
	uint32_t status;
	// find rx descriptor head
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


#ifdef CONFIG_APPSERVER
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

#ifdef CONFIG_ETH_AUDIO
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
#endif /* CONFIG_ETH_AUDIO */

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
	dumppacket((unsigned char *)rx_buffer, frame_size);
				
	// Chew on the frame data. Command bits should be the same for all frags.
	//e1000_process_frame(rx_buffer, frame_size, current_command);
	
	return;
}

static void e1000_clean_rx_irq() {
	// e1000_rx_index is the last one that we have processed
	uint32_t i= e1000_rx_index;
	// E1000 RDH is the last descriptor written by the hardware
	uint32_t head = e1000_rr32(E1000_RDH);
	uint32_t length = 0;
	struct e1000_rx_desc *rx_desc =  &(E1000_RX_DESC(i));

	// what happens when i go around the ring? 
	while (rx_desc->status & E1000_RXD_STAT_DD){
		struct pbuf* pb;
		uint8_t status;
		rx_desc = &rx_des_kva[i];
		// buffer_info = &rx_des_kva[i];
		status = rx_desc->status;
		pb = pbuf_alloc(PBUF_RAW, 0 , PBUF_MTU);
#if ETH_PAD_SIZE
		pbuf_header(pb, -ETH_PAD_SIZE); /* drop the padding word */
#endif
		// fragment size
		length = le16_to_cpu(rx_desc->length);

		length -= 4;

		memcpy(pb->payload, KADDR(E1000_RX_DESC(i).buffer_addr), length);
		// skb_put(skb, length);
		pb->len = length;
		pb->tot_len = length;
		schedule_pb(pb);
		// do all the error handling 
next_desc:
		// this replaces e1000_set_rx_descriptor
		rx_desc->status = 0;
		if (++i == NUM_RX_DESCRIPTORS) i = 0;
		rx_desc = &(E1000_RX_DESC(i)); 
	}
	//setting e1000_RDH?
 		printk ("cleaned index %d to %d \n", e1000_rx_index, i-1);
		e1000_rx_index = i;
}

struct pbuf* e1000_recv_pbuf(void) {
	uint16_t packet_size;
	uint32_t status;
	// recv head
	uint32_t head = e1000_rr32(E1000_RDH);

	printk("Current head is: %x\n", e1000_rr32(E1000_RDH));
	printk("Current tail is: %x\n", e1000_rr32(E1000_RDT));
	// e1000_rx_index = cleaned
	// If the HEAD is where we last processed, no new packets.
	if (head == e1000_rx_index) {
		e1000_frame_debug("-->Nothing to process. Returning.");
		return NULL;
	}
	// Set our current descriptor to where we last left off.
	uint32_t rx_des_loop_cur = e1000_rx_index;
	uint32_t frame_size = 0;
	uint32_t fragment_size = 0;
	uint32_t num_frags = 0;

	uint32_t top_fragment = rx_des_loop_cur; 
	struct pbuf* pb = pbuf_alloc(PBUF_RAW, 0, PBUF_MTU);
	if (!pb){
		warn("pbuf allocation failed, packet dropped\n");
		return NULL;
	}

	uint32_t copied = 0;
#if ETH_PAD_SIZE
	pbuf_header(pb, -ETH_PAD_SIZE); /* drop the padding word */
#endif
	// pblen is way too big? it is not an indication of the size but the allocation
	printk("pb loc %p , pb len %d \n", pb, pb->len);
	void* rx_buffer = pb->payload;

	/* The following loop generates 1 and only 1 pbuf out of 1(likely) 
	 * or more fragments. 
	 * TODO: convert this loop to clean rx irq style which is capable of 
	 * handling multiple packet / pbuf receptions
	 */

	do {
		// Get the descriptor status
		status = rx_des_kva[rx_des_loop_cur].status;

		// If the status is 0x00, it means we are somehow trying to process 
		// a packet that hasnt been written by the NIC yet.
		if (status & E1000_RXD_STAT_DD) {
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
		printk ("got out of the dead loop \n");
	
		// See how big this fragment is.
		fragment_size = rx_des_kva[rx_des_loop_cur].length;
		printk("fragment size %d\n",fragment_size);
		
		// If we've looped through the entire ring and not found a terminating packet, bad nic state.
		// Panic or clear all descriptors? This is a nic hardware error. 
		if (num_frags && (rx_des_loop_cur == head)) {
			e1000_frame_debug("-->ERR: No ending segment found in RX buffer.\n");
			panic("RX Descriptor Ring out of sync.");
		}
		// Denote that we have at least 1 fragment.
		num_frags++;
		if (num_frags > 1) warn ("we have fragments in the network \n");
		// Make sure ownership is correct. Packet owned by the NIC (ready for kernel reading)
		// is denoted by a 1. Packet owned by the kernel (ready for NIC use) is denoted by 0.
		if ((status & E1000_RXD_STAT_DD) == 0x0) {
			e1000_frame_debug("-->ERR: Current RX descriptor not owned by software. Panic!");
			warn("RX Descriptor Ring OWN out of sync");
		}
		
		// Deal with packets too large
		if ((frame_size + fragment_size) > MAX_FRAME_SIZE) {
			e1000_frame_debug("-->ERR: Nic sent %u byte packet. Max is %u\n", frame_size, MAX_FRAME_SIZE);
			warn("NIC Sent packets larger than configured.");
		}
		
		memcpy(rx_buffer, KADDR(rx_des_kva[rx_des_loop_cur].buffer_addr), fragment_size);
		copied += fragment_size;
		printk("fragment size %d \n", fragment_size);
		rx_buffer += fragment_size;
		

		// Copy into pbuf allocated for this	 
		// TODO: reuse the pbuf later
		// TODO:real driver uses a pbuf allocated (MTU sized) per descriptor and recycles that
		// TODO:real driver also does not handle fragments.. simply drops them

		// Reset the descriptor. Reuse current buffer (False means don't realloc).
		e1000_set_rx_descriptor(rx_des_loop_cur, FALSE);
		
		// Note: We mask out fragment sizes at 0x3FFFF. There can be at most 2048 of them.
		// This can not overflow the uint32_t we allocated for frame size, so
		// we dont need to worry about mallocing too little then overflowing when we read.
		frame_size = frame_size + fragment_size;
		
		/*Advance to the next descriptor*/
		rx_des_loop_cur = (rx_des_loop_cur + 1) % NUM_RX_DESCRIPTORS;

	} while ((status & E1000_RXD_STAT_EOP) == 0); // Check to see if we are at the final fragment

	// rx_des_loop_cur has gone past the top_fragment
	// printk("Copied %d bytes of data \n", copied);
	// ethernet crc performed in hardware
	copied -= 4;

	pb->len = copied;
	pb->tot_len = copied;
	schedule_pb(pb);
	return pb;
}

#if 0

int e1000_clean_rx(){
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_rx_desc *rx_desc, *next_rxd;
	struct e1000_buffer *buffer_info, *next_buffer;
	unsigned long flags;
	uint32_t length;
	uint8_t last_byte;
	unsigned int i;
	int cleaned_count = 0;
	boolean_t cleaned = FALSE;
	unsigned int total_rx_bytes=0, total_rx_packets=0;

	i = rx_ring->next_to_clean;
	// rx_desc is the same as rx_des_kva[rx_des_loop_cur]
	rx_desc = E1000_RX_DESC(*rx_ring, i);
	buffer_info = &rx_ring->buffer_info[i];

	while (rx_desc->status & E1000_RXD_STAT_DD) {
		struct sk_buff *skb;
		u8 status;

#ifdef CONFIG_E1000_NAPI
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
#endif
		status = rx_desc->status;
		skb = buffer_info->skb;
		buffer_info->skb = NULL;

		prefetch(skb->data - NET_IP_ALIGN);

		if (++i == rx_ring->count) i = 0;
		next_rxd = E1000_RX_DESC(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = TRUE;
		cleaned_count++;
		pci_unmap_single(pdev,
		                 buffer_info->dma,
		                 buffer_info->length,
		                 PCI_DMA_FROMDEVICE);

		length = le16_to_cpu(rx_desc->length);

		if (unlikely(!(status & E1000_RXD_STAT_EOP))) {
			/* All receives must fit into a single buffer */
			E1000_DBG("%s: Receive packet consumed multiple"
				  " buffers\n", netdev->name);
			/* recycle */
			buffer_info->skb = skb;
			goto next_desc;
		}

		if (unlikely(rx_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK)) {
			last_byte = *(skb->data + length - 1);
			if (TBI_ACCEPT(&adapter->hw, status,
			              rx_desc->errors, length, last_byte)) {
				spin_lock_irqsave(&adapter->stats_lock, flags);
				e1000_tbi_adjust_stats(&adapter->hw,
				                       &adapter->stats,
				                       length, skb->data);
				spin_unlock_irqrestore(&adapter->stats_lock,
				                       flags);
				length--;
			} else {
				/* recycle */
				buffer_info->skb = skb;
				goto next_desc;
			}
		}

		/* adjust length to remove Ethernet CRC, this must be
		 * done after the TBI_ACCEPT workaround above */
		length -= 4;

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += length;
		total_rx_packets++;

		/* code added for copybreak, this should improve
		 * performance for small packets with large amounts
		 * of reassembly being done in the stack */
		if (length < copybreak) {
			struct sk_buff *new_skb =
			    netdev_alloc_skb(netdev, length + NET_IP_ALIGN);
			if (new_skb) {
				skb_reserve(new_skb, NET_IP_ALIGN);
				memcpy(new_skb->data - NET_IP_ALIGN,
				       skb->data - NET_IP_ALIGN,
				       length + NET_IP_ALIGN);
				/* save the skb in buffer_info as good */
				buffer_info->skb = skb;
				skb = new_skb;
			}
			/* else just continue with the old one */
		}
		/* end copybreak code */
		skb_put(skb, length);

		/* Receive Checksum Offload */
		e1000_rx_checksum(adapter,
				  (uint32_t)(status) |
				  ((uint32_t)(rx_desc->errors) << 24),
				  le16_to_cpu(rx_desc->csum), skb);

		skb->protocol = eth_type_trans(skb, netdev);
#ifdef CONFIG_E1000_NAPI
		if (unlikely(adapter->vlgrp &&
			    (status & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
						 le16_to_cpu(rx_desc->special) &
						 E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_receive_skb(skb);
		}
#else /* CONFIG_E1000_NAPI */
		if (unlikely(adapter->vlgrp &&
			    (status & E1000_RXD_STAT_VP))) {
			vlan_hwaccel_rx(skb, adapter->vlgrp,
					le16_to_cpu(rx_desc->special) &
					E1000_RXD_SPC_VLAN_MASK);
		} else {
			netif_rx(skb);
		}
#endif /* CONFIG_E1000_NAPI */
		netdev->last_rx = jiffies;

next_desc:
		rx_desc->status = 0;

		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= E1000_RX_BUFFER_WRITE)) {
			adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;
	}
	rx_ring->next_to_clean = i;

	cleaned_count = E1000_DESC_UNUSED(rx_ring);
	if (cleaned_count)
		adapter->alloc_rx_buf(adapter, rx_ring, cleaned_count);

	adapter->total_rx_packets += total_rx_packets;
	adapter->total_rx_bytes += total_rx_bytes;
	return cleaned;
}
}

#endif

int e1000_send_pbuf(struct pbuf *p) {
	int len = p->tot_len;
	// print_pbuf(p);
	if (p == NULL) 
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
	int cplen = pbuf_copy_out(p, KADDR(tx_des_kva[e1000_tx_index].buffer_addr), len, 0);

	for(int i = 0; i< cplen; i++){
		printd("%x", ((uint8_t*)KADDR(tx_des_kva[e1000_tx_index].buffer_addr))[i]);
	}
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
