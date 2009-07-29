#ifndef ROS_INC_MPTABLES_H
#define ROS_INC_MPTABLES_H

#include <arch/types.h>
#include <trap.h>
#include <pmap.h>

#define mptables_info(...)  cprintf(__VA_ARGS__)  
#define mptables_dump(...)  //cprintf(__VA_ARGS__)  


#define MP_SIG			0x5f504d5f	/* _MP_ */

#define BIOS_ROM_BASE	0xf0000
#define BIOS_ROM_BOUND  0xffff0 // Inclusive.

#define EBDA_POINTER	0x040e // Bound is dynamic. In first KB
#define EBDA_SIZE		1024

#define TOPOFMEM_POINTER	0x0413		/* BIOS: base memory size */
#define IMCRP_MASK		0x80

#define NUM_ENTRY_TYPES 5

enum interrupt_modes {
	PIC, // PIC Mode (Bocsh, KVM)
	VW,  // Virtural Wire Mode (Dev Boxes)
	SIO  // Once we fix up the ioapic, shift to this mode, sym io.
};

enum entry_types {
	PROC = 		0,
	BUS  = 		1,
	IOAPIC = 	2,
	INT =		3,
	LINT =		4
};

enum busTypes {
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
} busTypeName;

static busTypeName busTypeTable[] =
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
} tableEntry;

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
    uint8_t		apicID;
    uint8_t		apicVersion;
    uint8_t		cpuFlags;
    uint32_t	cpuSignature;
    uint32_t	featureFlags;
    uint32_t	reserved1;
    uint32_t	reserved2;
} proc_entry;

typedef struct BUSENTRY {
    uint8_t	type;
    uint8_t	busID;
    char	busType[ 6 ];
} bus_entry;

typedef struct IOAPICENTRY {
    uint8_t	type;
    uint8_t	apicID;
    uint8_t	apicVersion;
    uint8_t	apicFlags;
    void*	apicAddress;
} ioapic_entry;

typedef struct INTENTRY {
    uint8_t		type;
    uint8_t		intType;
    uint16_t	intFlags;
    uint8_t		srcBusID;
    uint8_t		srcBusIRQ;
    uint8_t		dstApicID;
    uint8_t		dstApicINT;
} int_entry;

typedef struct PCIINTENTRY {
    uint16_t		dstApicID; // A value of 256 or greater means not valid.
    uint8_t		dstApicINT;
} pci_int_entry;

typedef pci_int_entry isa_int_entry;

typedef struct PCIINTGROUP {
	pci_int_entry intn[4];
} pci_int_group;

void mptables_parse();
mpfps_t *find_floating_pointer(physaddr_t base, physaddr_t bound);
bool checksum(physaddr_t addr, uint32_t len);
void configuration_parse(physaddr_t conf_addr);

void proc_parse(proc_entry* entry, uint32_t count);
void bus_parse(bus_entry* entry, uint32_t count);
void ioapic_parse(ioapic_entry* entry, uint32_t count);
void int_parse(int_entry* entry, uint32_t count);
void lint_parse(int_entry* entry, uint32_t count);

#endif /* !ROS_INC_MPTABLES_H */
