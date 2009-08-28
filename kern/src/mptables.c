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

proc_entry *COUNT(mp_entries_count[PROC]) mp_proc_entries;
bus_entry *COUNT(mp_entries_count[BUS]) mp_bus_entries;
ioapic_entry *COUNT(mp_entries_count[IOAPIC]) mp_ioapic_entries;
int_entry *COUNT(mp_entries_count[INT]) mp_int_entries;
int_entry *COUNT(mp_entries_count[LINT]) mp_lint_entries; // Not a type. lint entries == int entries


int mp_entries_count[NUM_ENTRY_TYPES]; // How large each array is.

pci_int_group pci_int_groups[PCI_MAX_BUS][PCI_MAX_DEV];
isa_int_entry isa_int_entries[NUM_IRQS];
ioapic_entry ioapic_entries[IOAPIC_MAX_ID];

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
	
	if (mpfps == NULL) {
		// Search the last KB of system memory based on a 640K limited, due to CMOS lying
		// Note: Will only be there if it not in the EBDA. So this must be called after the EBDA check.
				
		physaddr_t top_of_mem = DEFAULT_TOPOFMEM;
		
		if (top_of_mem) {
				
			top_of_mem = top_of_mem - 1024;
			
			top_of_mem = (physaddr_t)KADDR(top_of_mem);
		
	    	mptables_dump("-->Searching top of (real mode) Ram 640K cap, incase CMOS lied...\n");
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
	
	proc_parse();
	bus_parse();
	ioapic_parse();
	int_parse();
	lint_parse();
	
}

// Searches the given address range, starting at addr first, to (including) last, for the mptables pointer.
// Does not esure base/bounds are sane.
mpfps_t *find_floating_pointer(physaddr_t base, physaddr_t bound) {

	uint32_t count = (bound - base + sizeof(mpfps_t))/sizeof(mpfps_t);

	// This trusted count was authorized with the blessing of Zach.
	// Blame Intel and the MP Spec for making me do this cast.
	mpfps_t* mpfps = (mpfps_t* COUNT(count)) TC(base);

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
	
	// Yet another trusted cast. 
	// See comment at start of find_floating_pointer
	uint8_t *addr_p = (uint8_t* COUNT(len)) TC(addr);

	for (int i = 0; i < len; i++)
		checksum += *(addr_p + i);

	return (checksum == 0);
}

// Go over the configuration table and parse / load the data into the global variables
// NOTE: This uses kmalloc. Might be cleaner (but less flexable) to allocate fixed amounts
//	set to high numbers.
void configuration_parse(physaddr_t conf_addr) {
	
	int num_processed[NUM_ENTRY_TYPES];
	
	// Another. See comment at start of find_floating_pointer
	mpcth_t *mpcth = (mpcth_t* COUNT(1)) TC(conf_addr);
	
	for (int i = 0; i < NUM_ENTRY_TYPES; i++) {
		mp_entries_count[i] = num_processed[i] = 0;
	}
		
	// Do 1 pass to figure out how much space to allocate.
	uint16_t num_entries = mpcth->entry_count;
	uint16_t mpct_length = mpcth->base_table_length;
	uint16_t entry_length = mpct_length - sizeof(mpcth);
	
	// Now perform a checksum on the configuration table
	if (checksum((physaddr_t)mpcth, mpct_length) == FALSE) {
		panic("FAILED MP CONFIGURATION CHECKSUM.");
	}
	
	uint8_t * COUNT(entry_length) entry_base = (uint8_t* COUNT(entry_length)) TC(mpcth + 1);
	uint8_t * BND(entry_base, entry_base + entry_length) current_addr = entry_base;
	
	for (int i = 0; i < num_entries; i++) {
		uint8_t current_type = *current_addr;
		if (current_type >= NUM_ENTRY_TYPES)
			panic("CORRUPT MPTABLES CONFIGURATION ENTRY");
			
		mp_entries_count[current_type]++;
		current_addr += basetableEntryTypes[current_type].length;
	}
	
	// Allocate the correct space in the arrays (unrolled for ivy reasons)
	if (mp_entries_count[PROC] != 0)
		mp_proc_entries = kmalloc(mp_entries_count[PROC] * basetableEntryTypes[PROC].length , 0);

	if (mp_entries_count[BUS] != 0)
		mp_bus_entries = kmalloc(mp_entries_count[BUS] * basetableEntryTypes[BUS].length , 0);

	if (mp_entries_count[IOAPIC] != 0)
		mp_ioapic_entries = kmalloc(mp_entries_count[IOAPIC] * basetableEntryTypes[IOAPIC].length , 0);
	
	if (mp_entries_count[INT] != 0)
		mp_int_entries = kmalloc(mp_entries_count[INT] * basetableEntryTypes[INT].length , 0);

	if (mp_entries_count[LINT] != 0)
		mp_lint_entries = kmalloc(mp_entries_count[LINT] * basetableEntryTypes[LINT].length , 0);
	
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
						basetableEntryTypes[PROC].length);
				break;
			
			case BUS:
				memcpy(	&mp_bus_entries[num_processed[BUS]], 
						current_addr,  
						basetableEntryTypes[BUS].length);
				break;
			case IOAPIC:
				memcpy(	&mp_ioapic_entries[num_processed[IOAPIC]], 
						// This is needed due to the void* in the entry
						//  no clean way of doing this. Sorry Zach.
						(ioapic_entry* COUNT(1)) TC(current_addr),  
						basetableEntryTypes[IOAPIC].length);
				break;
			case INT:
				memcpy(	&mp_int_entries[num_processed[INT]], 
						current_addr,  
						basetableEntryTypes[INT].length);
				break;
			case LINT:
				memcpy(	&mp_lint_entries[num_processed[LINT]], 
						(void*)current_addr,  
						basetableEntryTypes[LINT].length);
				break;
						
			default: panic("UNKNOWN ENTRY TYPE");
		}

		num_processed[current_type]++;
		current_addr += basetableEntryTypes[current_type].length;
	}
	
	// We'd do extended table support stuff here (or alter the loop above)
	
	// We now have all of our entries copied into a single structure we can index into. Yay.
}

void proc_parse() {
	// For now, we don't do anything with the processor entries. Just print them.
	
	for (int i = 0; i < mp_entries_count[PROC]; i++){
		mptables_dump("Proc entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_proc_entires[i].type);
		mptables_dump("-->apicID: %x\n", mp_proc_entires[i].apicID);
		mptables_dump("-->apicVersion: %x\n", mp_proc_entires[i].apicVersion);
		mptables_dump("-->cpuFlags: %x\n", mp_proc_entires[i].cpuFlags);
		mptables_dump("-->cpuSignaure: %x\n", mp_proc_entires[i].cpuSignature);
		mptables_dump("-->featureFlags: %x\n", mp_proc_entires[i].featureFlags);
	}
	
	mptables_dump("\n");
}

void bus_parse() {
	// Do we need to sort this?
	// For now, don't. We assume the index into this structure matches the type.
	// This seems to be implied from the configuration
	
	for (int i = 0; i < mp_entries_count[BUS]; i++){
		if (i != mp_bus_entries[i].busID) 
			panic("Oh noes! We need to sort entries. The MP Spec lied! Ok lied is too strong a word, it implied.");
			
		mptables_dump("Bus entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_bus_entries[i].type);
		mptables_dump("-->BusID: %x\n", mp_bus_entries[i].busID);
		mptables_dump("-->Bus: %c%c%c\n", mp_bus_entries[i].busType[0], mp_bus_entries[i].busType[1], mp_bus_entries[i].busType[2]);
	
	}
	
	mptables_dump("\n");
}

void ioapic_parse() {

	// Note: We don't check if the apicFlags is 0. If zero, unusable
	// This should be done elsewhere.
	
	// mp_entries_count[IOAPIC] contains the number of ioapics on this system
	
	for (int i = 0; i < mp_entries_count[IOAPIC]; i++){
		mptables_dump("IOAPIC entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_ioapic_entries[i].type);
		mptables_dump("-->apicID: %x\n", mp_ioapic_entries[i].apicID);
		mptables_dump("-->apicVersion: %x\n", mp_ioapic_entries[i].apicVersion);
		mptables_dump("-->apicFlags: %x\n", mp_ioapic_entries[i].apicFlags);
		mptables_dump("-->apicAddress: %p\n", mp_ioapic_entries[i].apicAddress);
		
	}
	mptables_dump("\n");
	
	for (int i = 0; i < mp_entries_count[IOAPIC]; i++) {
		memcpy((void*)(ioapic_entries + mp_ioapic_entries[i].apicID), (void*)(mp_ioapic_entries + i), sizeof(ioapic_entry));
	}
}

void int_parse() {
	// create a massive array, tied together with bus/dev, for indexing
	
	for (int i = 0; i < mp_entries_count[INT]; i++){
		mptables_dump("Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_int_entries[i].type);
		mptables_dump("-->intType: %x\n", mp_int_entries[i].intType);
		mptables_dump("-->intFlags: %x\n", mp_int_entries[i].intFlags);
		mptables_dump("-->srcBusID: %u\n", mp_int_entries[i].srcBusID);
		mptables_dump("-->srcDevice: %u (PCI ONLY)\n", (mp_int_entries[i].srcBusIRQ >> 2) & 0x1F);
		mptables_dump("-->srcBusIRQ: %x\n", mp_int_entries[i].srcBusIRQ);
		mptables_dump("-->dstApicID: %u\n", mp_int_entries[i].dstApicID);
		mptables_dump("-->dstApicINT: %u\n", mp_int_entries[i].dstApicINT);
					
	}
	mptables_dump("\n");

	// Populate the PCI/ISA structure with the interrupt entries.
	for (int i = 0; i < mp_entries_count[INT]; i++) {
		if (strncmp(mp_bus_entries[mp_int_entries[i].srcBusID].busType, "PCI", 3) == 0) {
			int bus_idx, dev_idx, int_idx;
			bus_idx = mp_int_entries[i].srcBusID;
			dev_idx = (mp_int_entries[i].srcBusIRQ >> 2) & 0x1F;
			int_idx = mp_int_entries[i].srcBusIRQ & 0x3;
			pci_int_groups[bus_idx][dev_idx].intn[int_idx].dstApicID = mp_int_entries[i].dstApicID;
			pci_int_groups[bus_idx][dev_idx].intn[int_idx].dstApicINT = mp_int_entries[i].dstApicINT;
		}
		
		if (strncmp(mp_bus_entries[mp_int_entries[i].srcBusID].busType, "ISA", 3) == 0) {
			int irq = mp_int_entries[i].srcBusIRQ;
			int int_type = mp_int_entries[i].intType;
			
			if (int_type == 3) {
				// THIS IS WHERE THE PIC CONNECTS TO THE IOAPIC
				// WE DON'T CURRENTLY DO ANYTHING WITH THIS, BUT SHOULD WE NEED TO
				// HERES WHERE TO LOOK!
				// WE MUST NOT PLACE THIS INTO OUR TABLE AS IRQ HAS NO REAL MEANING AFAPK
				continue;
				
				// Note. On the dev boxes the pit and pic both claim to be on irq 0
				// However the pit and the pic are on different ioapic entries.
				// Seems odd. Not sure whats up with this. Paul assumes the IRQ has no meaning
				// in regards to the pic... which makes sense.
			}
						
			if ((isa_int_entries[irq].dstApicID != 0xFFFF) && 
				 ((isa_int_entries[irq].dstApicID != mp_int_entries[i].dstApicID) 
				   || (isa_int_entries[irq].dstApicINT != mp_int_entries[i].dstApicINT)))
				panic("SAME IRQ MAPS TO DIFFERENT IOAPIC/INTN'S. THIS DEFIES LOGIC.");
			
			isa_int_entries[irq].dstApicID = mp_int_entries[i].dstApicID;
			isa_int_entries[irq].dstApicINT = mp_int_entries[i].dstApicINT;
		}			
	}
}

void lint_parse() {
	// For now, we don't do anything with the local interrupt entries
	
	for (int i = 0; i < mp_entries_count[LINT]; i++){
		mptables_dump("Local Interrupt entry %u\n", i);
		mptables_dump("-->type: %x\n", mp_lint_entries[i].type);
		mptables_dump("-->intType: %x\n", mp_lint_entries[i].intType);
		mptables_dump("-->srcBusID: %x\n", mp_lint_entries[i].srcBusID);
		mptables_dump("-->srcBusIRQ: %x\n", mp_lint_entries[i].srcBusIRQ);
		mptables_dump("-->dstApicID: %p\n", mp_lint_entries[i].dstApicID);
		mptables_dump("-->dstApicINT: %p\n", mp_lint_entries[i].dstApicINT);
		
	}
}
