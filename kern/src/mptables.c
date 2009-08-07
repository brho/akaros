#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/smp.h>
#include <arch/apic.h>
#include <arch/ioapic.h>


#include <ros/memlayout.h>

#include <stdio.h>
#include <string.h>
#include <pmap.h>
#include <kmalloc.h>

#include <mptables.h>
#include <pci.h>

/* Basic MP Tables Parser
 *
 * Memory Locations and table structs taken from FreeBSD's mptable.c and modified.
 *
 * Put cool text here
 * 
 * TODO: Extended table support
 * TODO: KMALLOC null checks
 * TODO: Error checking, pci device not found, etc.
 */


tableEntry basetableEntryTypes[] =
{
    { 0, 20, "Processor" },
    { 1,  8, "Bus" },
    { 2,  8, "I/O APIC" },
    { 3,  8, "I/O INT" },
    { 4,  8, "Local INT" }
};

// Important global items
enum interrupt_modes current_interrupt_mode;

void * mp_entries[NUM_ENTRY_TYPES]; // Array of entry type arrays. Indexable by entry id
int mp_entries_count[NUM_ENTRY_TYPES]; // How large each array is.

pci_int_group pci_int_groups[PCI_MAX_BUS][PCI_MAX_DEV];
isa_int_entry isa_int_entries[NUM_IRQS];
ioapic_entry ioapic_entries[IOAPIC_MAX_ID];

// All this max stuff is removable. Here for debugging (originally for dynamically sized arrays.)
int max_pci_device = -1;
int max_pci_bus = -1;
int num_ioapics = -1;
int max_ioapic_id = -1;


void mptables_parse() {
	
	mpfps_t *mpfps;
	
	// Memsets.
	// Setup the indexable ioapic array.
	// YOU MUST check the flag field to see if its 0. If 0, unusable.
	// Ths is defined by MPTables, and I leaverage this with the memset below.
	memset(ioapic_entries, 0, sizeof(ioapic_entries));
	
	// We define an IOAPIC ID over 255 to be invalid.
	memset(pci_int_groups, 0xFF, sizeof(pci_int_groups));
	memset(isa_int_entries, 0xFF, sizeof(isa_int_entries));
	
	
	mptables_info("Starting MPTables Parsing...\n");
	
	// Basic struct:
	//	1) Find floating pointer
	//	2) Go to addr referenced by floating pointer
	//	3) Read table header info
	
	// We now have to search through 3 address regions searching for a magic string.
	
	// I unrolled this loop because I thought it was easier to read. This can be 
	// easily packed into a for loop on an array.
	
	// Note: The pointer can actually be elsewhere. See the FBSD MPTables implimentation for more info
	// Just doesn't make sense right now to check more places.
	
	// Search the BIOS ROM Address Space (MOST LIKELY)
	mptables_dump("-->Searching BIOS ROM Area...\n");
	mpfps = find_floating_pointer((physaddr_t)KADDR(BIOS_ROM_BASE), (physaddr_t)KADDR(BIOS_ROM_BOUND));
	
	if (mpfps == NULL) {
		
		// Search the EBDA UNTESTED
		
		// First, we have to find the darn EBDA Addr.
		// This USSUALLY works. May be some cases where it doesnt.
		// See osdev x86 mem-map for more information
		physaddr_t ebda_base = read_mmreg32((uint32_t)KADDR(EBDA_POINTER));
		
		if (ebda_base) {
			ebda_base = ebda_base << 4;
			ebda_base = (physaddr_t)KADDR(ebda_base);
			physaddr_t ebda_bound = ebda_base + EBDA_SIZE - sizeof(mpfps_t);
			
			mptables_dump("-->Searching EBDA...\n");
			mpfps = find_floating_pointer(ebda_base, ebda_bound);
		}
	}

	if (mpfps == NULL) {
		// Search the last KB of system memory UNTESTED
		
		// Note: Will only be there if it not in the EBDA. So this must be called after the EBDA check.
		
		// This logic is ripped from mptables without much understanding. No machine to test it on.
		
		physaddr_t top_of_mem = read_mmreg32((uint32_t)KADDR(TOPOFMEM_POINTER));
		
		if (top_of_mem) {
			--top_of_mem;
			top_of_mem = top_of_mem * 1024;
			
			top_of_mem = (physaddr_t)KADDR(top_of_mem);
		
	    	mptables_dump("-->Searching top of (real mode) Ram...\n");
			mpfps = find_floating_pointer(top_of_mem, top_of_mem + 1024 - sizeof(mpfps_t));
		}
	}

	// If we can't find the pointer, it means we are running on a non-mp compliant machine.
	// This is bad. We can't do interrupts the way we want.
	// We could have this trigger a MODE in which we operate using the standard PIC, if we really wanted.
	if (mpfps == NULL) {
		panic("MPTABLES Not found. IOAPIC and interrupts will not function properly. <Insert whale held up by smaller birds here>");
	}
	
	mptables_info("-->MPTables Floating Pointer Structure found @ KVA 0x%p.\n", mpfps);
	
	mptables_info("-->Current Interrupt Mode: ");
	// Identify our interrupt mode
	if (mpfps->mpfb2 & IMCRP_MASK) {
		current_interrupt_mode = PIC;
		mptables_info("PIC\n");
	}
	else {
		current_interrupt_mode = VW;
		mptables_info("Virtual Wire\n");
	}
	
	configuration_parse((physaddr_t)KADDR((uint32_t)(mpfps->pap)));
	proc_parse(		mp_entries[PROC], mp_entries_count[PROC]);
	bus_parse(		mp_entries[BUS], mp_entries_count[BUS]);
	ioapic_parse(	mp_entries[IOAPIC], mp_entries_count[IOAPIC]);
	int_parse(		mp_entries[INT], mp_entries_count[INT]);
	lint_parse(		mp_entries[LINT], mp_entries_count[LINT]);


	// // debugging the parsing.
	// cprintf("\n");
	// cprintf("max_pci_device: %u\n", max_pci_device);
	// cprintf("max_pci_bus: %u\n", max_pci_bus);
	// cprintf("num_ioapics: %u\n", num_ioapics);
	// cprintf("max_ioapic_id: %u\n", max_ioapic_id);
	// int n =0;
	// for (int i = 0; i <= max_pci_bus; i++) {
	// 	for (int j = 0; j <= max_pci_device; j++) {
	// 		for (int k = 0; k < 4; k++) {
	// 			if (pci_int_groups[i][j].intn[k].dstApicID != 0xFFFF) {
	// 				cprintf("Bus: %u\n", i);
	// 				cprintf("Device: %u\n", j);
	// 				cprintf("ApicID: %u\n", pci_int_groups[i][j].intn[k].dstApicID);
	// 				cprintf("INTLINE: %u\n", pci_int_groups[i][j].intn[k].dstApicINT);
	// 				cprintf("\n");
	// 				n++;
	// 			}
	// 		}
	// 	}
	// }
	// cprintf("n: %u\n", n);
	// for (int i = 0; i <= max_ioapic_id; i++) {
	// 	if ((ioapic_entries[i].apicFlags & 0x1) != 0) {
	// 		cprintf("IOAPIC ID: %u\n", ioapic_entries[i].apicID);
	// 		cprintf("IOAPIC Offset: %x\n", ioapic_entries[i].apicAddress);
	// 		cprintf("\n");
	// 	}
	// }
	// panic("AHH!");	
	
}

// Searches the given address range, starting at addr first, to (including) last, for the mptables pointer.
// Does not esure base/bounds are sane.
mpfps_t *find_floating_pointer(physaddr_t base, physaddr_t bound) {

	mpfps_t* mpfps = (mpfps_t*)base;	

	// Loop over the entire range looking for the signature. The signature is ascii _MP_, which is
	//  stored in the given MP_SIG
	while ( ((physaddr_t)mpfps <= bound) && (read_mmreg32((uint32_t)(mpfps->signature)) != MP_SIG)) {
		mpfps++;
	}
	
	if ((physaddr_t)mpfps > bound) 
		return NULL;
	
	// Now perform a checksum on the float
	if (checksum((physaddr_t)mpfps, sizeof(mpfps_t) * mpfps->length) == FALSE) {
		mptables_dump("-->Failed a MPTables Floating Pointer Structure checksum @ KVA 0x%p.\n", mpfps);
		
		// If we fail we need to keep checking. But if we are on the last addr
		// 	we just fail.
		if ((physaddr_t)mpfps == bound)
			return NULL;
		
		return find_floating_pointer((physaddr_t)(mpfps + 1), bound);
	}
	
	return mpfps;
}

bool checksum(physaddr_t addr, uint32_t len) {
	// MP Table checksums must add up to 0.
	uint8_t checksum = 0;

	for (int i = 0; i < len; i++)
		checksum += *((uint8_t*)addr + i);

	return (checksum == 0);
}

// Go over the configuration table and parse / load the data into the global variables
// NOTE: This uses kmalloc. Might be cleaner (but less flexable) to allocate fixed amounts
//	set to high numbers.
void configuration_parse(physaddr_t conf_addr) {
	
	int num_processed[NUM_ENTRY_TYPES];
	
	mpcth_t *mpcth = (mpcth_t*)conf_addr;
	
	for (int i = 0; i < NUM_ENTRY_TYPES; i++) {
		mp_entries_count[i] = num_processed[i] = 0;
		mp_entries[i] = 0;
	}
		
	// Do 1 pass to figure out how much space to allocate we need.
	uint16_t num_entries = mpcth->entry_count;
	physaddr_t current_addr = (physaddr_t)(mpcth + 1);
		
	for (int i = 0; i < num_entries; i++) {
		uint8_t current_type = *((uint8_t*)current_addr);
		if (current_type >= NUM_ENTRY_TYPES)
			panic("CORRUPT MPTABLES CONFIGURATION ENTRY");
			
		mp_entries_count[current_type]++;
		current_addr += basetableEntryTypes[current_type].length;
	}
	
	// Allocate the correct space in the mp_entries array
	for (int i = 0; i < NUM_ENTRY_TYPES; i++) {
		mp_entries[i] = kmalloc(mp_entries_count[i] * basetableEntryTypes[i].length , 0);
	}
	
	current_addr = current_addr = (physaddr_t)(mpcth + 1);
	for (int i = 0; i < num_entries; i++) {
		uint8_t current_type = *((uint8_t*)current_addr);
		if (current_type >= NUM_ENTRY_TYPES)
			panic("CORRUPT MPTABLES CONFIGURATION ENTRY.. after we already checked? Huh.");
		
		memcpy(	mp_entries[current_type] + num_processed[current_type] * basetableEntryTypes[current_type].length, 
				(void*)current_addr,  
				basetableEntryTypes[current_type].length);

		num_processed[current_type]++;
		current_addr += basetableEntryTypes[current_type].length;
	}
	
	// We'd do extended table support stuff here (or alter the loop above)
	
	// We now have all of our entries copied into a single structure we can index into. Yay.
}

void proc_parse(proc_entry* entries, uint32_t count) {
	// For now, we don't do anything with the processor entries. Just print them.
	
	for (int i = 0; i < count; i++){
		mptables_dump("Proc entry %u\n", i);
		mptables_dump("-->type: %x\n", entries[i].type);
		mptables_dump("-->apicID: %x\n", entries[i].apicID);
		mptables_dump("-->apicVersion: %x\n", entries[i].apicVersion);
		mptables_dump("-->cpuFlags: %x\n", entries[i].cpuFlags);
		mptables_dump("-->cpuSignaure: %x\n", entries[i].cpuSignature);
		mptables_dump("-->featureFlags: %x\n", entries[i].featureFlags);
	}
	
	mptables_dump("\n");
}

void bus_parse(bus_entry* entries, uint32_t count) {
	// Do we need to sort this?
	// For now, don't. We assume the index into this structure matches the type.
	// This seems to be implied from the configuration
	
	for (int i = 0; i < count; i++){
		if (i != entries[i].busID) 
			panic("Oh noes! We need to sort entries. The MP Spec lied! Ok lied is too strong a word, it implied.");
			
		mptables_dump("Bus entry %u\n", i);
		mptables_dump("-->type: %x\n", entries[i].type);
		mptables_dump("-->BusID: %x\n", entries[i].busID);
		mptables_dump("-->Bus: %c%c%c\n", entries[i].busType[0], entries[i].busType[1], entries[i].busType[2]);
		
		// This is removable. Just here for debugging for now.
		if ((strncmp(entries[i].busType, "PCI", 3) == 0) && (entries[i].busID > max_pci_bus))
			max_pci_bus = entries[i].busID;
		
	}
	
	mptables_dump("\n");
}

void ioapic_parse(ioapic_entry* entries, uint32_t count) {

	// Note: We don't check if the apicFlags is 0. If zero, unusable
	// This should be done elsewhere.
	
	num_ioapics = count;
	
	for (int i = 0; i < count; i++){
		mptables_dump("IOAPIC entry %u\n", i);
		mptables_dump("-->type: %x\n", entries[i].type);
		mptables_dump("-->apicID: %x\n", entries[i].apicID);
		mptables_dump("-->apicVersion: %x\n", entries[i].apicVersion);
		mptables_dump("-->apicFlags: %x\n", entries[i].apicFlags);
		mptables_dump("-->apicAddress: %p\n", entries[i].apicAddress);
		
		if (entries[i].apicID > max_ioapic_id)
			max_ioapic_id = entries[i].apicID;
	}
	mptables_dump("\n");
	

	
	for (int i = 0; i < count; i++) {
		memcpy((void*)(ioapic_entries + entries[i].apicID), (void*)(entries + i), sizeof(ioapic_entry));
	}
}

void int_parse(int_entry* entries, uint32_t count) {
	// create a massive array, tied together with bus/dev, for indexing
	
	for (int i = 0; i < count; i++){
		mptables_dump("Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", entries[i].type);
		mptables_dump("-->intType: %x\n", entries[i].intType);
		mptables_dump("-->intFlags: %x\n", entries[i].intFlags);
		mptables_dump("-->srcBusID: %u\n", entries[i].srcBusID);
		mptables_dump("-->srcDevice: %u (PCI ONLY)\n", (entries[i].srcBusIRQ >> 2) & 0x1F);
		mptables_dump("-->srcBusIRQ: %x\n", entries[i].srcBusIRQ);
		mptables_dump("-->dstApicID: %u\n", entries[i].dstApicID);
		mptables_dump("-->dstApicINT: %u\n", entries[i].dstApicINT);
		
		// Find the max PCI device.
		// removable. here for debugging.
		if (strncmp(((bus_entry*)mp_entries[BUS])[entries[i].srcBusID].busType, "PCI", 3) == 0) {
			
			// Mask out the device number
			int device_num = (entries[i].srcBusIRQ >> 2) & 0x1F;
			if (device_num > max_pci_device)
				max_pci_device= device_num;
		}			
	}
	mptables_dump("\n");

	// Populate the PCI/ISA structure with the interrupt entries.
	for (int i = 0; i < count; i++) {
		if (strncmp(((bus_entry*)mp_entries[BUS])[entries[i].srcBusID].busType, "PCI", 3) == 0) {
			int bus_idx, dev_idx, int_idx;
			bus_idx = entries[i].srcBusID;
			dev_idx = (entries[i].srcBusIRQ >> 2) & 0x1F;
			int_idx = entries[i].srcBusIRQ & 0x3;
			pci_int_groups[bus_idx][dev_idx].intn[int_idx].dstApicID = entries[i].dstApicID;
			pci_int_groups[bus_idx][dev_idx].intn[int_idx].dstApicINT = entries[i].dstApicINT;
		}
		
		if (strncmp(((bus_entry*)mp_entries[BUS])[entries[i].srcBusID].busType, "ISA", 3) == 0) {
			int irq = entries[i].srcBusIRQ;
			int int_type = entries[i].intType;
			
			if (int_type == 3) {
				// THIS IS WHERE THE PIC CONNECTS TO THE IOAPIC
				// WE DON'T CURRENTLY DO ANYTHING WITH THIS, BUT SHOULD WE NEED TO
				// HERED WHERE TO LOOK!
				// WE MUST NOT PLACE THIS INTO OUR TABLE AS IRQ HAS NO REAL MEANING AFAPK
				continue;
				
				// Note. On the dev boxes the pit and pic both claim to be on irq 0
				// However the pit and the pic are on different ioapic entries.
				// Seems odd. Not sure whats up with this. Paul assumes the IRQ has no meaning
				// in regards to the pic... which makes sense.
			}
						
			if ((isa_int_entries[irq].dstApicID != 0xFFFF) && 
				 ((isa_int_entries[irq].dstApicID != entries[i].dstApicID) 
				   || (isa_int_entries[irq].dstApicINT != entries[i].dstApicINT)))
				panic("SAME IRQ MAPS TO DIFFERENT IOAPIC/INTN'S. THIS DEFIES LOGIC.");
			
			isa_int_entries[irq].dstApicID = entries[i].dstApicID;
			isa_int_entries[irq].dstApicINT = entries[i].dstApicINT;
		}			
	}
}

void lint_parse(int_entry* entries, uint32_t count) {
	// For now, we don't do anything with the local interrupt entries
	
	for (int i = 0; i < count; i++){
		mptables_dump("Local Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", entries[i].type);
		mptables_dump("-->intType: %x\n", entries[i].intType);
		mptables_dump("-->srcBusID: %x\n", entries[i].srcBusID);
		mptables_dump("-->srcBusIRQ: %x\n", entries[i].srcBusIRQ);
		mptables_dump("-->dstApicID: %p\n", entries[i].dstApicID);
		mptables_dump("-->dstApicINT: %p\n", entries[i].dstApicINT);
		
	}
}



// Old backup code of how we made it work before. 
// void setup_interrupts() {
// 	
// 	extern handler_t interrupt_handlers[];
// 	
// 	nic_debug("-->Setting interrupts.\n");
// 	
// 	// Enable NIC interrupts
// 	outw(io_base_addr + RL_IM_REG, RL_INTERRUPT_MASK);
// 	
// 	//Clear the current interrupts.
// 	outw(io_base_addr + RL_IS_REG, RL_INTRRUPT_CLEAR);
// 	
// 	// Kernel based interrupt stuff
// 	register_interrupt_handler(interrupt_handlers, KERNEL_IRQ_OFFSET + irq, nic_interrupt_handler, 0);
// 	//pic_unmask_irq(irq); // move this after we setup redirection
// 	//unmask_lapic_lvt(LAPIC_LVT_LINT0);
// 	
// 	// Program the IOAPIC
// 	
// 	uint32_t redirect_low = KERNEL_IRQ_OFFSET + irq;
// 	redirect_low = redirect_low | 0xa000;
// 	uint32_t redirect_high = 0x7000000;
// 
// 	cprintf("Trying table entry....\n");
// 	
// 	uint32_t table_entry = 16;
// 	write_mmreg32(IOAPIC_BASE, 0x10 + 2*table_entry);
// 	write_mmreg32(IOAPIC_BASE + 0x10, redirect_low);
// 	
// 	write_mmreg32(IOAPIC_BASE, 0x10 + 2*table_entry + 1);
// 	write_mmreg32(IOAPIC_BASE + 0x10, redirect_high);
// 	
// 	udelay(1000000);
// 	outb(io_base_addr + 0x38, 0x1);
// 	
// 	udelay(100000000);
// 	
// 	
// /*
// 	udelay(1000000);
// 	uint32_t ka_mp_table = 0;
// 	uint32_t ka_mp_table_base = 0;
// 
// 	for (int i = 0xf0000; i < 0xf0000 + 0x10000; i=i+4)
// 	{
// 		if(read_mmreg32((int)KADDR(i)) == 0x5f504d5f)
// 		{
// 			cprintf("VICTORY! ");
// 			ka_mp_table_base = (int)KADDR(i);
// 			ka_mp_table = (int)KADDR(read_mmreg32((int)KADDR(i+4)));
// 			cprintf("ADDR: %p\n", PADDR(ka_mp_table));
// 			break;
// 		}
// 	}
// 	
// 	uint32_t ka_mp_table_cur_ptr = ka_mp_table;
// 	uint32_t num_entries = read_mmreg32(ka_mp_table_cur_ptr + 0x20) >> 16;
// 	
// 	cprintf("num_entires: %d\n", num_entries);
// 	cprintf("spec_rev: %x\n",   read_mmreg32(ka_mp_table_base + 9) & 0xFF);
// 	cprintf("checksum: %x\n",   read_mmreg32(ka_mp_table_base + 10) & 0xFF);
// 	cprintf("byte 1: %x\n",   read_mmreg32(ka_mp_table_base + 11) & 0xFF);
// 	cprintf("byte 2: %x\n",   read_mmreg32(ka_mp_table_base + 12) & 0xFF);
// 	
// 	
// 	ka_mp_table_cur_ptr = ka_mp_table + 0x2c;
// 	for (int i = 0; i < num_entries; i++) {
// 		
// 		uint32_t low = read_mmreg32(ka_mp_table_cur_ptr);
// 		uint32_t high = read_mmreg32(ka_mp_table_cur_ptr + 4);
// 		uint8_t type = low & 0xFF;
// 		
// 		switch(type) {
// 			case 0:
// 				cprintf("Found Processor Entry\n");
// 				ka_mp_table_cur_ptr += 20;
// 				break;
// 			
// 			case 1:
// 				cprintf("Found Bus Entry\n");
// 
// 				cprintf("-->%c%c%c\n", (char)(low >>16 & 0xFF), (char)(low >> 24 & 0xFF), (char)(high & 0xFF));
// 				cprintf("-->id: %u\n\n", (low >> 8) & 0xFF);
// 				
// 				
// 				ka_mp_table_cur_ptr += 8;
// 				break;
// 			
// 			case 2:
// 				cprintf("Found IOAPIC Entry\n");
// 				cprintf("-->ID: %u\n", (low >> 8) & 0xFF);
// 				cprintf("-->addr: %p\n", high);
// 				
// 				ka_mp_table_cur_ptr += 8;
// 				break;
// 			
// 			case 3:
// 				cprintf("Found IO Interrupt Entry\n");
// 
// 				// only print if we found something hooked to the ioapic
// 				if (((high >> 16) & 0xFF) == 8) {
// 	
// 					cprintf("-->TYPE: %u\n", (low >> 8) & 0xFF);
// 					cprintf("-->FLAGS: %x\n", (low >> 16) & 0xFFFF);
// 					cprintf("-->BUS ID: %u\n", (high) & 0xFF);
// 					cprintf("-->BUS IRQ: %x\n", (high >> 8) & 0xFF);
// 					cprintf("---->SOURCE INT#: %x\n", (high >> 8) & 0x03);
// 					cprintf("---->SOURCE DEV#: %x\n", (high >> 10) & 0x1F);
// 					cprintf("-->DEST APIC ID: %u\n", (high >> 16) & 0xFF);
// 					cprintf("-->DEST APIC ID INITIN: %u\n\n", (high >> 24) & 0xFF);
// 				}
// 			
// 				
// 				ka_mp_table_cur_ptr += 8;
// 				break;
// 			
// 			case 4:
// 				cprintf("Found Local Interrupt Entry\n");
// 				ka_mp_table_cur_ptr += 8;
// 				break;
// 			
// 			default:
// 				cprintf("Unknown type! Danger! Failing out\n");
// 				i = num_entries;
// 		}
// 		
// 	
// 		
// 	}
// */
// /*
// 	// ugly test code for ioapic
// 	udelay(1000000);
// 
// 	cprintf("flooding table\n");
// 	
// 	for (int j = 0; j < 256; j++) {
// 		redirect_low = j << 8;
// 		redirect_low = redirect_low | (KERNEL_IRQ_OFFSET + irq);
// 		cprintf("trying %x\n", j);
// 		
// 		for (int i = 16; i < 17; i++) {
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 			write_mmreg32(IOAPIC_BASE + 0x10, redirect_low);
// 		
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 			write_mmreg32(IOAPIC_BASE + 0x10, redirect_high);
// 		}
// 		
// 		udelay(100000);
// 		
// 		outb(io_base_addr + 0x38, 0x1);
// 		
// 		udelay(100000);
// 	}
// 
// 	udelay(10000000000);	
// */		
// 
// /*
// 	
// 	cprintf("Generating test interrupt....\n");
// 	outb(io_base_addr + 0x38, 0x1);
// 
// 	udelay(1000000);
// 	uint32_t old_low = -1;
// 	uint32_t old_high = -1;
// 	uint32_t new_low = -1;
// 	uint32_t new_high = -1;
// 
// 	for (int i = 0; i <= 24; i++) {
// 		
// 		if (i != 0) {
// 			cprintf("     masking %u with: %x %x\n\n", i-1, old_high, old_low);
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*(i-1));
// 			write_mmreg32(IOAPIC_BASE + 0x10, old_low);
// 			
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*(i-1) + 1);
// 			write_mmreg32(IOAPIC_BASE + 0x10, old_high);
// 		}
// 		
// 		if (i == 24)
// 			break;
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		old_low = read_mmreg32(IOAPIC_BASE + 0x10);
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		old_high = read_mmreg32(IOAPIC_BASE + 0x10);
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		write_mmreg32(IOAPIC_BASE + 0x10, redirect_low);
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		write_mmreg32(IOAPIC_BASE + 0x10, redirect_high);
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		new_low = read_mmreg32(IOAPIC_BASE + 0x10);
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		new_high = read_mmreg32(IOAPIC_BASE + 0x10);	
// 		
// 		//Trigger sw based nic interrupt
// 		cprintf("Trying entry: %u....(WAS: %x %x   NOW: %x %x)\n", i, old_high, old_low, new_high, new_low);
// 		outb(io_base_addr + 0x38, 0x1);
// 		udelay(100000);
// 		outb(io_base_addr + 0x38, 0x1);
// 		udelay(1000000);
// 	}
// */	
// 	
// 	/*
// 	udelay(1000000);
// 	
// 	old_high = -1;
// 	old_low = -1;
// 	
// 	for (int i = 23; i >= -1; i--) {
// 		
// 		if (i != 23) {
// 			cprintf("     masking %u with: %x %x\n\n", i+1, old_high, old_low);
// 			
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*(i + 1));
// 			write_mmreg32(IOAPIC_BASE + 0x10, old_low);
// 			
// 			write_mmreg32(IOAPIC_BASE, 0x10 + 2*(i + 1) + 1);
// 			write_mmreg32(IOAPIC_BASE + 0x10, old_high);
// 
// 		}
// 		
// 		if (i == -1)
// 			break;
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		old_low = read_mmreg32(IOAPIC_BASE + 0x10);
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		old_high = read_mmreg32(IOAPIC_BASE + 0x10);
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		write_mmreg32(IOAPIC_BASE + 0x10, redirect_low);
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		write_mmreg32(IOAPIC_BASE + 0x10, redirect_high);	
// 		
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i);
// 		new_low = read_mmreg32(IOAPIC_BASE + 0x10);
// 		write_mmreg32(IOAPIC_BASE, 0x10 + 2*i + 1);
// 		new_high = read_mmreg32(IOAPIC_BASE + 0x10);	
// 		
// 		//Trigger sw based nic interrupt
// 		cprintf("Trying entry: %u....(WAS: %x %x   NOW: %x %x)\n", i, old_high, old_low, new_high, new_low);
// 		outb(io_base_addr + 0x38, 0x1);
// 		udelay(100000);
// 		outb(io_base_addr + 0x38, 0x1);
// 		udelay(1000000);
// 	}
// 	udelay(1000000); 
// 	
// */	
// 	/*
// 	cprintf("low: %u\nhigh %u\n", redirect_low, redirect_high);
// 
// 	write_mmreg32(IOAPIC_BASE, 0xA0 );
// 	cprintf("IOAPIC Mappings%x\n", read_mmreg32(IOAPIC_BASE + 0x10));*/
// 
// //	cprintf("Core 1 LAPIC ID: %x\n", read_mmreg32(0x0FEE00020));
// 
// 	
// //	panic("WERE ALL GONNA DIE!");
// 
// 	return;
// }
