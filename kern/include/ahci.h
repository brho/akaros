/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * advanced host controller interface (sata)
 * © 2007  coraid, inc
 */

/* ata errors */
enum {
	Emed = 1 << 0,  /* media error */
	Enm = 1 << 1,   /* no media */
	Eabrt = 1 << 2, /* abort */
	Emcr = 1 << 3,  /* media change request */
	Eidnf = 1 << 4, /* no user-accessible address */
	Emc = 1 << 5,   /* media change */
	Eunc = 1 << 6,  /* data error */
	Ewp = 1 << 6,   /* write protect */
	Eicrc = 1 << 7, /* interface crc error */

	Efatal = Eidnf | Eicrc, /* must sw reset */
};

/* ata status */
enum {
	ASerr = 1 << 0,  /* error */
	ASdrq = 1 << 3,  /* request */
	ASdf = 1 << 5,   /* fault, command specific */
	ASdrdy = 1 << 6, /* ready, command specific */
	ASbsy = 1 << 7,  /* busy */

	ASobs = 1 << 1 | 1 << 2 | 1 << 4,
};

/* pci configuration */
enum {
	Abar = 5,
};

/*
 * ahci memory configuration
 *
 * 0000-0023	generic host control
 * 0024-009f	reserved
 * 00a0-00ff	vendor specific.
 * 0100-017f	port 0
 * ...
 * 1080-1100	port 31
 */

/* Capability bits: supported features */
enum {
	Hs64a = 1 << 31,  /* 64-bit addressing */
	Hsncq = 1 << 30,  /* ncq */
	Hssntf = 1 << 29, /* snotification reg. */
	Hsmps = 1 << 28,  /* mech pres switch */
	Hsss = 1 << 27,   /* staggered spinup */
	Hsalp = 1 << 26,  /* aggressive link pm */
	Hsal = 1 << 25,   /* activity led */
	Hsclo = 1 << 24,  /* command-list override */
	Hiss = 1 << 20,   /* for interface speed */
	                  //	Hsnzo	= 1<<19,
	Hsam = 1 << 18,   /* ahci-mode only */
	Hspm = 1 << 17,   /* port multiplier */
	                  //	Hfbss	= 1<<16,
	Hpmb = 1 << 15,   /* multiple-block pio */
	Hssc = 1 << 14,   /* slumber state */
	Hpsc = 1 << 13,   /* partial-slumber state */
	Hncs = 1 << 8,    /* n command slots */
	Hcccs = 1 << 7,   /* coal */
	Hems = 1 << 6,    /* enclosure mgmt. */
	Hsxs = 1 << 5,    /* external sata */
	Hnp = 1 << 0,     /* n ports */
};

/* GHC bits */
enum {
	Hae = 1 << 31, /* enable ahci */
	Hie = 1 << 1,  /* " interrupts */
	Hhr = 1 << 0,  /* hba reset */
};

#define HBA_CAP 0x00       // Host Capabilities
#define HBA_GHC 0x04       // Global Host Control
#define HBA_ISR 0x08       // Interrupt Status Register
#define HBA_PI  0x0C       // Ports Implemented
#define HBA_VS 0x10        // Version
#define HBA_CCC_CTL 0x14   // Command Completion Coalescing Control
#define HBA_CCC_PORTS 0x18 // Command Completion Coalescing Ports
#define HBA_EM_LOC 0x1C    // Enclosure Management Location
#define HBA_EM_CTL 0x20    // Enclosure Management Control
#define HBA_CAP2 0x24      // Host Capabilities Extended
#define HBA_BOHC 0x28      // BIOS/OS Hand-Off Control and Status

/* Interrupt Status bits */
enum {
	Acpds = 1 << 31, /* cold port detect status */
	Atfes = 1 << 30, /* task file error status */
	Ahbfs = 1 << 29, /* hba fatal */
	Ahbds = 1 << 28, /* hba error (parity error) */
	Aifs = 1 << 27,  /* interface fatal  §6.1.2 */
	Ainfs = 1 << 26, /* interface error (recovered) */
	Aofs = 1 << 24,  /* too many bytes from disk */
	Aipms = 1 << 23, /* incorrect prt mul status */
	Aprcs = 1 << 22, /* PhyRdy change status Pxserr.diag.n */
	Adpms = 1 << 7,  /* mechanical presence status */
	Apcs = 1 << 6,   /* port connect  diag.x */
	Adps = 1 << 5,   /* descriptor processed */
	Aufs = 1 << 4,   /* unknown fis diag.f */
	Asdbs = 1 << 3,  /* set device bits fis received w/ i bit set */
	Adss = 1 << 2,   /* dma setup */
	Apio = 1 << 1,   /* pio setup fis */
	Adhrs = 1 << 0,  /* device to host register fis */

	IEM = Acpds | Atfes | Ahbds | Ahbfs | Ahbds | Aifs | Ainfs | Aprcs | Apcs |
	      Adps | Aufs | Asdbs | Adss | Adhrs,
	Ifatal = Atfes | Ahbfs | Ahbds | Aifs,
};

/* SError bits */
enum {
	SerrX = 1 << 26, /* exchanged */
	SerrF = 1 << 25, /* unknown fis */
	SerrT = 1 << 24, /* transition error */
	SerrS = 1 << 23, /* link sequence */
	SerrH = 1 << 22, /* handshake */
	SerrC = 1 << 21, /* crc */
	SerrD = 1 << 20, /* not used by ahci */
	SerrB = 1 << 19, /* 10-tp-8 decode */
	SerrW = 1 << 18, /* comm wake */
	SerrI = 1 << 17, /* phy internal */
	SerrN = 1 << 16, /* phyrdy change */

	ErrE = 1 << 11, /* internal */
	ErrP = 1 << 10, /* ata protocol violation */
	ErrC = 1 << 9,  /* communication */
	ErrT = 1 << 8,  /* transient */
	ErrM = 1 << 1,  /* recoverd comm */
	ErrI = 1 << 0,  /* recovered data integrety */

	ErrAll = ErrE | ErrP | ErrC | ErrT | ErrM | ErrI,
	SerrAll = SerrX | SerrF | SerrT | SerrS | SerrH | SerrC | SerrD | SerrB |
	          SerrW | SerrI | SerrN | ErrAll,
	SerrBad = 0x7f << 19,
};

/* Command/Status register bits */
enum {
	Aicc = 1 << 28,   /* interface communcations control. 4 bits */
	Aasp = 1 << 27,   /* aggressive slumber & partial sleep */
	Aalpe = 1 << 26,  /* aggressive link pm enable */
	Adlae = 1 << 25,  /* drive led on atapi */
	Aatapi = 1 << 24, /* device is atapi */
	Aesp = 1 << 21,   /* external sata port */
	Acpd = 1 << 20,   /* cold presence detect */
	Ampsp = 1 << 19,  /* mechanical pres. */
	Ahpcp = 1 << 18,  /* hot plug capable */
	Apma = 1 << 17,   /* pm attached */
	Acps = 1 << 16,   /* cold presence state */
	Acr = 1 << 15,    /* cmdlist running */
	Afr = 1 << 14,    /* fis running */
	Ampss = 1 << 13,  /* mechanical presence switch state */
	Accs = 1 << 8,    /* current command slot 12:08 */
	Afre = 1 << 4,    /* fis enable receive */
	Aclo = 1 << 3,    /* command list override */
	Apod = 1 << 2,    /* power on dev (requires cold-pres. detect) */
	Asud = 1 << 1,    /* spin-up device;  requires ss capability */
	Ast = 1 << 0,     /* start */

	Arun = Ast | Acr | Afre | Afr,
};

/* SControl register bits */
enum {
	Aipm = 1 << 8, /* interface power mgmt. 3=off */
	Aspd = 1 << 4,
	Adis = 1 << 2, // Disable SATA interface and put Phy in offline mode
	Adet = 1 << 0, /* device detection */
};

#define PORT_CLB    0x00 // Port Command List Base address
#define PORT_CLBU   0x04 // Port Command List Base address Upper 32-bits
#define PORT_FB     0x08 // Port FIS Base address
#define PORT_FBU    0x0C // Port FIS Base address Upper 32-bits
#define PORT_IS     0x10 // Port Interrupt Status
#define PORT_IE     0x14 // Port Interrupt Enable
#define PORT_CMD    0x18 // Port Command and status
#define PORT_RES1   0x1C // Reserved
#define PORT_TFD    0x20 // Port Task File Data
#define PORT_SIG    0x24 // Port Signature
#define PORT_SSTS   0x28 // Port Serial ATA Status (SCR0: SStatus)
#define PORT_SCTL   0x2C // Port Serial ATA Control (SCR2: SControl)
#define PORT_SERR   0x30 // Port Serial ATA Error (SCR1: SError)
#define PORT_SACT   0x34 // Port Serial ATA Active (SCR3: SActive)
#define PORT_CI     0x38 // Port Command Issue
#define PORT_SNTF   0x3C // Port Serial ATA Notification (SCR4: SNotification)
#define PORT_FBS    0x40 // Port FIS-Based Switching control
#define PORT_DEVSLP 0x44 // Port Device Sleep
#define PORT_RES2   0x48 // Reserved
#define PORT_VS     0x70 // Vendor Specific

enum {
	/*
     * Aport sstatus bits (actually states):
     * 11-8 interface power management
     *  7-4 current interface speed (generation #)
     *  3-0 device detection
     */
	Intslumber = 0x600,
	Intpartpwr = 0x200,
	Intactive = 0x100,
	Intpm = 0xf00,

	Devphyoffline = 4,
	Devphycomm = 2, /* phy communication established */
	Devpresent = 1,
	Devdet = Devpresent | Devphycomm | Devphyoffline,
};

/* in host's memory; not memory mapped */
struct afis {
	unsigned char *base;
	unsigned char *d;
	unsigned char *p;
	unsigned char *r;
	unsigned char *u;
	uint32_t *devicebits;
};

// Command header flags
enum {
	Lprdtl = 1 << 16, /* physical region descriptor table len */
	Lpmp = 1 << 12,   /* port multiplier port */
	Lclear = 1 << 10, /* clear busy on R_OK */
	Lbist = 1 << 9,
	Lreset = 1 << 8,
	Lpref = 1 << 7, /* prefetchable */
	Lwrite = 1 << 6,
	Latapi = 1 << 5,
	Lcfl = 1 << 0, /* command fis length in double words */
};

// AHCI Command List Command Header
// Each header is an element in the list which is up to 32 elements long
#define ALIST_SIZE 0x20   // Size of the struct in memory, not for access
#define ALIST_FLAGS  0x00 // Flags and PRDTL (PRDT Length)
#define ALIST_LEN    0x04 // PRD byte count transferred
#define ALIST_CTAB   0x08 // Physical address of 128-bit aligned Command Table
#define ALIST_CTABHI 0x0C // CTAB physical address upper 32 bits
#define ALIST_RES    0x10 // Reserved

// AHCI Physical Region Descriptor Table Element
// Each of these elements is part of a table with up to 65,535 entries
#define APRDT_SIZE 0x10  // Size of the struct in memory, not for access
#define APRDT_DBA   0x00 // Data Base Address (physical)
#define APRDT_DBAHI 0x04 // Data Base Address upper 32 bits
#define APRDT_RES   0x08 // Reserved
#define APRDT_COUNT 0x0C // 31=Intr on Completion, 30:22=Reserved, 21:0=DBC

// AHCI Command Table
// Note that there is no fixed size specified - there are 1 to 65,535 PRDT's
// Size = ACTAB_PRDT + APRDT*N_APRDT
#define ACTAB_CFIS  0x00 // Command Frame Information Struct (up to 64 bytes)
#define ACTAB_ATAPI 0x40 // ATAPI Command (12 or 16 bytes)
#define ACTAB_RES   0x50 // Reserved
#define ACTAB_PRDT  0x80 // PRDT (up to 65,535 entries in spec, this has one)

// Portm flags (status flags?)
enum {
	Ferror = 1,
	Fdone = 2,
};

// Portm feature flags
enum {
	Dllba = 1,
	Dsmart = 1 << 1,
	Dpower = 1 << 2,
	Dnop = 1 << 3,
	Datapi = 1 << 4,
	Datapi16 = 1 << 5,
};

struct aportm {
	qlock_t ql;
	struct rendez Rendez;
	unsigned char flag;
	unsigned char feat;
	unsigned char smart;
	struct afis fis;
	void *list;
	void *ctab;
};

struct aportc {
	void *p;
	struct aportm *pm;
};
