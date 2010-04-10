/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifdef __SHARC__
#pragma nosharc
#endif
// Not currently sharc complient. However
// we should never be modifying these structures post smp_boot().

#include <arch/ioapic.h>
#include <arch/pci.h>
#include <arch/mptables.h>

#include <ros/common.h>
#include <stdio.h>
#include <string.h>
#include <kmalloc.h>
#include <arch/x86.h>

/** @file
 * @brief Basic MP Tables Parser
 *
 * This file is responsible for locating, checksuming, and parsing the 
 * MultiProcessor Specification Tables.
 *
 * See Intel Multiprocessor Specification for more info
 *
 * @author Paul Pearce <pearce@eecs.berkeley.edu>
 *
 * @todo Extended table support (why?)
 * @todo Expanded error checking?
 * @todo virtaddr_t to match physaddr_t support?
 */

// Important global items
enum interrupt_modes current_interrupt_mode;

proc_entry_t	*COUNT(mp_entries_count[PROC])	 mp_proc_entries = NULL;
bus_entry_t		*COUNT(mp_entries_count[BUS])	 mp_bus_entries = NULL;
ioapic_entry_t	*COUNT(mp_entries_count[IOAPIC]) mp_ioapic_entries = NULL;
int_entry_t		*COUNT(mp_entries_count[INT])	 mp_int_entries = NULL;
int_entry_t		*COUNT(mp_entries_count[LINT])	 mp_lint_entries = NULL; 
// ^ Not a typo. lint entries == int entries, so We just use that.


int mp_entries_count[NUM_ENTRY_TYPES]; // How large each array is.

pci_int_device_t pci_int_devices[PCI_MAX_BUS][PCI_MAX_DEV];
isa_int_entry_t isa_int_entries[NUM_IRQS];
ioapic_entry_t ioapic_entries[IOAPIC_MAX_ID];


/** @brief Entry function to the mptable parser. Calling this function results in the parsing of the tables and setup of all structures
 *
 * This function does the following:
 * 	- Search various locations in memory for the MP Floating Structure 
 *  - Checkum the floating structure to make sure its valid
 *  - Locate the MP Configuration Header, and checksum it
 *  - Locate all entries of type proc, bus, ioapic, int, lint
 *  - Parse the above entries and form the data structures that the rest of the system relies upon
 */
void mptables_parse() {
	
	// Before we do anything. We didn't pack our structs because BSD didnt. Make sure we're sane.
	if (	(sizeof(proc_entry_t)	!= entry_types[PROC].length) || 
			(sizeof(bus_entry_t)	!= entry_types[BUS].length) || 
			(sizeof(ioapic_entry_t)	!= entry_types[IOAPIC].length) || 
			(sizeof(int_entry_t)	!= entry_types[INT].length) || 
			(sizeof(mpfps_t)		!= MPFPS_SIZE) ||
			(sizeof(mpcth_t)		!= MPCTH_SIZE) )
				panic("MPTable structure sizes out of sync with spec");
			
			
	mpfps_t *mpfps;
	
	// Memsets to initalize all our structures to invalid entries
	
	/* Setup the indexable ioapic array.
	 * YOU MUST check the flag field to see if its 0. If 0, unusable.
	 * This is defined by MPTables, and I leaverage this with the memset below to set invalid
	 */
	memset(ioapic_entries, 0, sizeof(ioapic_entries));
	
	// We define an IOAPIC DEST ID of 0xFF (>=256) to be invalid. Pack with all 1's.
	memset(pci_int_devices, 0xFF, sizeof(pci_int_devices));
	memset(isa_int_entries, 0xFF, sizeof(isa_int_entries));
	
	
	mptables_info("Starting MPTables Parsing...\n");
	
	/*  Basic procedure:
	 *	1) Find floating pointer
	 *	2) Go to addr referenced by floating pointer
	 *	3) Read table header info
	 *
	 * We now have to search through 3 address regions searching for a magic string.
	 *
	 *
	 * Note: The pointer can actually be elsewhere. See the FBSD MPTables implimentation for more info
	 * Just doesn't make sense right now to check more places.
	 */
	
	// Search the BIOS ROM Address Space (MOST LIKELY)
	mptables_dump("-->Searching BIOS ROM Area...\n");
	mpfps = find_floating_pointer((physaddr_t)KADDR(BIOS_ROM_BASE), (physaddr_t)KADDR(BIOS_ROM_BOUND));
	
	if (mpfps == NULL) {
		
		/* Search the EBDA UNTESTED, haven't found something that uses this.
		 *
		 * First, we have to find the EBDA Addr.
		 * This USSUALLY works (based on third hand info). May be some cases where it doesnt.
		 * See osdev x86 mem-map for more information
		 */
		physaddr_t ebda_base = READ_FROM_STORED_PHYSADDR32(EBDA_POINTER);
		
		if (ebda_base) {
			ebda_base = ebda_base << 4;
			ebda_base = (physaddr_t)KADDR(ebda_base);
			physaddr_t ebda_bound = ebda_base + EBDA_SIZE - sizeof(mpfps_t);
			
			mptables_dump("-->Searching EBDA...\n");
			mpfps = find_floating_pointer(ebda_base, ebda_bound);
		}
	}

	if (mpfps == NULL) {
		/* Search the last KB of system memory UNTESTED
		 * Note: Will only be there if it not in the EBDA. So this must be called after the EBDA check.
		 * This logic is ripped from mptables without much understanding. No machine to test it on.
		 */
		
		physaddr_t top_of_mem = READ_FROM_STORED_PHYSADDR32(TOPOFMEM_POINTER);
		
		if (top_of_mem) {
			--top_of_mem;
			top_of_mem = top_of_mem * 1024;
			
			top_of_mem = (physaddr_t)KADDR(top_of_mem);
		
	    	mptables_dump("-->Searching top of (real mode) Ram...\n");
			mpfps = find_floating_pointer(top_of_mem, top_of_mem + 1024 - sizeof(mpfps_t));
		}
	}
	
	if (mpfps == NULL) {
		/* Search the last KB of system memory based on a 640K limited, due to CMOS lying
		 * Note: Will only be there if it not in the EBDA. So this must be called after the EBDA check.
		 * This IS tested. Thanks VirtualBox!
		 */
				
		physaddr_t top_of_mem = DEFAULT_TOPOFMEM;
		
		if (top_of_mem) {
				
			top_of_mem = top_of_mem - 1024;
			
			top_of_mem = (physaddr_t)KADDR(top_of_mem);
		
	    	mptables_dump("-->Searching top of (real mode) Ram 640K cap, incase CMOS lied...\n");
			mpfps = find_floating_pointer(top_of_mem, top_of_mem + 1024 - sizeof(mpfps_t));
		}
	}

	/* If we can't find the pointer, it means we are running on a non-mp compliant machine.
	 * This is bad. We can't do interrupts the way we want.
	 * We could have this trigger a MODE in which we operate using the standard PIC, if we really wanted...
	 */
	if (mpfps == NULL) {
		panic("MPTABLES Not found. IOAPIC and interrupts will not function properly. <Insert whale held up by smaller birds here>");
	}
	
	mptables_info("-->MPTables Floating Pointer Structure found @ KVA %08p.\n", mpfps);
	
	mptables_info("-->Current Interrupt Mode: ");
	// Identify our interrupt mode
	if (mpfps->mpfb2 & IMCRP_MASK) {
		current_interrupt_mode = PIC;
		mptables_info("PIC\n");
		// TODO: Do SOMETHING here. We've never found such a system (they are generally ancient). Should we just panic?
	}
	else {
		current_interrupt_mode = VW;
		mptables_info("Virtual Wire\n");
	}
	
	configuration_parse((physaddr_t)KADDR((uint32_t)(mpfps->pap)));
	
	proc_parse();
	bus_parse();
	ioapic_parse();
	int_parse();
	lint_parse();
	
}


/** @brief Take the given memory range and search for the MP Floating Structure
 * 
 * This function will look at every sizeof(mpfps_t) chunch of memory for a given 4byte value (_MP_)
 * until bound is reached (inclusive).
 *
 * Note: Doesn't ensure bounds are sane. This shouldnt be an issue as this function should be priviate to mptables_parse()
 *
 * @param[in] base	The base (kernel virtual) address we start looking at
 * @param[in] bound The bound (inclusive kernel virtual) address we stop looking at
 * 
 * @return MPFPS The virtual address of the base of the floating point structure
 * @return NULL No floating point structure exists in this range
 */
mpfps_t *find_floating_pointer(physaddr_t base, physaddr_t bound) {

	uint32_t count = (bound - base + sizeof(mpfps_t))/sizeof(mpfps_t);

	// This trusted count was authorized with the blessing of Zach.
	// Blame Intel and the MP Spec for making me do this cast.
	mpfps_t* mpfps = (mpfps_t* COUNT(count)) TC(base);

	// Loop over the entire range looking for the signature. The signature is ascii _MP_, which is
	//  stored in the given MP_SIG
	while ( ((physaddr_t)mpfps <= bound) && (READ_FROM_STORED_VIRTADDR32(mpfps->signature) != MP_SIG)) {
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

/** @brief Perform the mptable checksum on the memory given by addr and len
 *
 * This function will take len bytes of memory starting at the (kernel virtual)
 * address addr and sum them together. If the result is 0, the checksum is valid. 
 *
 * @param[in] addr	The base (kernel virtual) address we start checking 
 * @param[in] len 	How many bytes to look at
 * 
 * @return TRUE Valid checksum
 * @return FALSE Invalid checksum
 */
bool checksum(physaddr_t addr, uint32_t len) {
	// MP Table checksums must add up to 0.
	uint8_t checksum = 0;
	
	// Yet another trusted cast. 
	// See comment at start of find_floating_pointer
	uint8_t *addr_p = (uint8_t* COUNT(len)) TC(addr);

	for (int i = 0; i < len; i++)
		checksum += *(addr_p + i);

	return (checksum == 0);
}

/** @brief Parse the configuration MP Table given a valid address to the base of the table
 *
 * This function begin examining a given (kernel virtual) address assuming it is the base 
 * of the configuration table. It will determine the size of the table, and then loop over
 * each entry in the table, loading all entires into the correct corresponding data structures
 *
 * @param[in] conf_addr		The base (kernel virtual) address of the configuration table
 */	
void configuration_parse(physaddr_t conf_addr) {
	
	int num_processed[NUM_ENTRY_TYPES];
	
	// Another. See comment at start of find_floating_pointer
	mpcth_t *mpcth = (mpcth_t* COUNT(1)) TC(conf_addr);
	
	for (int i = 0; i < NUM_ENTRY_TYPES; i++) {
		mp_entries_count[i] = num_processed[i] = 0;
	}
		
	// Do 1 pass to figure out how much space to allocate.
	// Note: Length here means length in bytes. This is from the mp spec.
	uint16_t num_entries = mpcth->entry_count;
	uint16_t mpct_length = mpcth->base_table_length;
	uint16_t entries_length = mpct_length - sizeof(mpcth);
	
	// Now perform a checksum on the configuration table
	if (checksum((physaddr_t)mpcth, mpct_length) == FALSE) {
		panic("FAILED MP CONFIGURATION CHECKSUM.");
	}
	
	uint8_t * COUNT(entries_length) entry_base = (uint8_t* COUNT(entries_length)) TC(mpcth + 1);
	uint8_t * BND(entry_base, entry_base + entries_length) current_addr = entry_base;
	
	for (int i = 0; i < num_entries; i++) {
		uint8_t current_type = *current_addr;
		if (current_type >= NUM_ENTRY_TYPES)
			panic("CORRUPT MPTABLES CONFIGURATION ENTRY");
			
		mp_entries_count[current_type]++;
		current_addr += entry_types[current_type].length;
	}
	
	// Allocate the correct space in the arrays (unrolled for ivy reasons)
	if (mp_entries_count[PROC] != 0) {
		mp_proc_entries = kmalloc(mp_entries_count[PROC] * entry_types[PROC].length , 0);
		if (mp_proc_entries == NULL)
			panic("Failed to allocate space for mp_proc_entires");
	}

	if (mp_entries_count[BUS] != 0) {
		mp_bus_entries = kmalloc(mp_entries_count[BUS] * entry_types[BUS].length , 0);
		if (mp_bus_entries == NULL)
			panic("Failed to allocate space for mp_bus_entires");
	}

	if (mp_entries_count[IOAPIC] != 0) {
		mp_ioapic_entries = kmalloc(mp_entries_count[IOAPIC] * entry_types[IOAPIC].length , 0);
		if (mp_ioapic_entries == NULL)
			panic("Failed to allocate space for mp_ioapic_entires");
	}
	
	if (mp_entries_count[INT] != 0) {
		mp_int_entries = kmalloc(mp_entries_count[INT] * entry_types[INT].length , 0);
		if (mp_int_entries == NULL)
			panic("Failed to allocate space for mp_int_entires");
	}

	if (mp_entries_count[LINT] != 0) {
		mp_lint_entries = kmalloc(mp_entries_count[LINT] * entry_types[LINT].length , 0);
		if (mp_lint_entries == NULL)
			panic("Failed to allocate space for mp_lint_entires");
	}
	
	current_addr = entry_base;
	
	for (int i = 0; i < num_entries; i++) {
		uint8_t current_type = *((uint8_t*)current_addr);
		if (current_type >= NUM_ENTRY_TYPES)
			panic("CORRUPT MPTABLES CONFIGURATION ENTRY.. after we already checked? Huh.");
		
		if (num_processed[current_type] >= mp_entries_count[current_type])
			panic("MPTABLES LIED ABOUT NUMBER OF ENTRIES. NO IDEA WHAT TO DO!");
		
		switch (current_type) {
			case PROC:
				memcpy(	&mp_proc_entries[num_processed[PROC]], 
						current_addr,  
						entry_types[PROC].length);
				break;
			
			case BUS:
				memcpy(	&mp_bus_entries[num_processed[BUS]], 
						current_addr,  
						entry_types[BUS].length);
				break;
			case IOAPIC:
				memcpy(	&mp_ioapic_entries[num_processed[IOAPIC]], 
						// This is needed due to the void* in the entry
						//  no clean way of doing this. Sorry Zach.
						(ioapic_entry_t* COUNT(1)) TC(current_addr),  
						entry_types[IOAPIC].length);
				break;
			case INT:
				memcpy(	&mp_int_entries[num_processed[INT]], 
						current_addr,  
						entry_types[INT].length);
				break;
			case LINT:
				memcpy(	&mp_lint_entries[num_processed[LINT]], 
						(void*)current_addr,  
						entry_types[LINT].length);
				break;
						
			default: panic("UNKNOWN ENTRY TYPE");
		}

		num_processed[current_type]++;
		current_addr += entry_types[current_type].length;
	}
	
	// We'd do extended table support stuff here (or alter the loop above)
	
	// We now have all of our entries copied into a single structure we can index into. Yay.
}

/** @brief Parse all processor mptable entires
 *
 * This function will loop over the raw proc entry structure and parse it into a usable form.
 * This currently just prints stuff if dumping is enabled.
 */
void proc_parse() {
	// For now, we don't do anything with the processor entries. Just print them.
	
	for (int i = 0; i < mp_entries_count[PROC]; i++){
		mptables_dump("Proc entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_proc_entires[i].type);
		mptables_dump("-->apic ID: %x\n", mp_proc_entires[i].apic_id);
		mptables_dump("-->apic Version: %x\n", mp_proc_entires[i].apic_version);
		mptables_dump("-->cpu Flags: %x\n", mp_proc_entires[i].cpu_flags);
		mptables_dump("-->cpu Signaure: %x\n", mp_proc_entires[i].cpu_signature);
		mptables_dump("-->feature Flags: %x\n", mp_proc_entires[i].feature_flags);
	}
	
	mptables_dump("\n");
}

/** @brief Parse all bus mptable entires
 *
 * This function will loop over the raw bus entry structure and parse it into a usable form
 * This currently just prints stuff if dumping is enabled. (With a basic sanity check).
 */
void bus_parse() {
	// Do we need to sort this?
	// For now, don't. We assume the index into this structure matches the type.
	// This seems to be implied from the configuration
	
	for (int i = 0; i < mp_entries_count[BUS]; i++){
		if (i != mp_bus_entries[i].bus_id) 
			panic("Oh noes! We need to sort entries. The MP Spec lied! Ok lied is too strong a word, it implied.");
			
		mptables_dump("Bus entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_bus_entries[i].type);
		mptables_dump("-->Bus ID: %x\n", mp_bus_entries[i].bus_id);
		mptables_dump("-->Bus: %c%c%c\n", mp_bus_entries[i].bus_type[0], mp_bus_entries[i].bus_type[1], mp_bus_entries[i].bus_type[2]);
	
	}
	
	mptables_dump("\n");
}

/** @brief Parse all ioapic mptable entires
 *
 * This function will loop over the raw ioapic entry structure and parse it into a usable form.
 * ioapic_entires[] contains all found ioapics after this function.
 */
void ioapic_parse() {

	// Note: We don't check if the apicFlags is 0. If zero, unusable
	// This should be done elsewhere.
	
	// mp_entries_count[IOAPIC] contains the number of ioapics on this system
	
	for (int i = 0; i < mp_entries_count[IOAPIC]; i++){
		
		memcpy((void*)(ioapic_entries + mp_ioapic_entries[i].apic_id), (void*)(mp_ioapic_entries + i), sizeof(ioapic_entry_t));
		
		mptables_dump("IOAPIC entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_ioapic_entries[i].type);
		mptables_dump("-->apic_id: %x\n", mp_ioapic_entries[i].apic_id);
		mptables_dump("-->apic_version: %x\n", mp_ioapic_entries[i].apic_version);
		mptables_dump("-->apic_flags: %x\n", mp_ioapic_entries[i].apic_flags);
		mptables_dump("-->apic_address: %p\n", mp_ioapic_entries[i].apic_address);
		
	}
	mptables_dump("\n");
}

/** @brief Parse all interrupt mptable entires
 *
 * This function will loop over the raw interrupt entry structure and parse it into a usable form.
 * pci_int_devices[] and isa_int_entries[] will be populated after this function is called.
 */
void int_parse() {
	// create a massive array, tied together with bus/dev, for indexing
	
	for (int i = 0; i < mp_entries_count[INT]; i++){
		mptables_dump("Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_int_entries[i].type);
		mptables_dump("-->int Type: %x\n", mp_int_entries[i].int_type);
		mptables_dump("-->int Flags: %x\n", mp_int_entries[i].int_flags);
		mptables_dump("-->src Bus ID: %u\n", mp_int_entries[i].src_bus_id);
		mptables_dump("-->src Device: %u (PCI ONLY)\n", (mp_int_entries[i].src_bus_irq >> 2) & 0x1F);
		mptables_dump("-->src Bus IRQ: %x\n", mp_int_entries[i].src_bus_irq);
		mptables_dump("-->dst Apic ID: %u\n", mp_int_entries[i].dst_apic_id);
		mptables_dump("-->dst Apic INT: %u\n", mp_int_entries[i].dst_apic_int);
					
	}
	mptables_dump("\n");

	// Populate the PCI/ISA structure with the interrupt entries.
	for (int i = 0; i < mp_entries_count[INT]; i++) {
		if (strncmp(mp_bus_entries[mp_int_entries[i].src_bus_id].bus_type, "PCI", 3) == 0) {
			int bus_idx, dev_idx, line_idx;
			bus_idx = mp_int_entries[i].src_bus_id;
			dev_idx = (mp_int_entries[i].src_bus_irq >> 2) & 0x1F;
			line_idx = mp_int_entries[i].src_bus_irq & 0x3;
			pci_int_devices[bus_idx][dev_idx].line[line_idx].dst_apic_id = mp_int_entries[i].dst_apic_id;
			pci_int_devices[bus_idx][dev_idx].line[line_idx].dst_apic_int = mp_int_entries[i].dst_apic_int;
		}
		
		if (strncmp(mp_bus_entries[mp_int_entries[i].src_bus_id].bus_type, "ISA", 3) == 0) {
			int irq = mp_int_entries[i].src_bus_irq;
			int int_type = mp_int_entries[i].int_type;
			
			if (int_type == 3) {
				/* THIS IS WHERE THE PIC CONNECTS TO THE IOAPIC
				 * WE DON'T CURRENTLY DO ANYTHING WITH THIS, BUT SHOULD WE NEED TO
				 * HERES WHERE TO LOOK!
				 * WE MUST NOT PLACE THIS INTO OUR TABLE AS IRQ HAS NO REAL MEANING AFAPK
				 */
				continue;
				
				/* Note. On the dev boxes the pit and pic both claim to be on irq 0
				 * However the pit and the pic are on different ioapic entries.
				 * Seems odd. Not sure whats up with this. Paul assumes the IRQ has no meaning
				 * in regards to the pic... which makes sense.
				 */
			}
						
			if ((isa_int_entries[irq].dst_apic_id != 0xFFFF) && 
				 ((isa_int_entries[irq].dst_apic_id != mp_int_entries[i].dst_apic_id) 
				   || (isa_int_entries[irq].dst_apic_int != mp_int_entries[i].dst_apic_int)))
				panic("SAME IRQ MAPS TO DIFFERENT IOAPIC/INTN'S. THIS DEFIES LOGIC.");
			
			isa_int_entries[irq].dst_apic_id = mp_int_entries[i].dst_apic_id;
			isa_int_entries[irq].dst_apic_int = mp_int_entries[i].dst_apic_int;
		}			
	}
}

/** @brief Parse all local interrupt mptable entires
 *
 * This function will loop over the raw local interrupt entry structure and parse it into a usable form.
 * This currently just prints stuff if dumping is enabled.
 */

void lint_parse() {
	// For now, we don't do anything with the local interrupt entries
	
	for (int i = 0; i < mp_entries_count[LINT]; i++){
		mptables_dump("Local Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_lint_entries[i].type);
		mptables_dump("-->int Type: %x\n", mp_lint_entries[i].int_type);
		mptables_dump("-->src Bus ID: %x\n", mp_lint_entries[i].src_bus_id);
		mptables_dump("-->src Bus IRQ: %x\n", mp_lint_entries[i].src_bus_irq);
		mptables_dump("-->dst Apic ID: %p\n", mp_lint_entries[i].dst_apic_id);
		mptables_dump("-->dst Apic INT: %p\n", mp_lint_entries[i].dst_apic_int);
		
	}
}
