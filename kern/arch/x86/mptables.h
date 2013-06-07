/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_INC_MPTABLES_H
#define ROS_INC_MPTABLES_H

#include <ros/common.h>
#include <pmap.h>

/* 
 * LICENCE NOTE: Most of these structures and some constants
 * were blatently ripped out of BSD with <3. Only the camel 
 * casing has been changed to protect the innocent.
 */

// OBSCENELY IMPORTANT NOTE: None of this is packed. I didn't do it because BSD didnt. This may need to change

#define mptables_info(...)  printk(__VA_ARGS__)  
#define mptables_dump(...)  //printk(__VA_ARGS__)  

// The HEX representation of the ascii string _MP_ we search for
#define MP_SIG				0x5f504d5f	/* _MP_ */

// Base and (inclusive bound) of the BIOS region to check
#define BIOS_ROM_BASE		0xf0000
#define BIOS_ROM_BOUND 		0xffff0 

// Where to look for the EBDA pointer
// Bound is dynamic. In first KB
#define EBDA_POINTER		0x040e 
#define EBDA_SIZE			1024

/* BIOS: base memory size */
#define TOPOFMEM_POINTER	0x0413		
#define IMCRP_MASK			0x80

// Sometimes the BIOS is a lying pain in my ass
// so don't believe it and assume this top of memory and check it
#define DEFAULT_TOPOFMEM	0xa0000

// How many entry types exist? Won't ever change
#define NUM_ENTRY_TYPES 	5

#define INVALID_DEST_APIC 	0xffff

#define MPFPS_SIZE 			16 // For ivy
#define MPCTH_SIZE			44

// Sorry Zach, this has to be done.
#define READ_FROM_STORED_PHYSADDR32(addr)  READ_FROM_STORED_VIRTADDR32(KADDR(addr))
#define READ_FROM_STORED_VIRTADDR32(addr)  *((uint32_t* SAFE)addr)


enum interrupt_modes {
	PIC, // PIC Mode 
	VW,  // Virtural Wire Mode (Dev Boxes)
	SIO  // Once we fix up the ioapic, shift to this mode (not currently used)
};

// Start BSDs lovingly barrowed structs/etc

enum entry_types {
	PROC = 		0,
	BUS  = 		1,
	IOAPIC = 	2,
	INT =		3,
	LINT =		4
};

enum bus_types {
    CBUS = 1,
    CBUSII = 2,
    EISA = 3,
    ISA = 6,
    PCI = 13,
    XPRESS = 18,
    MAX_BUSTYPE = 18,
    UNKNOWN_BUSTYPE = 0xff
};

typedef struct BUSTYPENAME {
    uint8_t	type;
    char	name[ 7 ];
} bus_type_name_t;

static bus_type_name_t bus_type_table[] =
{
    { CBUS,		"CBUS"   },
    { CBUSII,		"CBUSII" },
    { EISA,		"EISA"   },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { ISA,		"ISA"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { PCI,		"PCI"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    }
};



typedef struct TABLE_ENTRY {
    uint8_t	type;
    uint8_t	length;
    char	name[ 32 ];
} table_entry_t;

static table_entry_t entry_types[] =
{
    { 0, 20, "Processor" },
    { 1,  8, "Bus" },
    { 2,  8, "I/O APIC" },
    { 3,  8, "I/O INT" },
    { 4,  8, "Local INT" }
};

/* MP Floating Pointer Structure */
typedef struct MPFPS {
    char	signature[ 4 ];
    void*	pap;
    uint8_t	length;
    uint8_t	spec_rev;
    uint8_t	checksum;
    uint8_t	mpfb1;
    uint8_t	mpfb2;
    uint8_t	mpfb3;
    uint8_t	mpfb4;
    uint8_t	mpfb5;
} mpfps_t;


/* MP Configuration Table Header */
typedef struct MPCTH {
    char	signature[ 4 ];
    uint16_t	base_table_length;
    uint8_t		spec_rev;
    uint8_t		checksum;
    uint8_t		oem_id[ 8 ];
    uint8_t		product_id[ 12 ];
    void*		oem_table_pointer;
    uint16_t	oem_table_size;
    uint16_t	entry_count;
    void*		apic_address;
    uint16_t	extended_table_length;
    uint8_t		extended_table_checksum;
    uint8_t		reserved;
} mpcth_t;

typedef struct PROCENTRY {
    uint8_t		type;
    uint8_t		apic_id;
    uint8_t		apic_version;
    uint8_t		cpu_flags;
    uint32_t	cpu_signature;
    uint32_t	feature_flags;
    uint32_t	reserved1;
    uint32_t	reserved2;
} proc_entry_t;

typedef struct BUSENTRY {
    uint8_t	type;
    uint8_t	bus_id;
    char (NT bus_type)[ 6 ];
} bus_entry_t;

typedef struct IOAPICENTRY {
    uint8_t	type;
    uint8_t	apic_id;
    uint8_t	apic_version;
    uint8_t	apic_flags;
    void*	apic_address;
} ioapic_entry_t;

typedef struct INTENTRY {
    uint8_t		type;
    uint8_t		int_type;
    uint16_t	int_flags;
    uint8_t		src_bus_id;
    uint8_t		src_bus_irq;
    uint8_t		dst_apic_id;
    uint8_t		dst_apic_int;
} int_entry_t;

typedef struct PCIINTENTRY {
    uint16_t	dst_apic_id; // A value of INVALID_DEST_APIC means invalid (>=256)
    uint8_t		dst_apic_int;
} pci_int_entry_t;

typedef pci_int_entry_t isa_int_entry_t;

typedef struct PCIINTDEVICE {
	pci_int_entry_t line[4];
} pci_int_device_t;

// Prototypes
void mptables_parse();
mpfps_t * COUNT(MPFPS_SIZE) find_floating_pointer(physaddr_t base, physaddr_t bound);
bool checksum(physaddr_t addr, uint32_t len);
void configuration_parse(physaddr_t conf_addr);

void proc_parse();
void bus_parse();
void ioapic_parse();
void int_parse();
void lint_parse();

#endif /* !ROS_INC_MPTABLES_H */
