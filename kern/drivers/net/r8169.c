/*
 * r8169.c: RealTek 8169/8168/8101 ethernet driver.
 *
 * Copyright (c) 2002 ShuChen <shuchen@realtek.com.tw>
 * Copyright (c) 2003 - 2007 Francois Romieu <romieu@fr.zoreil.com>
 * Copyright (c) a lot of people too. Please respect their work.
 *
 * See MAINTAINERS file for support contact information.
 */

#include <linux_compat.h>
#include "linux_mii.h"
#include <net/tcp.h>

#define RTL8169_VERSION "2.3LK-NAPI"
#define MODULENAME "r8169"
#define PFX MODULENAME ": "

#define FIRMWARE_8168D_1	"rtl_nic/rtl8168d-1.fw"
#define FIRMWARE_8168D_2	"rtl_nic/rtl8168d-2.fw"
#define FIRMWARE_8168E_1	"rtl_nic/rtl8168e-1.fw"
#define FIRMWARE_8168E_2	"rtl_nic/rtl8168e-2.fw"
#define FIRMWARE_8168E_3	"rtl_nic/rtl8168e-3.fw"
#define FIRMWARE_8168F_1	"rtl_nic/rtl8168f-1.fw"
#define FIRMWARE_8168F_2	"rtl_nic/rtl8168f-2.fw"
#define FIRMWARE_8105E_1	"rtl_nic/rtl8105e-1.fw"
#define FIRMWARE_8402_1		"rtl_nic/rtl8402-1.fw"
#define FIRMWARE_8411_1		"rtl_nic/rtl8411-1.fw"
#define FIRMWARE_8411_2		"rtl_nic/rtl8411-2.fw"
#define FIRMWARE_8106E_1	"rtl_nic/rtl8106e-1.fw"
#define FIRMWARE_8106E_2	"rtl_nic/rtl8106e-2.fw"
#define FIRMWARE_8168G_2	"rtl_nic/rtl8168g-2.fw"
#define FIRMWARE_8168G_3	"rtl_nic/rtl8168g-3.fw"
#define FIRMWARE_8168H_1	"rtl_nic/rtl8168h-1.fw"
#define FIRMWARE_8168H_2	"rtl_nic/rtl8168h-2.fw"
#define FIRMWARE_8107E_1	"rtl_nic/rtl8107e-1.fw"
#define FIRMWARE_8107E_2	"rtl_nic/rtl8107e-2.fw"

#ifdef RTL8169_DEBUG
#define dprintk(fmt, args...) \
	do { printk(KERN_DEBUG PFX fmt, ## args); } while (0)
#else
#define dprintk(fmt, args...)	do {} while (0)
#endif /* RTL8169_DEBUG */

#define R8169_MSG_DEFAULT \
	(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN)

#define TX_SLOTS_AVAIL(tp) \
	(tp->dirty_tx + NUM_TX_DESC - tp->cur_tx)

/* A skbuff with nr_frags needs nr_frags+1 entries in the tx queue */
#define TX_FRAGS_READY_FOR(tp,nr_frags) \
	(TX_SLOTS_AVAIL(tp) >= (nr_frags + 1))

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC. */
static const int multicast_filter_limit = 32;

#define MAX_READ_REQUEST_SHIFT	12
#define TX_DMA_BURST	7	/* Maximum PCI burst, '7' is unlimited */
#define InterFrameGap	0x03	/* 3 means InterFrameGap = the shortest one */

#define R8169_REGS_SIZE		256
#define R8169_NAPI_WEIGHT	64
#define NUM_TX_DESC	64	/* Number of Tx descriptor registers */
#define NUM_RX_DESC	256U	/* Number of Rx descriptor registers */
#define R8169_TX_RING_BYTES	(NUM_TX_DESC * sizeof(struct TxDesc))
#define R8169_RX_RING_BYTES	(NUM_RX_DESC * sizeof(struct RxDesc))

#define RTL8169_TX_TIMEOUT	(6*HZ)
#define RTL8169_PHY_TIMEOUT	(10*HZ)

/* write/read MMIO register */
#define RTL_W8(reg, val8)	write8 ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)	write16 ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)	write32 ((val32), ioaddr + (reg))
#define RTL_R8(reg)		read8 (ioaddr + (reg))
#define RTL_R16(reg)		read16 (ioaddr + (reg))
#define RTL_R32(reg)		read32 (ioaddr + (reg))

enum mac_version {
	RTL_GIGA_MAC_VER_01 = 0,
	RTL_GIGA_MAC_VER_02,
	RTL_GIGA_MAC_VER_03,
	RTL_GIGA_MAC_VER_04,
	RTL_GIGA_MAC_VER_05,
	RTL_GIGA_MAC_VER_06,
	RTL_GIGA_MAC_VER_07,
	RTL_GIGA_MAC_VER_08,
	RTL_GIGA_MAC_VER_09,
	RTL_GIGA_MAC_VER_10,
	RTL_GIGA_MAC_VER_11,
	RTL_GIGA_MAC_VER_12,
	RTL_GIGA_MAC_VER_13,
	RTL_GIGA_MAC_VER_14,
	RTL_GIGA_MAC_VER_15,
	RTL_GIGA_MAC_VER_16,
	RTL_GIGA_MAC_VER_17,
	RTL_GIGA_MAC_VER_18,
	RTL_GIGA_MAC_VER_19,
	RTL_GIGA_MAC_VER_20,
	RTL_GIGA_MAC_VER_21,
	RTL_GIGA_MAC_VER_22,
	RTL_GIGA_MAC_VER_23,
	RTL_GIGA_MAC_VER_24,
	RTL_GIGA_MAC_VER_25,
	RTL_GIGA_MAC_VER_26,
	RTL_GIGA_MAC_VER_27,
	RTL_GIGA_MAC_VER_28,
	RTL_GIGA_MAC_VER_29,
	RTL_GIGA_MAC_VER_30,
	RTL_GIGA_MAC_VER_31,
	RTL_GIGA_MAC_VER_32,
	RTL_GIGA_MAC_VER_33,
	RTL_GIGA_MAC_VER_34,
	RTL_GIGA_MAC_VER_35,
	RTL_GIGA_MAC_VER_36,
	RTL_GIGA_MAC_VER_37,
	RTL_GIGA_MAC_VER_38,
	RTL_GIGA_MAC_VER_39,
	RTL_GIGA_MAC_VER_40,
	RTL_GIGA_MAC_VER_41,
	RTL_GIGA_MAC_VER_42,
	RTL_GIGA_MAC_VER_43,
	RTL_GIGA_MAC_VER_44,
	RTL_GIGA_MAC_VER_45,
	RTL_GIGA_MAC_VER_46,
	RTL_GIGA_MAC_VER_47,
	RTL_GIGA_MAC_VER_48,
	RTL_GIGA_MAC_VER_49,
	RTL_GIGA_MAC_VER_50,
	RTL_GIGA_MAC_VER_51,
	RTL_GIGA_MAC_NONE   = 0xff,
};

enum rtl_tx_desc_version {
	RTL_TD_0	= 0,
	RTL_TD_1	= 1,
};

#define JUMBO_1K	ETHERMAXTU
#define JUMBO_4K	(4*1024 - ETHERHDRSIZE - 2)
#define JUMBO_6K	(6*1024 - ETHERHDRSIZE - 2)
#define JUMBO_7K	(7*1024 - ETHERHDRSIZE - 2)
#define JUMBO_9K	(9*1024 - ETHERHDRSIZE - 2)

#define _R(NAME,TD,FW,SZ,B) {	\
	.name = NAME,		\
	.txd_version = TD,	\
	.fw_name = FW,		\
	.jumbo_max = SZ,	\
	.jumbo_tx_csum = B	\
}

static const struct {
	const char *name;
	enum rtl_tx_desc_version txd_version;
	const char *fw_name;
	uint16_t jumbo_max;
	bool jumbo_tx_csum;
} rtl_chip_infos[] = {
	/* PCI devices. */
	[RTL_GIGA_MAC_VER_01] =
		_R("RTL8169",		RTL_TD_0, NULL, JUMBO_7K, true),
	[RTL_GIGA_MAC_VER_02] =
		_R("RTL8169s",		RTL_TD_0, NULL, JUMBO_7K, true),
	[RTL_GIGA_MAC_VER_03] =
		_R("RTL8110s",		RTL_TD_0, NULL, JUMBO_7K, true),
	[RTL_GIGA_MAC_VER_04] =
		_R("RTL8169sb/8110sb",	RTL_TD_0, NULL, JUMBO_7K, true),
	[RTL_GIGA_MAC_VER_05] =
		_R("RTL8169sc/8110sc",	RTL_TD_0, NULL, JUMBO_7K, true),
	[RTL_GIGA_MAC_VER_06] =
		_R("RTL8169sc/8110sc",	RTL_TD_0, NULL, JUMBO_7K, true),
	/* PCI-E devices. */
	[RTL_GIGA_MAC_VER_07] =
		_R("RTL8102e",		RTL_TD_1, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_08] =
		_R("RTL8102e",		RTL_TD_1, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_09] =
		_R("RTL8102e",		RTL_TD_1, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_10] =
		_R("RTL8101e",		RTL_TD_0, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_11] =
		_R("RTL8168b/8111b",	RTL_TD_0, NULL, JUMBO_4K, false),
	[RTL_GIGA_MAC_VER_12] =
		_R("RTL8168b/8111b",	RTL_TD_0, NULL, JUMBO_4K, false),
	[RTL_GIGA_MAC_VER_13] =
		_R("RTL8101e",		RTL_TD_0, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_14] =
		_R("RTL8100e",		RTL_TD_0, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_15] =
		_R("RTL8100e",		RTL_TD_0, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_16] =
		_R("RTL8101e",		RTL_TD_0, NULL, JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_17] =
		_R("RTL8168b/8111b",	RTL_TD_0, NULL, JUMBO_4K, false),
	[RTL_GIGA_MAC_VER_18] =
		_R("RTL8168cp/8111cp",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_19] =
		_R("RTL8168c/8111c",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_20] =
		_R("RTL8168c/8111c",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_21] =
		_R("RTL8168c/8111c",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_22] =
		_R("RTL8168c/8111c",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_23] =
		_R("RTL8168cp/8111cp",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_24] =
		_R("RTL8168cp/8111cp",	RTL_TD_1, NULL, JUMBO_6K, false),
	[RTL_GIGA_MAC_VER_25] =
		_R("RTL8168d/8111d",	RTL_TD_1, FIRMWARE_8168D_1,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_26] =
		_R("RTL8168d/8111d",	RTL_TD_1, FIRMWARE_8168D_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_27] =
		_R("RTL8168dp/8111dp",	RTL_TD_1, NULL, JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_28] =
		_R("RTL8168dp/8111dp",	RTL_TD_1, NULL, JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_29] =
		_R("RTL8105e",		RTL_TD_1, FIRMWARE_8105E_1,
							JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_30] =
		_R("RTL8105e",		RTL_TD_1, FIRMWARE_8105E_1,
							JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_31] =
		_R("RTL8168dp/8111dp",	RTL_TD_1, NULL, JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_32] =
		_R("RTL8168e/8111e",	RTL_TD_1, FIRMWARE_8168E_1,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_33] =
		_R("RTL8168e/8111e",	RTL_TD_1, FIRMWARE_8168E_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_34] =
		_R("RTL8168evl/8111evl",RTL_TD_1, FIRMWARE_8168E_3,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_35] =
		_R("RTL8168f/8111f",	RTL_TD_1, FIRMWARE_8168F_1,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_36] =
		_R("RTL8168f/8111f",	RTL_TD_1, FIRMWARE_8168F_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_37] =
		_R("RTL8402",		RTL_TD_1, FIRMWARE_8402_1,
							JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_38] =
		_R("RTL8411",		RTL_TD_1, FIRMWARE_8411_1,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_39] =
		_R("RTL8106e",		RTL_TD_1, FIRMWARE_8106E_1,
							JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_40] =
		_R("RTL8168g/8111g",	RTL_TD_1, FIRMWARE_8168G_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_41] =
		_R("RTL8168g/8111g",	RTL_TD_1, NULL, JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_42] =
		_R("RTL8168g/8111g",	RTL_TD_1, FIRMWARE_8168G_3,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_43] =
		_R("RTL8106e",		RTL_TD_1, FIRMWARE_8106E_2,
							JUMBO_1K, true),
	[RTL_GIGA_MAC_VER_44] =
		_R("RTL8411",		RTL_TD_1, FIRMWARE_8411_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_45] =
		_R("RTL8168h/8111h",	RTL_TD_1, FIRMWARE_8168H_1,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_46] =
		_R("RTL8168h/8111h",	RTL_TD_1, FIRMWARE_8168H_2,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_47] =
		_R("RTL8107e",		RTL_TD_1, FIRMWARE_8107E_1,
							JUMBO_1K, false),
	[RTL_GIGA_MAC_VER_48] =
		_R("RTL8107e",		RTL_TD_1, FIRMWARE_8107E_2,
							JUMBO_1K, false),
	[RTL_GIGA_MAC_VER_49] =
		_R("RTL8168ep/8111ep",	RTL_TD_1, NULL,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_50] =
		_R("RTL8168ep/8111ep",	RTL_TD_1, NULL,
							JUMBO_9K, false),
	[RTL_GIGA_MAC_VER_51] =
		_R("RTL8168ep/8111ep",	RTL_TD_1, NULL,
							JUMBO_9K, false),
};
#undef _R

enum cfg_version {
	RTL_CFG_0 = 0x00,
	RTL_CFG_1,
	RTL_CFG_2
};

static const struct pci_device_id rtl8169_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8129), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8136), 0, 0, RTL_CFG_2 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8161), 0, 0, RTL_CFG_1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8167), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8168), 0, 0, RTL_CFG_1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8169), 0, 0, RTL_CFG_0 },
	{ PCI_VENDOR_ID_DLINK,			0x4300,
		PCI_VENDOR_ID_DLINK, 0x4b10,		 0, 0, RTL_CFG_1 },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK,	0x4300), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK,	0x4302), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(PCI_VENDOR_ID_AT,		0xc107), 0, 0, RTL_CFG_0 },
	{ PCI_DEVICE(0x16ec,			0x0116), 0, 0, RTL_CFG_0 },
	{ PCI_VENDOR_ID_LINKSYS,		0x1032,
		PCI_ANY_ID, 0x0024, 0, 0, RTL_CFG_0 },
	{ 0x0001,				0x8168,
		PCI_ANY_ID, 0x2410, 0, 0, RTL_CFG_2 },
	{0,},
};

MODULE_DEVICE_TABLE(pci, rtl8169_pci_tbl);

static int rx_buf_sz = 16383;
static int use_dac = -1;
static struct {
	uint32_t msg_enable;
} debug = { -1 };

enum rtl_registers {
	MAC0		= 0,	/* Ethernet hardware address. */
	MAC4		= 4,
	MAR0		= 8,	/* Multicast filter. */
	CounterAddrLow		= 0x10,
	CounterAddrHigh		= 0x14,
	TxDescStartAddrLow	= 0x20,
	TxDescStartAddrHigh	= 0x24,
	TxHDescStartAddrLow	= 0x28,
	TxHDescStartAddrHigh	= 0x2c,
	FLASH		= 0x30,
	ERSR		= 0x36,
	ChipCmd		= 0x37,
	TxPoll		= 0x38,
	IntrMask	= 0x3c,
	IntrStatus	= 0x3e,

	TxConfig	= 0x40,
#define	TXCFG_AUTO_FIFO			(1 << 7)	/* 8111e-vl */
#define	TXCFG_EMPTY			(1 << 11)	/* 8111e-vl */

	RxConfig	= 0x44,
#define	RX128_INT_EN			(1 << 15)	/* 8111c and later */
#define	RX_MULTI_EN			(1 << 14)	/* 8111c only */
#define	RXCFG_FIFO_SHIFT		13
					/* No threshold before first PCI xfer */
#define	RX_FIFO_THRESH			(7 << RXCFG_FIFO_SHIFT)
#define	RX_EARLY_OFF			(1 << 11)
#define	RXCFG_DMA_SHIFT			8
					/* Unlimited maximum PCI burst. */
#define	RX_DMA_BURST			(7 << RXCFG_DMA_SHIFT)

	RxMissed	= 0x4c,
	Cfg9346		= 0x50,
	Config0		= 0x51,
	Config1		= 0x52,
	Config2		= 0x53,
#define PME_SIGNAL			(1 << 5)	/* 8168c and later */

	Config3		= 0x54,
	Config4		= 0x55,
	Config5		= 0x56,
	MultiIntr	= 0x5c,
	PHYAR		= 0x60,
	PHYstatus	= 0x6c,
	RxMaxSize	= 0xda,
	CPlusCmd	= 0xe0,
	IntrMitigate	= 0xe2,
	RxDescAddrLow	= 0xe4,
	RxDescAddrHigh	= 0xe8,
	EarlyTxThres	= 0xec,	/* 8169. Unit of 32 bytes. */

#define NoEarlyTx	0x3f	/* Max value : no early transmit. */

	MaxTxPacketSize	= 0xec,	/* 8101/8168. Unit of 128 bytes. */

#define TxPacketMax	(8064 >> 7)
#define EarlySize	0x27

	FuncEvent	= 0xf0,
	FuncEventMask	= 0xf4,
	FuncPresetState	= 0xf8,
	IBCR0           = 0xf8,
	IBCR2           = 0xf9,
	IBIMR0          = 0xfa,
	IBISR0          = 0xfb,
	FuncForceEvent	= 0xfc,
};

enum rtl8110_registers {
	TBICSR			= 0x64,
	TBI_ANAR		= 0x68,
	TBI_LPAR		= 0x6a,
};

enum rtl8168_8101_registers {
	CSIDR			= 0x64,
	CSIAR			= 0x68,
#define	CSIAR_FLAG			0x80000000
#define	CSIAR_WRITE_CMD			0x80000000
#define	CSIAR_BYTE_ENABLE		0x0f
#define	CSIAR_BYTE_ENABLE_SHIFT		12
#define	CSIAR_ADDR_MASK			0x0fff
#define CSIAR_FUNC_CARD			0x00000000
#define CSIAR_FUNC_SDIO			0x00010000
#define CSIAR_FUNC_NIC			0x00020000
#define CSIAR_FUNC_NIC2			0x00010000
	PMCH			= 0x6f,
	EPHYAR			= 0x80,
#define	EPHYAR_FLAG			0x80000000
#define	EPHYAR_WRITE_CMD		0x80000000
#define	EPHYAR_REG_MASK			0x1f
#define	EPHYAR_REG_SHIFT		16
#define	EPHYAR_DATA_MASK		0xffff
	DLLPR			= 0xd0,
#define	PFM_EN				(1 << 6)
#define	TX_10M_PS_EN			(1 << 7)
	DBG_REG			= 0xd1,
#define	FIX_NAK_1			(1 << 4)
#define	FIX_NAK_2			(1 << 3)
	TWSI			= 0xd2,
	MCU			= 0xd3,
#define	NOW_IS_OOB			(1 << 7)
#define	TX_EMPTY			(1 << 5)
#define	RX_EMPTY			(1 << 4)
#define	RXTX_EMPTY			(TX_EMPTY | RX_EMPTY)
#define	EN_NDP				(1 << 3)
#define	EN_OOB_RESET			(1 << 2)
#define	LINK_LIST_RDY			(1 << 1)
	EFUSEAR			= 0xdc,
#define	EFUSEAR_FLAG			0x80000000
#define	EFUSEAR_WRITE_CMD		0x80000000
#define	EFUSEAR_READ_CMD		0x00000000
#define	EFUSEAR_REG_MASK		0x03ff
#define	EFUSEAR_REG_SHIFT		8
#define	EFUSEAR_DATA_MASK		0xff
	MISC_1			= 0xf2,
#define	PFM_D3COLD_EN			(1 << 6)
};

enum rtl8168_registers {
	LED_FREQ		= 0x1a,
	EEE_LED			= 0x1b,
	ERIDR			= 0x70,
	ERIAR			= 0x74,
#define ERIAR_FLAG			0x80000000
#define ERIAR_WRITE_CMD			0x80000000
#define ERIAR_READ_CMD			0x00000000
#define ERIAR_ADDR_BYTE_ALIGN		4
#define ERIAR_TYPE_SHIFT		16
#define ERIAR_EXGMAC			(0x00 << ERIAR_TYPE_SHIFT)
#define ERIAR_MSIX			(0x01 << ERIAR_TYPE_SHIFT)
#define ERIAR_ASF			(0x02 << ERIAR_TYPE_SHIFT)
#define ERIAR_OOB			(0x02 << ERIAR_TYPE_SHIFT)
#define ERIAR_MASK_SHIFT		12
#define ERIAR_MASK_0001			(0x1 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0011			(0x3 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0100			(0x4 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_0101			(0x5 << ERIAR_MASK_SHIFT)
#define ERIAR_MASK_1111			(0xf << ERIAR_MASK_SHIFT)
	EPHY_RXER_NUM		= 0x7c,
	OCPDR			= 0xb0,	/* OCP GPHY access */
#define OCPDR_WRITE_CMD			0x80000000
#define OCPDR_READ_CMD			0x00000000
#define OCPDR_REG_MASK			0x7f
#define OCPDR_GPHY_REG_SHIFT		16
#define OCPDR_DATA_MASK			0xffff
	OCPAR			= 0xb4,
#define OCPAR_FLAG			0x80000000
#define OCPAR_GPHY_WRITE_CMD		0x8000f060
#define OCPAR_GPHY_READ_CMD		0x0000f060
	GPHY_OCP		= 0xb8,
	RDSAR1			= 0xd0,	/* 8168c only. Undocumented on 8168dp */
	MISC			= 0xf0,	/* 8168e only. */
#define TXPLA_RST			(1 << 29)
#define DISABLE_LAN_EN			(1 << 23) /* Enable GPIO pin */
#define PWM_EN				(1 << 22)
#define RXDV_GATED_EN			(1 << 19)
#define EARLY_TALLY_EN			(1 << 16)
};

enum rtl_register_content {
	/* InterruptStatusBits */
	SYSErr		= 0x8000,
	PCSTimeout	= 0x4000,
	SWInt		= 0x0100,
	TxDescUnavail	= 0x0080,
	RxFIFOOver	= 0x0040,
	LinkChg		= 0x0020,
	RxOverflow	= 0x0010,
	TxErr		= 0x0008,
	TxOK		= 0x0004,
	RxErr		= 0x0002,
	RxOK		= 0x0001,

	/* RxStatusDesc */
	RxBOVF	= (1 << 24),
	RxFOVF	= (1 << 23),
	RxRWT	= (1 << 22),
	RxRES	= (1 << 21),
	RxRUNT	= (1 << 20),
	RxCRC	= (1 << 19),

	/* ChipCmdBits */
	StopReq		= 0x80,
	CmdReset	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,
	RxBufEmpty	= 0x01,

	/* TXPoll register p.5 */
	HPQ		= 0x80,		/* Poll cmd on the high prio queue */
	NPQ		= 0x40,		/* Poll cmd on the low prio queue */
	FSWInt		= 0x01,		/* Forced software interrupt */

	/* Cfg9346Bits */
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xc0,

	/* rx_mode_bits */
	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,
#define RX_CONFIG_ACCEPT_MASK		0x3f

	/* TxConfigBits */
	TxInterFrameGapShift = 24,
	TxDMAShift = 8,	/* DMA burst value (0-7) is shift this many bits */

	/* Config1 register p.24 */
	LEDS1		= (1 << 7),
	LEDS0		= (1 << 6),
	Speed_down	= (1 << 4),
	MEMMAP		= (1 << 3),
	IOMAP		= (1 << 2),
	VPD		= (1 << 1),
	PMEnable	= (1 << 0),	/* Power Management Enable */

	/* Config2 register p. 25 */
	ClkReqEn	= (1 << 7),	/* Clock Request Enable */
	MSIEnable	= (1 << 5),	/* 8169 only. Reserved in the 8168. */
	PCI_Clock_66MHz = 0x01,
	PCI_Clock_33MHz = 0x00,

	/* Config3 register p.25 */
	MagicPacket	= (1 << 5),	/* Wake up when receives a Magic Packet */
	LinkUp		= (1 << 4),	/* Wake up when the cable connection is re-established */
	Jumbo_En0	= (1 << 2),	/* 8168 only. Reserved in the 8168b */
	Rdy_to_L23	= (1 << 1),	/* L23 Enable */
	Beacon_en	= (1 << 0),	/* 8168 only. Reserved in the 8168b */

	/* Config4 register */
	Jumbo_En1	= (1 << 1),	/* 8168 only. Reserved in the 8168b */

	/* Config5 register p.27 */
	BWF		= (1 << 6),	/* Accept Broadcast wakeup frame */
	MWF		= (1 << 5),	/* Accept Multicast wakeup frame */
	UWF		= (1 << 4),	/* Accept Unicast wakeup frame */
	Spi_en		= (1 << 3),
	LanWake		= (1 << 1),	/* LanWake enable/disable */
	PMEStatus	= (1 << 0),	/* PME status can be reset by PCI RST# */
	ASPM_en		= (1 << 0),	/* ASPM enable */

	/* TBICSR p.28 */
	TBIReset	= 0x80000000,
	TBILoopback	= 0x40000000,
	TBINwEnable	= 0x20000000,
	TBINwRestart	= 0x10000000,
	TBILinkOk	= 0x02000000,
	TBINwComplete	= 0x01000000,

	/* CPlusCmd p.31 */
	EnableBist	= (1 << 15),	// 8168 8101
	Mac_dbgo_oe	= (1 << 14),	// 8168 8101
	Normal_mode	= (1 << 13),	// unused
	Force_half_dup	= (1 << 12),	// 8168 8101
	Force_rxflow_en	= (1 << 11),	// 8168 8101
	Force_txflow_en	= (1 << 10),	// 8168 8101
	Cxpl_dbg_sel	= (1 << 9),	// 8168 8101
	ASF		= (1 << 8),	// 8168 8101
	PktCntrDisable	= (1 << 7),	// 8168 8101
	Mac_dbgo_sel	= 0x001c,	// 8168
	RxVlan		= (1 << 6),
	RxChkSum	= (1 << 5),
	PCIDAC		= (1 << 4),
	PCIMulRW	= (1 << 3),
	INTT_0		= 0x0000,	// 8168
	INTT_1		= 0x0001,	// 8168
	INTT_2		= 0x0002,	// 8168
	INTT_3		= 0x0003,	// 8168

	/* rtl8169_PHYstatus */
	TBI_Enable	= 0x80,
	TxFlowCtrl	= 0x40,
	RxFlowCtrl	= 0x20,
	_1000bpsF	= 0x10,
	_100bps		= 0x08,
	_10bps		= 0x04,
	LinkStatus	= 0x02,
	FullDup		= 0x01,

	/* _TBICSRBit */
	TBILinkOK	= 0x02000000,

	/* ResetCounterCommand */
	CounterReset	= 0x1,

	/* DumpCounterCommand */
	CounterDump	= 0x8,

	/* magic enable v2 */
	MagicPacket_v2	= (1 << 16),	/* Wake up when receives a Magic Packet */
};

enum rtl_desc_bit {
	/* First doubleword. */
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */
};

/* Generic case. */
enum rtl_tx_desc_bit {
	/* First doubleword. */
	TD_LSO		= (1 << 27),		/* Large Send Offload */
#define TD_MSS_MAX			0x07ffu	/* MSS value */

	/* Second doubleword. */
	TxVlanTag	= (1 << 17),		/* Add VLAN tag */
};

/* 8169, 8168b and 810x except 8102e. */
enum rtl_tx_desc_bit_0 {
	/* First doubleword. */
#define TD0_MSS_SHIFT			16	/* MSS position (11 bits) */
	TD0_TCP_CS	= (1 << 16),		/* Calculate TCP/IP checksum */
	TD0_UDP_CS	= (1 << 17),		/* Calculate UDP/IP checksum */
	TD0_IP_CS	= (1 << 18),		/* Calculate IP checksum */
};

/* 8102e, 8168c and beyond. */
enum rtl_tx_desc_bit_1 {
	/* First doubleword. */
	TD1_GTSENV4	= (1 << 26),		/* Giant Send for IPv4 */
	TD1_GTSENV6	= (1 << 25),		/* Giant Send for IPv6 */
#define GTTCPHO_SHIFT			18
#define GTTCPHO_MAX			0x7fU

	/* Second doubleword. */
#define TCPHO_SHIFT			18
#define TCPHO_MAX			0x3ffU
#define TD1_MSS_SHIFT			18	/* MSS position (11 bits) */
	TD1_IPv6_CS	= (1 << 28),		/* Calculate IPv6 checksum */
	TD1_IPv4_CS	= (1 << 29),		/* Calculate IPv4 checksum */
	TD1_TCP_CS	= (1 << 30),		/* Calculate TCP/IP checksum */
	TD1_UDP_CS	= (1 << 31),		/* Calculate UDP/IP checksum */
};

enum rtl_rx_desc_bit {
	/* Rx private */
	PID1		= (1 << 18), /* Protocol ID bit 1/2 */
	PID0		= (1 << 17), /* Protocol ID bit 0/2 */

#define RxProtoUDP	(PID1)
#define RxProtoTCP	(PID0)
#define RxProtoIP	(PID1 | PID0)
#define RxProtoMask	RxProtoIP

	IPFail		= (1 << 16), /* IP checksum failed */
	UDPFail		= (1 << 15), /* UDP/IP checksum failed */
	TCPFail		= (1 << 14), /* TCP/IP checksum failed */
	RxVlanTag	= (1 << 16), /* VLAN tag available */
};

#define RsvdMask	0x3fffc000

struct TxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct RxDesc {
	__le32 opts1;
	__le32 opts2;
	__le64 addr;
};

struct ring_info {
	struct block	*block;
	uint32_t		len;
	uint8_t		__pad[sizeof(void *) - sizeof(uint32_t)];
};

enum features {
	RTL_FEATURE_WOL		= (1 << 0),
	RTL_FEATURE_MSI		= (1 << 1),
	RTL_FEATURE_GMII	= (1 << 2),
};

struct rtl8169_counters {
	__le64	tx_packets;
	__le64	rx_packets;
	__le64	tx_errors;
	__le32	rx_errors;
	__le16	rx_missed;
	__le16	align_errors;
	__le32	tx_one_collision;
	__le32	tx_multi_collision;
	__le64	rx_unicast;
	__le64	rx_broadcast;
	__le32	rx_multicast;
	__le16	tx_aborted;
	__le16	tx_underun;
};

struct rtl8169_tc_offsets {
	bool	inited;
	__le64	tx_errors;
	__le32	tx_multi_collision;
	__le16	tx_aborted;
};

enum rtl_flag {
	RTL_FLAG_TASK_ENABLED,
	RTL_FLAG_TASK_SLOW_PENDING,
	RTL_FLAG_TASK_RESET_PENDING,
	RTL_FLAG_TASK_PHY_PENDING,
	RTL_FLAG_MAX
};

struct rtl8169_stats {
	uint64_t			packets;
	uint64_t			bytes;
	struct u64_stats_sync	syncp;
};

struct rtl_poke_args {
	struct ether *edev;
	struct rtl8169_private *tp;
};

struct rtl8169_private {
	/* 9ns compat */
	struct pci_device		*pcidev;
	struct ether			*edev;
	TAILQ_ENTRY(rtl8169_private) link9ns;
	const struct pci_device_id	*pci_id; /* for navigating pci/pnp */
	struct rendez			irq_task;	/* bottom half IRQ */
	struct poke_tracker		poker;		/* tx concurrency */
	bool				active;
	bool				attached;

	void __iomem *mmio_addr;	/* memory map physical address */
	struct pci_device *pci_dev;
	struct ether *dev;
	struct napi_struct napi;
	uint32_t msg_enable;
	uint16_t txd_version;
	uint16_t mac_version;
	uint32_t cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
	uint32_t cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
	uint32_t dirty_tx;
	struct rtl8169_stats rx_stats;
	struct rtl8169_stats tx_stats;
	struct TxDesc *TxDescArray;	/* 256-aligned Tx descriptor ring */
	struct RxDesc *RxDescArray;	/* 256-aligned Rx descriptor ring */
	dma_addr_t TxPhyAddr;
	dma_addr_t RxPhyAddr;
	void *Rx_databuff[NUM_RX_DESC];	/* Rx data buffers */
	struct ring_info tx_skb[NUM_TX_DESC];	/* Tx data buffers */
	struct timer_list timer;
	uint16_t cp_cmd;

	uint16_t event_slow;

	struct mdio_ops {
		void (*write)(struct rtl8169_private *, int, int);
		int (*read)(struct rtl8169_private *, int);
	} mdio_ops;

	struct pll_power_ops {
		void (*down)(struct rtl8169_private *);
		void (*up)(struct rtl8169_private *);
	} pll_power_ops;

	struct jumbo_ops {
		void (*enable)(struct rtl8169_private *);
		void (*disable)(struct rtl8169_private *);
	} jumbo_ops;

	struct csi_ops {
		void (*write)(struct rtl8169_private *, int, int);
		uint32_t (*read)(struct rtl8169_private *, int);
	} csi_ops;

	int (*set_speed)(struct ether *, uint8_t aneg, uint16_t sp,
			 uint8_t dpx, uint32_t adv);
#if 0 // AKAROS_PORT
	int (*get_link_ksettings)(struct ether *,
				  struct ethtool_link_ksettings *);
#endif
	void (*phy_reset_enable)(struct rtl8169_private *tp);
	void (*hw_start)(struct ether *);
	unsigned int (*phy_reset_pending)(struct rtl8169_private *tp);
	unsigned int (*link_ok)(void __iomem *);
#if 0 // AKAROS_PORT
	int (*do_ioctl)(struct rtl8169_private *tp, struct mii_ioctl_data *data, int cmd);
#endif
	bool (*tso_csum)(struct rtl8169_private *, struct block *,
			 uint32_t *);

	struct {
		DECLARE_BITMAP(flags, RTL_FLAG_MAX);
		qlock_t mutex;
		struct work_struct work;
	} wk;

	unsigned features;

	struct mii_if_info mii;
	dma_addr_t counters_phys_addr;
	struct rtl8169_counters *counters;
	struct rtl8169_tc_offsets tc_offset;
	uint32_t saved_wolopts;
	uint32_t opts1_mask;

	struct rtl_fw {
		const struct firmware *fw;

#define RTL_VER_SIZE		32

		char version[RTL_VER_SIZE];

		struct rtl_fw_phy_action {
			__le32 *code;
			size_t size;
		} phy_action;
	} *rtl_fw;
#define RTL_FIRMWARE_UNKNOWN	ERR_PTR(-EAGAIN)

	uint32_t ocp_base;
};

MODULE_AUTHOR("Realtek and the Linux r8169 crew <netdev@vger.kernel.org>");
MODULE_DESCRIPTION("RealTek RTL-8169 Gigabit Ethernet driver");
module_param(use_dac, int, 0);
MODULE_PARM_DESC(use_dac, "Enable PCI DAC. Unsafe on 32 bit PCI slot.");
module_param_named(debug, debug.msg_enable, int, 0);
MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., 16=all)");
MODULE_LICENSE("GPL");
MODULE_VERSION(RTL8169_VERSION);
MODULE_FIRMWARE(FIRMWARE_8168D_1);
MODULE_FIRMWARE(FIRMWARE_8168D_2);
MODULE_FIRMWARE(FIRMWARE_8168E_1);
MODULE_FIRMWARE(FIRMWARE_8168E_2);
MODULE_FIRMWARE(FIRMWARE_8168E_3);
MODULE_FIRMWARE(FIRMWARE_8105E_1);
MODULE_FIRMWARE(FIRMWARE_8168F_1);
MODULE_FIRMWARE(FIRMWARE_8168F_2);
MODULE_FIRMWARE(FIRMWARE_8402_1);
MODULE_FIRMWARE(FIRMWARE_8411_1);
MODULE_FIRMWARE(FIRMWARE_8411_2);
MODULE_FIRMWARE(FIRMWARE_8106E_1);
MODULE_FIRMWARE(FIRMWARE_8106E_2);
MODULE_FIRMWARE(FIRMWARE_8168G_2);
MODULE_FIRMWARE(FIRMWARE_8168G_3);
MODULE_FIRMWARE(FIRMWARE_8168H_1);
MODULE_FIRMWARE(FIRMWARE_8168H_2);
MODULE_FIRMWARE(FIRMWARE_8107E_1);
MODULE_FIRMWARE(FIRMWARE_8107E_2);

static void rtl_lock_work(struct rtl8169_private *tp)
{
	qlock(&tp->wk.mutex);
}

static void rtl_unlock_work(struct rtl8169_private *tp)
{
	qunlock(&tp->wk.mutex);
}

static void rtl_tx_performance_tweak(struct pci_device *pdev, uint16_t force)
{
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_DEVCTL,
					   PCI_EXP_DEVCTL_READRQ, force);
}

struct rtl_cond {
	bool (*check)(struct rtl8169_private *);
	const char *msg;
};

static void rtl_udelay(unsigned int d)
{
	udelay(d);
}

static bool rtl_loop_wait(struct rtl8169_private *tp, const struct rtl_cond *c,
			  void (*delay)(unsigned int), unsigned int d, int n,
			  bool high)
{
	int i;

	for (i = 0; i < n; i++) {
		delay(d);
		if (c->check(tp) == high)
			return true;
	}
	netif_err(tp, drv, tp->dev, "%s == %d (loop: %d, delay: %d).\n",
		  c->msg, !high, n, d);
	return false;
}

static bool rtl_udelay_loop_wait_high(struct rtl8169_private *tp,
				      const struct rtl_cond *c,
				      unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, true);
}

static bool rtl_udelay_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, rtl_udelay, d, n, false);
}

static bool rtl_msleep_loop_wait_high(struct rtl8169_private *tp,
				      const struct rtl_cond *c,
				      unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, true);
}

static bool rtl_msleep_loop_wait_low(struct rtl8169_private *tp,
				     const struct rtl_cond *c,
				     unsigned int d, int n)
{
	return rtl_loop_wait(tp, c, msleep, d, n, false);
}

#define DECLARE_RTL_COND(name)				\
static bool name ## _check(struct rtl8169_private *);	\
							\
static const struct rtl_cond name = {			\
	.check	= name ## _check,			\
	.msg	= #name					\
};							\
							\
static bool name ## _check(struct rtl8169_private *tp)

static bool rtl_ocp_reg_failure(struct rtl8169_private *tp, uint32_t reg)
{
	if (reg & 0xffff0001) {
		netif_err(tp, drv, tp->dev, "Invalid ocp reg %x!\n", reg);
		return true;
	}
	return false;
}

DECLARE_RTL_COND(rtl_ocp_gphy_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(GPHY_OCP) & OCPAR_FLAG;
}

static void r8168_phy_ocp_write(struct rtl8169_private *tp, uint32_t reg,
                                uint32_t data)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if (rtl_ocp_reg_failure(tp, reg))
		return;

	RTL_W32(GPHY_OCP, OCPAR_FLAG | (reg << 15) | data);

	rtl_udelay_loop_wait_low(tp, &rtl_ocp_gphy_cond, 25, 10);
}

static uint16_t r8168_phy_ocp_read(struct rtl8169_private *tp, uint32_t reg)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if (rtl_ocp_reg_failure(tp, reg))
		return 0;

	RTL_W32(GPHY_OCP, reg << 15);

	return rtl_udelay_loop_wait_high(tp, &rtl_ocp_gphy_cond, 25, 10) ?
		(RTL_R32(GPHY_OCP) & 0xffff) : ~0;
}

static void r8168_mac_ocp_write(struct rtl8169_private *tp, uint32_t reg,
				uint32_t data)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if (rtl_ocp_reg_failure(tp, reg))
		return;

	RTL_W32(OCPDR, OCPAR_FLAG | (reg << 15) | data);
}

static uint16_t r8168_mac_ocp_read(struct rtl8169_private *tp, uint32_t reg)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if (rtl_ocp_reg_failure(tp, reg))
		return 0;

	RTL_W32(OCPDR, reg << 15);

	return RTL_R32(OCPDR);
}

#define OCP_STD_PHY_BASE	0xa400

static void r8168g_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	if (reg == 0x1f) {
		tp->ocp_base = value ? value << 4 : OCP_STD_PHY_BASE;
		return;
	}

	if (tp->ocp_base != OCP_STD_PHY_BASE)
		reg -= 0x10;

	r8168_phy_ocp_write(tp, tp->ocp_base + reg * 2, value);
}

static int r8168g_mdio_read(struct rtl8169_private *tp, int reg)
{
	if (tp->ocp_base != OCP_STD_PHY_BASE)
		reg -= 0x10;

	return r8168_phy_ocp_read(tp, tp->ocp_base + reg * 2);
}

static void mac_mcu_write(struct rtl8169_private *tp, int reg, int value)
{
	if (reg == 0x1f) {
		tp->ocp_base = value << 4;
		return;
	}

	r8168_mac_ocp_write(tp, tp->ocp_base + reg, value);
}

static int mac_mcu_read(struct rtl8169_private *tp, int reg)
{
	return r8168_mac_ocp_read(tp, tp->ocp_base + reg);
}

DECLARE_RTL_COND(rtl_phyar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(PHYAR) & 0x80000000;
}

static void r8169_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(PHYAR, 0x80000000 | (reg & 0x1f) << 16 | (value & 0xffff));

	rtl_udelay_loop_wait_low(tp, &rtl_phyar_cond, 25, 20);
	/*
	 * According to hardware specs a 20us delay is required after write
	 * complete indication, but before sending next command.
	 */
	udelay(20);
}

static int r8169_mdio_read(struct rtl8169_private *tp, int reg)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int value;

	RTL_W32(PHYAR, 0x0 | (reg & 0x1f) << 16);

	value = rtl_udelay_loop_wait_high(tp, &rtl_phyar_cond, 25, 20) ?
		RTL_R32(PHYAR) & 0xffff : ~0;

	/*
	 * According to hardware specs a 20us delay is required after read
	 * complete indication, but before sending next command.
	 */
	udelay(20);

	return value;
}

DECLARE_RTL_COND(rtl_ocpar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(OCPAR) & OCPAR_FLAG;
}

static void r8168dp_1_mdio_access(struct rtl8169_private *tp, int reg,
                                  uint32_t data)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(OCPDR, data | ((reg & OCPDR_REG_MASK) << OCPDR_GPHY_REG_SHIFT));
	RTL_W32(OCPAR, OCPAR_GPHY_WRITE_CMD);
	RTL_W32(EPHY_RXER_NUM, 0);

	rtl_udelay_loop_wait_low(tp, &rtl_ocpar_cond, 1000, 100);
}

static void r8168dp_1_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	r8168dp_1_mdio_access(tp, reg,
			      OCPDR_WRITE_CMD | (value & OCPDR_DATA_MASK));
}

static int r8168dp_1_mdio_read(struct rtl8169_private *tp, int reg)
{
	void __iomem *ioaddr = tp->mmio_addr;

	r8168dp_1_mdio_access(tp, reg, OCPDR_READ_CMD);

	mdelay(1);
	RTL_W32(OCPAR, OCPAR_GPHY_READ_CMD);
	RTL_W32(EPHY_RXER_NUM, 0);

	return rtl_udelay_loop_wait_high(tp, &rtl_ocpar_cond, 1000, 100) ?
		RTL_R32(OCPDR) & OCPDR_DATA_MASK : ~0;
}

#define R8168DP_1_MDIO_ACCESS_BIT	0x00020000

static void r8168dp_2_mdio_start(void __iomem *ioaddr)
{
	RTL_W32(0xd0, RTL_R32(0xd0) & ~R8168DP_1_MDIO_ACCESS_BIT);
}

static void r8168dp_2_mdio_stop(void __iomem *ioaddr)
{
	RTL_W32(0xd0, RTL_R32(0xd0) | R8168DP_1_MDIO_ACCESS_BIT);
}

static void r8168dp_2_mdio_write(struct rtl8169_private *tp, int reg, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	r8168dp_2_mdio_start(ioaddr);

	r8169_mdio_write(tp, reg, value);

	r8168dp_2_mdio_stop(ioaddr);
}

static int r8168dp_2_mdio_read(struct rtl8169_private *tp, int reg)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int value;

	r8168dp_2_mdio_start(ioaddr);

	value = r8169_mdio_read(tp, reg);

	r8168dp_2_mdio_stop(ioaddr);

	return value;
}

static void rtl_writephy(struct rtl8169_private *tp, int location,
			 uint32_t val)
{
	tp->mdio_ops.write(tp, location, val);
}

static int rtl_readphy(struct rtl8169_private *tp, int location)
{
	return tp->mdio_ops.read(tp, location);
}

static void rtl_patchphy(struct rtl8169_private *tp, int reg_addr, int value)
{
	rtl_writephy(tp, reg_addr, rtl_readphy(tp, reg_addr) | value);
}

static void rtl_w0w1_phy(struct rtl8169_private *tp, int reg_addr, int p, int m)
{
	int val;

	val = rtl_readphy(tp, reg_addr);
	rtl_writephy(tp, reg_addr, (val & ~m) | p);
}

static void rtl_mdio_write(struct ether *dev, int phy_id, int location,
			   int val)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_writephy(tp, location, val);
}

static int rtl_mdio_read(struct ether *dev, int phy_id, int location)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return rtl_readphy(tp, location);
}

DECLARE_RTL_COND(rtl_ephyar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(EPHYAR) & EPHYAR_FLAG;
}

static void rtl_ephy_write(struct rtl8169_private *tp, int reg_addr, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(EPHYAR, EPHYAR_WRITE_CMD | (value & EPHYAR_DATA_MASK) |
		(reg_addr & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	rtl_udelay_loop_wait_low(tp, &rtl_ephyar_cond, 10, 100);

	udelay(10);
}

static uint16_t rtl_ephy_read(struct rtl8169_private *tp, int reg_addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(EPHYAR, (reg_addr & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_ephyar_cond, 10, 100) ?
		RTL_R32(EPHYAR) & EPHYAR_DATA_MASK : ~0;
}

DECLARE_RTL_COND(rtl_eriar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(ERIAR) & ERIAR_FLAG;
}

static void rtl_eri_write(struct rtl8169_private *tp, int addr, uint32_t mask,
			  uint32_t val, int type)
{
	void __iomem *ioaddr = tp->mmio_addr;

	assert(!((addr & 3) || (mask == 0)));
	RTL_W32(ERIDR, val);
	RTL_W32(ERIAR, ERIAR_WRITE_CMD | type | mask | addr);

	rtl_udelay_loop_wait_low(tp, &rtl_eriar_cond, 100, 100);
}

static uint32_t rtl_eri_read(struct rtl8169_private *tp, int addr, int type)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(ERIAR, ERIAR_READ_CMD | type | ERIAR_MASK_1111 | addr);

	return rtl_udelay_loop_wait_high(tp, &rtl_eriar_cond, 100, 100) ?
		RTL_R32(ERIDR) : ~0;
}

static void rtl_w0w1_eri(struct rtl8169_private *tp, int addr, uint32_t mask,
                         uint32_t p, uint32_t m, int type)
{
	uint32_t val;

	val = rtl_eri_read(tp, addr, type);
	rtl_eri_write(tp, addr, mask, (val & ~m) | p, type);
}

static uint32_t r8168dp_ocp_read(struct rtl8169_private *tp, uint8_t mask,
                                 uint16_t reg)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(OCPAR, ((uint32_t)mask & 0x0f) << 12 | (reg & 0x0fff));
	return rtl_udelay_loop_wait_high(tp, &rtl_ocpar_cond, 100, 20) ?
		RTL_R32(OCPDR) : ~0;
}

static uint32_t r8168ep_ocp_read(struct rtl8169_private *tp, uint8_t mask,
			    uint16_t reg)
{
	return rtl_eri_read(tp, reg, ERIAR_OOB);
}

static uint32_t ocp_read(struct rtl8169_private *tp, uint8_t mask, uint16_t reg)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		return r8168dp_ocp_read(tp, mask, reg);
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		return r8168ep_ocp_read(tp, mask, reg);
	default:
		panic("BUG");
		return ~0;
	}
}

static void r8168dp_ocp_write(struct rtl8169_private *tp, uint8_t mask,
                              uint16_t reg, uint32_t data)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(OCPDR, data);
	RTL_W32(OCPAR, OCPAR_FLAG | ((uint32_t)mask & 0x0f) << 12 | (reg & 0x0fff));
	rtl_udelay_loop_wait_low(tp, &rtl_ocpar_cond, 100, 20);
}

static void r8168ep_ocp_write(struct rtl8169_private *tp, uint8_t mask,
                              uint16_t reg, uint32_t data)
{
	rtl_eri_write(tp, reg, ((uint32_t)mask & 0x0f) << ERIAR_MASK_SHIFT,
		      data, ERIAR_OOB);
}

static void ocp_write(struct rtl8169_private *tp, uint8_t mask, uint16_t reg,
		      uint32_t data)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		r8168dp_ocp_write(tp, mask, reg, data);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		r8168ep_ocp_write(tp, mask, reg, data);
		break;
	default:
		panic("BUG");
		break;
	}
}

static void rtl8168_oob_notify(struct rtl8169_private *tp, uint8_t cmd)
{
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_0001, cmd, ERIAR_EXGMAC);

	ocp_write(tp, 0x1, 0x30, 0x00000001);
}

#define OOB_CMD_RESET		0x00
#define OOB_CMD_DRIVER_START	0x05
#define OOB_CMD_DRIVER_STOP	0x06

static uint16_t rtl8168_get_ocp_reg(struct rtl8169_private *tp)
{
	return (tp->mac_version == RTL_GIGA_MAC_VER_31) ? 0xb8 : 0x10;
}

DECLARE_RTL_COND(rtl_ocp_read_cond)
{
	uint16_t reg;

	reg = rtl8168_get_ocp_reg(tp);

	return ocp_read(tp, 0x0f, reg) & 0x00000800;
}

DECLARE_RTL_COND(rtl_ep_ocp_read_cond)
{
	return ocp_read(tp, 0x0f, 0x124) & 0x00000001;
}

DECLARE_RTL_COND(rtl_ocp_tx_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(IBISR0) & 0x02;
}

static void rtl8168ep_stop_cmac(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(IBCR2, RTL_R8(IBCR2) & ~0x01);
	rtl_msleep_loop_wait_low(tp, &rtl_ocp_tx_cond, 50, 2000);
	RTL_W8(IBISR0, RTL_R8(IBISR0) | 0x20);
	RTL_W8(IBCR0, RTL_R8(IBCR0) & ~0x01);
}

static void rtl8168dp_driver_start(struct rtl8169_private *tp)
{
	rtl8168_oob_notify(tp, OOB_CMD_DRIVER_START);
	rtl_msleep_loop_wait_high(tp, &rtl_ocp_read_cond, 10, 10);
}

static void rtl8168ep_driver_start(struct rtl8169_private *tp)
{
	ocp_write(tp, 0x01, 0x180, OOB_CMD_DRIVER_START);
	ocp_write(tp, 0x01, 0x30, ocp_read(tp, 0x01, 0x30) | 0x01);
	rtl_msleep_loop_wait_high(tp, &rtl_ep_ocp_read_cond, 10, 10);
}

static void rtl8168_driver_start(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl8168dp_driver_start(tp);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl8168ep_driver_start(tp);
		break;
	default:
		panic("BUG");
		break;
	}
}

static void rtl8168dp_driver_stop(struct rtl8169_private *tp)
{
	rtl8168_oob_notify(tp, OOB_CMD_DRIVER_STOP);
	rtl_msleep_loop_wait_low(tp, &rtl_ocp_read_cond, 10, 10);
}

static void rtl8168ep_driver_stop(struct rtl8169_private *tp)
{
	rtl8168ep_stop_cmac(tp);
	ocp_write(tp, 0x01, 0x180, OOB_CMD_DRIVER_STOP);
	ocp_write(tp, 0x01, 0x30, ocp_read(tp, 0x01, 0x30) | 0x01);
	rtl_msleep_loop_wait_low(tp, &rtl_ep_ocp_read_cond, 10, 10);
}

static void rtl8168_driver_stop(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl8168dp_driver_stop(tp);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl8168ep_driver_stop(tp);
		break;
	default:
		panic("BUG");
		break;
	}
}

static int r8168dp_check_dash(struct rtl8169_private *tp)
{
	uint16_t reg = rtl8168_get_ocp_reg(tp);

	return (ocp_read(tp, 0x0f, reg) & 0x00008000) ? 1 : 0;
}

static int r8168ep_check_dash(struct rtl8169_private *tp)
{
	return (ocp_read(tp, 0x0f, 0x128) & 0x00000001) ? 1 : 0;
}

static int r8168_check_dash(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		return r8168dp_check_dash(tp);
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		return r8168ep_check_dash(tp);
	default:
		return 0;
	}
}

struct exgmac_reg {
	uint16_t addr;
	uint16_t mask;
	uint32_t val;
};

static void rtl_write_exgmac_batch(struct rtl8169_private *tp,
				   const struct exgmac_reg *r, int len)
{
	while (len-- > 0) {
		rtl_eri_write(tp, r->addr, r->mask, r->val, ERIAR_EXGMAC);
		r++;
	}
}

DECLARE_RTL_COND(rtl_efusear_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(EFUSEAR) & EFUSEAR_FLAG;
}

static uint8_t rtl8168d_efuse_read(struct rtl8169_private *tp, int reg_addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(EFUSEAR, (reg_addr & EFUSEAR_REG_MASK) << EFUSEAR_REG_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_efusear_cond, 100, 300) ?
		RTL_R32(EFUSEAR) & EFUSEAR_DATA_MASK : ~0;
}

static uint16_t rtl_get_events(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R16(IntrStatus);
}

static void rtl_ack_events(struct rtl8169_private *tp, uint16_t bits)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(IntrStatus, bits);
	bus_wmb();
}

static void rtl_irq_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(IntrMask, 0);
	bus_wmb();
}

static void rtl_irq_enable(struct rtl8169_private *tp, uint16_t bits)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W16(IntrMask, bits);
}

#define RTL_EVENT_NAPI_RX	(RxOK | RxErr)
#define RTL_EVENT_NAPI_TX	(TxOK | TxErr)
#define RTL_EVENT_NAPI		(RTL_EVENT_NAPI_RX | RTL_EVENT_NAPI_TX)

static void rtl_irq_enable_all(struct rtl8169_private *tp)
{
	rtl_irq_enable(tp, RTL_EVENT_NAPI | tp->event_slow);
}

static void rtl8169_irq_mask_and_ack(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_irq_disable(tp);
	rtl_ack_events(tp, RTL_EVENT_NAPI | tp->event_slow);
	RTL_R8(ChipCmd);
}

static unsigned int rtl8169_tbi_reset_pending(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(TBICSR) & TBIReset;
}

static unsigned int rtl8169_xmii_reset_pending(struct rtl8169_private *tp)
{
	return rtl_readphy(tp, MII_BMCR) & BMCR_RESET;
}

static unsigned int rtl8169_tbi_link_ok(void __iomem *ioaddr)
{
	return RTL_R32(TBICSR) & TBILinkOk;
}

static unsigned int rtl8169_xmii_link_ok(void __iomem *ioaddr)
{
	return RTL_R8(PHYstatus) & LinkStatus;
}

static void rtl8169_tbi_reset_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(TBICSR, RTL_R32(TBICSR) | TBIReset);
}

static void rtl8169_xmii_reset_enable(struct rtl8169_private *tp)
{
	unsigned int val;

	val = rtl_readphy(tp, MII_BMCR) | BMCR_RESET;
	rtl_writephy(tp, MII_BMCR, val & 0xffff);
}

static void rtl_link_chg_patch(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct ether *dev = tp->dev;

	if (!netif_running(dev))
		return;

	if (tp->mac_version == RTL_GIGA_MAC_VER_34 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_38) {
		if (RTL_R8(PHYstatus) & _1000bpsF) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x00000011,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005,
				      ERIAR_EXGMAC);
		} else if (RTL_R8(PHYstatus) & _100bps) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005,
				      ERIAR_EXGMAC);
		} else {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x0000003f,
				      ERIAR_EXGMAC);
		}
		/* Reset packet filter */
		rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01,
			     ERIAR_EXGMAC);
		rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00,
			     ERIAR_EXGMAC);
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_35 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_36) {
		if (RTL_R8(PHYstatus) & _1000bpsF) {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x00000011,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x00000005,
				      ERIAR_EXGMAC);
		} else {
			rtl_eri_write(tp, 0x1bc, ERIAR_MASK_1111, 0x0000001f,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_1111, 0x0000003f,
				      ERIAR_EXGMAC);
		}
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_37) {
		if (RTL_R8(PHYstatus) & _10bps) {
			rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x4d02,
				      ERIAR_EXGMAC);
			rtl_eri_write(tp, 0x1dc, ERIAR_MASK_0011, 0x0060,
				      ERIAR_EXGMAC);
		} else {
			rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x0000,
				      ERIAR_EXGMAC);
		}
	}
}

static void __rtl8169_check_link_status(struct ether *dev,
					struct rtl8169_private *tp,
					void __iomem *ioaddr, bool pm)
{
	if (tp->link_ok(ioaddr)) {
		rtl_link_chg_patch(tp);
		/* This is to cancel a scheduled suspend if there's one. */
		if (pm)
			pm_request_resume(&tp->pci_dev->device);
		netif_carrier_on(dev);
		if (net_ratelimit())
			netif_info(tp, ifup, dev, "link up\n");
	} else {
		netif_carrier_off(dev);
		netif_info(tp, ifdown, dev, "link down\n");
		if (pm)
			pm_schedule_suspend(&tp->pci_dev->device, 5000);
	}
}

static void rtl8169_check_link_status(struct ether *dev,
				      struct rtl8169_private *tp,
				      void __iomem *ioaddr)
{
	__rtl8169_check_link_status(dev, tp, ioaddr, false);
}

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)

static uint32_t __rtl8169_get_wol(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	uint8_t options;
	uint32_t wolopts = 0;

	options = RTL_R8(Config1);
	if (!(options & PMEnable))
		return 0;

	options = RTL_R8(Config3);
	if (options & LinkUp)
		wolopts |= WAKE_PHY;
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		if (rtl_eri_read(tp, 0xdc, ERIAR_EXGMAC) & MagicPacket_v2)
			wolopts |= WAKE_MAGIC;
		break;
	default:
		if (options & MagicPacket)
			wolopts |= WAKE_MAGIC;
		break;
	}

	options = RTL_R8(Config5);
	if (options & UWF)
		wolopts |= WAKE_UCAST;
	if (options & BWF)
		wolopts |= WAKE_BCAST;
	if (options & MWF)
		wolopts |= WAKE_MCAST;

	return wolopts;
}

#if 0 // AKAROS_PORT
static void rtl8169_get_wol(struct ether *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = &tp->pci_dev->device;

	pm_runtime_get_noresume(d);

	rtl_lock_work(tp);

	wol->supported = WAKE_ANY;
	if (pm_runtime_active(d))
		wol->wolopts = __rtl8169_get_wol(tp);
	else
		wol->wolopts = tp->saved_wolopts;

	rtl_unlock_work(tp);

	pm_runtime_put_noidle(d);
}
#endif

static void __rtl8169_set_wol(struct rtl8169_private *tp, uint32_t wolopts)
{
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int i, tmp;
	static const struct {
		uint32_t opt;
		uint16_t reg;
		uint8_t  mask;
	} cfg[] = {
		{ WAKE_PHY,   Config3, LinkUp },
		{ WAKE_UCAST, Config5, UWF },
		{ WAKE_BCAST, Config5, BWF },
		{ WAKE_MCAST, Config5, MWF },
		{ WAKE_ANY,   Config5, LanWake },
		{ WAKE_MAGIC, Config3, MagicPacket }
	};
	uint8_t options;

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		tmp = ARRAY_SIZE(cfg) - 1;
		if (wolopts & WAKE_MAGIC)
			rtl_w0w1_eri(tp,
				     0x0dc,
				     ERIAR_MASK_0100,
				     MagicPacket_v2,
				     0x0000,
				     ERIAR_EXGMAC);
		else
			rtl_w0w1_eri(tp,
				     0x0dc,
				     ERIAR_MASK_0100,
				     0x0000,
				     MagicPacket_v2,
				     ERIAR_EXGMAC);
		break;
	default:
		tmp = ARRAY_SIZE(cfg);
		break;
	}

	for (i = 0; i < tmp; i++) {
		options = RTL_R8(cfg[i].reg) & ~cfg[i].mask;
		if (wolopts & cfg[i].opt)
			options |= cfg[i].mask;
		RTL_W8(cfg[i].reg, options);
	}

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_01 ... RTL_GIGA_MAC_VER_17:
		options = RTL_R8(Config1) & ~PMEnable;
		if (wolopts)
			options |= PMEnable;
		RTL_W8(Config1, options);
		break;
	default:
		options = RTL_R8(Config2) & ~PME_SIGNAL;
		if (wolopts)
			options |= PME_SIGNAL;
		RTL_W8(Config2, options);
		break;
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);
}

#if 0 // AKAROS_PORT
static int rtl8169_set_wol(struct ether *dev, struct ethtool_wolinfo *wol)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = &tp->pci_dev->device;

	pm_runtime_get_noresume(d);

	rtl_lock_work(tp);

	if (wol->wolopts)
		tp->features |= RTL_FEATURE_WOL;
	else
		tp->features &= ~RTL_FEATURE_WOL;
	if (pm_runtime_active(d))
		__rtl8169_set_wol(tp, wol->wolopts);
	else
		tp->saved_wolopts = wol->wolopts;

	rtl_unlock_work(tp);

	device_set_wakeup_enable(&tp->pci_dev->device, wol->wolopts);

	pm_runtime_put_noidle(d);

	return 0;
}
#endif

static const char *rtl_lookup_firmware_name(struct rtl8169_private *tp)
{
	return rtl_chip_infos[tp->mac_version].fw_name;
}

#if 0 // AKAROS_PORT
static void rtl8169_get_drvinfo(struct ether *dev,
				struct ethtool_drvinfo *info)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct rtl_fw *rtl_fw = tp->rtl_fw;

	strlcpy(info->driver, MODULENAME, sizeof(info->driver));
	strlcpy(info->version, RTL8169_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(tp->pci_dev), sizeof(info->bus_info));
	static_assert(!(sizeof(info->fw_version) < sizeof(rtl_fw->version)));
	if (!IS_ERR_OR_NULL(rtl_fw))
		strlcpy(info->fw_version, rtl_fw->version,
			sizeof(info->fw_version));
}
#endif

static int rtl8169_get_regs_len(struct ether *dev)
{
	return R8169_REGS_SIZE;
}

static int rtl8169_set_speed_tbi(struct ether *dev,
				 uint8_t autoneg, uint16_t speed, uint8_t duplex,
				 uint32_t ignored)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int ret = 0;
	uint32_t reg;

	reg = RTL_R32(TBICSR);
	if ((autoneg == AUTONEG_DISABLE) && (speed == SPEED_1000) &&
	    (duplex == DUPLEX_FULL)) {
		RTL_W32(TBICSR, reg & ~(TBINwEnable | TBINwRestart));
	} else if (autoneg == AUTONEG_ENABLE)
		RTL_W32(TBICSR, reg | TBINwEnable | TBINwRestart);
	else {
		netif_warn(tp, link, dev,
			   "incorrect speed setting refused in TBI mode\n");
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int rtl8169_set_speed_xmii(struct ether *dev,
				  uint8_t autoneg, uint16_t speed, uint8_t duplex,
				  uint32_t adv)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int giga_ctrl, bmcr;
	int rc = -EINVAL;

	rtl_writephy(tp, 0x1f, 0x0000);

	if (autoneg == AUTONEG_ENABLE) {
		int auto_nego;

		auto_nego = rtl_readphy(tp, MII_ADVERTISE);
		auto_nego &= ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
				ADVERTISE_100HALF | ADVERTISE_100FULL);

		if (adv & ADVERTISED_10baseT_Half)
			auto_nego |= ADVERTISE_10HALF;
		if (adv & ADVERTISED_10baseT_Full)
			auto_nego |= ADVERTISE_10FULL;
		if (adv & ADVERTISED_100baseT_Half)
			auto_nego |= ADVERTISE_100HALF;
		if (adv & ADVERTISED_100baseT_Full)
			auto_nego |= ADVERTISE_100FULL;

		auto_nego |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;

		giga_ctrl = rtl_readphy(tp, MII_CTRL1000);
		giga_ctrl &= ~(ADVERTISE_1000FULL | ADVERTISE_1000HALF);

		/* The 8100e/8101e/8102e do Fast Ethernet only. */
		if (tp->mii.supports_gmii) {
			if (adv & ADVERTISED_1000baseT_Half)
				giga_ctrl |= ADVERTISE_1000HALF;
			if (adv & ADVERTISED_1000baseT_Full)
				giga_ctrl |= ADVERTISE_1000FULL;
		} else if (adv & (ADVERTISED_1000baseT_Half |
				  ADVERTISED_1000baseT_Full)) {
			netif_info(tp, link, dev,
				   "PHY does not support 1000Mbps\n");
			goto out;
		}

		bmcr = BMCR_ANENABLE | BMCR_ANRESTART;

		rtl_writephy(tp, MII_ADVERTISE, auto_nego);
		rtl_writephy(tp, MII_CTRL1000, giga_ctrl);
	} else {
		giga_ctrl = 0;

		if (speed == SPEED_10)
			bmcr = 0;
		else if (speed == SPEED_100)
			bmcr = BMCR_SPEED100;
		else
			goto out;

		if (duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;
	}

	rtl_writephy(tp, MII_BMCR, bmcr);

	if (tp->mac_version == RTL_GIGA_MAC_VER_02 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_03) {
		if ((speed == SPEED_100) && (autoneg != AUTONEG_ENABLE)) {
			rtl_writephy(tp, 0x17, 0x2138);
			rtl_writephy(tp, 0x0e, 0x0260);
		} else {
			rtl_writephy(tp, 0x17, 0x2108);
			rtl_writephy(tp, 0x0e, 0x0000);
		}
	}

	rc = 0;
out:
	return rc;
}

static int rtl8169_set_speed(struct ether *dev,
			     uint8_t autoneg, uint16_t speed, uint8_t duplex,
			     uint32_t advertising)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	ret = tp->set_speed(dev, autoneg, speed, duplex, advertising);
	if (ret < 0)
		goto out;

	dev->mbps = speed;

	if (netif_running(dev) && (autoneg == AUTONEG_ENABLE) &&
	    (advertising & ADVERTISED_1000baseT_Full) &&
	    !pci_is_pcie(tp->pci_dev)) {
		mod_timer(&tp->timer, jiffies + RTL8169_PHY_TIMEOUT);
	}
out:
	return ret;
}

#if 0 // AKAROS_PORT
static int rtl8169_set_settings(struct ether *dev, struct ethtool_cmd *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int ret;

	del_timer_sync(&tp->timer);

	rtl_lock_work(tp);
	ret = rtl8169_set_speed(dev, cmd->autoneg, ethtool_cmd_speed(cmd),
				cmd->duplex, cmd->advertising);
	rtl_unlock_work(tp);

	return ret;
}
#endif

static netdev_features_t rtl8169_fix_features(struct ether *dev,
	netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (dev->mtu > TD_MSS_MAX)
		features &= ~NETIF_F_ALL_TSO;

	if (dev->mtu > JUMBO_1K &&
	    !rtl_chip_infos[tp->mac_version].jumbo_tx_csum)
		features &= ~NETIF_F_IP_CSUM;

	return features;
}

static void __rtl8169_set_features(struct ether *dev,
				   netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t rx_config;

	rx_config = RTL_R32(RxConfig);
	if (features & NETIF_F_RXALL)
		rx_config |= (AcceptErr | AcceptRunt);
	else
		rx_config &= ~(AcceptErr | AcceptRunt);

	RTL_W32(RxConfig, rx_config);

	if (features & NETIF_F_RXCSUM)
		tp->cp_cmd |= RxChkSum;
	else
		tp->cp_cmd &= ~RxChkSum;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		tp->cp_cmd |= RxVlan;
	else
		tp->cp_cmd &= ~RxVlan;

	tp->cp_cmd |= RTL_R16(CPlusCmd) & ~(RxVlan | RxChkSum);

	RTL_W16(CPlusCmd, tp->cp_cmd);
	RTL_R16(CPlusCmd);
}

static int rtl8169_set_features(struct ether *dev,
				netdev_features_t features)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	features &= NETIF_F_RXALL | NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_RX;

	rtl_lock_work(tp);
	if (features ^ dev->feat)
		__rtl8169_set_features(dev, features);
	rtl_unlock_work(tp);

	return 0;
}


static inline uint32_t rtl8169_tx_vlan_tag(struct block *bp)
{
#if 0 // AKAROS_PORT
	return (skb_vlan_tag_present(skb)) ?
		TxVlanTag | swab16(skb_vlan_tag_get(skb)) : 0x00;
#else
	return 0x00;
#endif
}

static void rtl8169_rx_vlan_tag(struct RxDesc *desc, struct sk_buff *skb)
{
	uint32_t opts2 = le32_to_cpu(desc->opts2);

#if 0 // AKAROS_PORT
	if (opts2 & RxVlanTag)
		__vlan_hwaccel_put_tag(skb, cpu_to_be16(ETH_P_8021Q),
				       swab16(opts2 & 0xffff));
#endif
}

#if 0 // AKAROS_PORT
static int rtl8169_get_link_ksettings_tbi(struct ether *dev,
					  struct ethtool_link_ksettings *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t status;
	uint32_t supported, advertising;

	supported =
		SUPPORTED_1000baseT_Full | SUPPORTED_Autoneg | SUPPORTED_FIBRE;
	cmd->base.port = PORT_FIBRE;

	status = RTL_R32(TBICSR);
	advertising = (status & TBINwEnable) ?  ADVERTISED_Autoneg : 0;
	cmd->base.autoneg = !!(status & TBINwEnable);

	cmd->base.speed = SPEED_1000;
	cmd->base.duplex = DUPLEX_FULL; /* Always set */

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int rtl8169_get_link_ksettings_xmii(struct ether *dev,
					   struct ethtool_link_ksettings *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	mii_ethtool_get_link_ksettings(&tp->mii, cmd);

	return 0;
}

static int rtl8169_get_link_ksettings(struct ether *dev,
				      struct ethtool_link_ksettings *cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	int rc;

	rtl_lock_work(tp);
	rc = tp->get_link_ksettings(dev, cmd);
	rtl_unlock_work(tp);

	return rc;
}

static void rtl8169_get_regs(struct ether *dev, struct ethtool_regs *regs,
			     void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	uint32_t __iomem *data = tp->mmio_addr;
	uint32_t *dw = p;
	int i;

	rtl_lock_work(tp);
	for (i = 0; i < R8169_REGS_SIZE; i += 4)
		memcpy_fromio(dw++, data++, 4);
	rtl_unlock_work(tp);
}
#endif

static uint32_t rtl8169_get_msglevel(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void rtl8169_set_msglevel(struct ether *dev, uint32_t value)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static const char rtl8169_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"unicast",
	"broadcast",
	"multicast",
	"tx_aborted",
	"tx_underrun",
};

static int rtl8169_get_sset_count(struct ether *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rtl8169_gstrings);
	default:
		return -EOPNOTSUPP;
	}
}

DECLARE_RTL_COND(rtl_counters_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(CounterAddrLow) & (CounterReset | CounterDump);
}

static bool rtl8169_do_counters(struct ether *dev, uint32_t counter_cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	dma_addr_t paddr = tp->counters_phys_addr;
	uint32_t cmd;
	bool ret;

	RTL_W32(CounterAddrHigh, (uint64_t)paddr >> 32);
	cmd = (uint64_t)paddr & DMA_BIT_MASK(32);
	RTL_W32(CounterAddrLow, cmd);
	RTL_W32(CounterAddrLow, cmd | counter_cmd);

	ret = rtl_udelay_loop_wait_low(tp, &rtl_counters_cond, 10, 1000);

	RTL_W32(CounterAddrLow, 0);
	RTL_W32(CounterAddrHigh, 0);

	return ret;
}

static bool rtl8169_reset_counters(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	/*
	 * Versions prior to RTL_GIGA_MAC_VER_19 don't support resetting the
	 * tally counters.
	 */
	if (tp->mac_version < RTL_GIGA_MAC_VER_19)
		return true;

	return rtl8169_do_counters(dev, CounterReset);
}

static bool rtl8169_update_counters(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	/*
	 * Some chips are unable to dump tally counters when the receiver
	 * is disabled.
	 */
	if ((RTL_R8(ChipCmd) & CmdRxEnb) == 0)
		return true;

	return rtl8169_do_counters(dev, CounterDump);
}

static bool rtl8169_init_counter_offsets(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct rtl8169_counters *counters = tp->counters;
	bool ret = false;

	/*
	 * rtl8169_init_counter_offsets is called from rtl_open.  On chip
	 * versions prior to RTL_GIGA_MAC_VER_19 the tally counters are only
	 * reset by a power cycle, while the counter values collected by the
	 * driver are reset at every driver unload/load cycle.
	 *
	 * To make sure the HW values returned by @get_stats64 match the SW
	 * values, we collect the initial values at first open(*) and use them
	 * as offsets to normalize the values returned by @get_stats64.
	 *
	 * (*) We can't call rtl8169_init_counter_offsets from rtl_init_one
	 * for the reason stated in rtl8169_update_counters; CmdRxEnb is only
	 * set at open time by rtl_hw_start.
	 */

	if (tp->tc_offset.inited)
		return true;

	/* If both, reset and update fail, propagate to caller. */
	if (rtl8169_reset_counters(dev))
		ret = true;

	if (rtl8169_update_counters(dev))
		ret = true;

	tp->tc_offset.tx_errors = counters->tx_errors;
	tp->tc_offset.tx_multi_collision = counters->tx_multi_collision;
	tp->tc_offset.tx_aborted = counters->tx_aborted;
	tp->tc_offset.inited = true;

	return ret;
}

#if 0 // AKAROS_PORT
static void rtl8169_get_ethtool_stats(struct ether *dev,
				      struct ethtool_stats *stats,
				      uint64_t *data)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = &tp->pci_dev->device;
	struct rtl8169_counters *counters = tp->counters;

	ASSERT_RTNL();

	pm_runtime_get_noresume(d);

	if (pm_runtime_active(d))
		rtl8169_update_counters(dev);

	pm_runtime_put_noidle(d);

	data[0] = le64_to_cpu(counters->tx_packets);
	data[1] = le64_to_cpu(counters->rx_packets);
	data[2] = le64_to_cpu(counters->tx_errors);
	data[3] = le32_to_cpu(counters->rx_errors);
	data[4] = le16_to_cpu(counters->rx_missed);
	data[5] = le16_to_cpu(counters->align_errors);
	data[6] = le32_to_cpu(counters->tx_one_collision);
	data[7] = le32_to_cpu(counters->tx_multi_collision);
	data[8] = le64_to_cpu(counters->rx_unicast);
	data[9] = le64_to_cpu(counters->rx_broadcast);
	data[10] = le32_to_cpu(counters->rx_multicast);
	data[11] = le16_to_cpu(counters->tx_aborted);
	data[12] = le16_to_cpu(counters->tx_underun);
}
#endif

static void rtl8169_get_strings(struct ether *dev, uint32_t stringset,
				uint8_t *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *rtl8169_gstrings, sizeof(rtl8169_gstrings));
		break;
	}
}

static int rtl8169_nway_reset(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	return mii_nway_restart(&tp->mii);
}

#if 0 // AKAROS_PORT
static const struct ethtool_ops rtl8169_ethtool_ops = {
	.get_drvinfo		= rtl8169_get_drvinfo,
	.get_regs_len		= rtl8169_get_regs_len,
	.get_link		= ethtool_op_get_link,
	.set_settings		= rtl8169_set_settings,
	.get_msglevel		= rtl8169_get_msglevel,
	.set_msglevel		= rtl8169_set_msglevel,
	.get_regs		= rtl8169_get_regs,
	.get_wol		= rtl8169_get_wol,
	.set_wol		= rtl8169_set_wol,
	.get_strings		= rtl8169_get_strings,
	.get_sset_count		= rtl8169_get_sset_count,
	.get_ethtool_stats	= rtl8169_get_ethtool_stats,
	.get_ts_info		= ethtool_op_get_ts_info,
	.nway_reset		= rtl8169_nway_reset,
	.get_link_ksettings	= rtl8169_get_link_ksettings,
};
#endif

static void rtl8169_get_mac_version(struct rtl8169_private *tp,
				    struct ether *dev,
				    uint8_t default_version)
{
	void __iomem *ioaddr = tp->mmio_addr;
	/*
	 * The driver currently handles the 8168Bf and the 8168Be identically
	 * but they can be identified more specifically through the test below
	 * if needed:
	 *
	 * (RTL_R32(TxConfig) & 0x700000) == 0x500000 ? 8168Bf : 8168Be
	 *
	 * Same thing for the 8101Eb and the 8101Ec:
	 *
	 * (RTL_R32(TxConfig) & 0x700000) == 0x200000 ? 8101Eb : 8101Ec
	 */
	static const struct rtl_mac_info {
		uint32_t mask;
		uint32_t val;
		int mac_version;
	} mac_info[] = {
		/* 8168EP family. */
		{ 0x7cf00000, 0x50200000,	RTL_GIGA_MAC_VER_51 },
		{ 0x7cf00000, 0x50100000,	RTL_GIGA_MAC_VER_50 },
		{ 0x7cf00000, 0x50000000,	RTL_GIGA_MAC_VER_49 },

		/* 8168H family. */
		{ 0x7cf00000, 0x54100000,	RTL_GIGA_MAC_VER_46 },
		{ 0x7cf00000, 0x54000000,	RTL_GIGA_MAC_VER_45 },

		/* 8168G family. */
		{ 0x7cf00000, 0x5c800000,	RTL_GIGA_MAC_VER_44 },
		{ 0x7cf00000, 0x50900000,	RTL_GIGA_MAC_VER_42 },
		{ 0x7cf00000, 0x4c100000,	RTL_GIGA_MAC_VER_41 },
		{ 0x7cf00000, 0x4c000000,	RTL_GIGA_MAC_VER_40 },

		/* 8168F family. */
		{ 0x7c800000, 0x48800000,	RTL_GIGA_MAC_VER_38 },
		{ 0x7cf00000, 0x48100000,	RTL_GIGA_MAC_VER_36 },
		{ 0x7cf00000, 0x48000000,	RTL_GIGA_MAC_VER_35 },

		/* 8168E family. */
		{ 0x7c800000, 0x2c800000,	RTL_GIGA_MAC_VER_34 },
		{ 0x7cf00000, 0x2c200000,	RTL_GIGA_MAC_VER_33 },
		{ 0x7cf00000, 0x2c100000,	RTL_GIGA_MAC_VER_32 },
		{ 0x7c800000, 0x2c000000,	RTL_GIGA_MAC_VER_33 },

		/* 8168D family. */
		{ 0x7cf00000, 0x28300000,	RTL_GIGA_MAC_VER_26 },
		{ 0x7cf00000, 0x28100000,	RTL_GIGA_MAC_VER_25 },
		{ 0x7c800000, 0x28000000,	RTL_GIGA_MAC_VER_26 },

		/* 8168DP family. */
		{ 0x7cf00000, 0x28800000,	RTL_GIGA_MAC_VER_27 },
		{ 0x7cf00000, 0x28a00000,	RTL_GIGA_MAC_VER_28 },
		{ 0x7cf00000, 0x28b00000,	RTL_GIGA_MAC_VER_31 },

		/* 8168C family. */
		{ 0x7cf00000, 0x3cb00000,	RTL_GIGA_MAC_VER_24 },
		{ 0x7cf00000, 0x3c900000,	RTL_GIGA_MAC_VER_23 },
		{ 0x7cf00000, 0x3c800000,	RTL_GIGA_MAC_VER_18 },
		{ 0x7c800000, 0x3c800000,	RTL_GIGA_MAC_VER_24 },
		{ 0x7cf00000, 0x3c000000,	RTL_GIGA_MAC_VER_19 },
		{ 0x7cf00000, 0x3c200000,	RTL_GIGA_MAC_VER_20 },
		{ 0x7cf00000, 0x3c300000,	RTL_GIGA_MAC_VER_21 },
		{ 0x7cf00000, 0x3c400000,	RTL_GIGA_MAC_VER_22 },
		{ 0x7c800000, 0x3c000000,	RTL_GIGA_MAC_VER_22 },

		/* 8168B family. */
		{ 0x7cf00000, 0x38000000,	RTL_GIGA_MAC_VER_12 },
		{ 0x7cf00000, 0x38500000,	RTL_GIGA_MAC_VER_17 },
		{ 0x7c800000, 0x38000000,	RTL_GIGA_MAC_VER_17 },
		{ 0x7c800000, 0x30000000,	RTL_GIGA_MAC_VER_11 },

		/* 8101 family. */
		{ 0x7cf00000, 0x44900000,	RTL_GIGA_MAC_VER_39 },
		{ 0x7c800000, 0x44800000,	RTL_GIGA_MAC_VER_39 },
		{ 0x7c800000, 0x44000000,	RTL_GIGA_MAC_VER_37 },
		{ 0x7cf00000, 0x40b00000,	RTL_GIGA_MAC_VER_30 },
		{ 0x7cf00000, 0x40a00000,	RTL_GIGA_MAC_VER_30 },
		{ 0x7cf00000, 0x40900000,	RTL_GIGA_MAC_VER_29 },
		{ 0x7c800000, 0x40800000,	RTL_GIGA_MAC_VER_30 },
		{ 0x7cf00000, 0x34a00000,	RTL_GIGA_MAC_VER_09 },
		{ 0x7cf00000, 0x24a00000,	RTL_GIGA_MAC_VER_09 },
		{ 0x7cf00000, 0x34900000,	RTL_GIGA_MAC_VER_08 },
		{ 0x7cf00000, 0x24900000,	RTL_GIGA_MAC_VER_08 },
		{ 0x7cf00000, 0x34800000,	RTL_GIGA_MAC_VER_07 },
		{ 0x7cf00000, 0x24800000,	RTL_GIGA_MAC_VER_07 },
		{ 0x7cf00000, 0x34000000,	RTL_GIGA_MAC_VER_13 },
		{ 0x7cf00000, 0x34300000,	RTL_GIGA_MAC_VER_10 },
		{ 0x7cf00000, 0x34200000,	RTL_GIGA_MAC_VER_16 },
		{ 0x7c800000, 0x34800000,	RTL_GIGA_MAC_VER_09 },
		{ 0x7c800000, 0x24800000,	RTL_GIGA_MAC_VER_09 },
		{ 0x7c800000, 0x34000000,	RTL_GIGA_MAC_VER_16 },
		/* FIXME: where did these entries come from ? -- FR */
		{ 0xfc800000, 0x38800000,	RTL_GIGA_MAC_VER_15 },
		{ 0xfc800000, 0x30800000,	RTL_GIGA_MAC_VER_14 },

		/* 8110 family. */
		{ 0xfc800000, 0x98000000,	RTL_GIGA_MAC_VER_06 },
		{ 0xfc800000, 0x18000000,	RTL_GIGA_MAC_VER_05 },
		{ 0xfc800000, 0x10000000,	RTL_GIGA_MAC_VER_04 },
		{ 0xfc800000, 0x04000000,	RTL_GIGA_MAC_VER_03 },
		{ 0xfc800000, 0x00800000,	RTL_GIGA_MAC_VER_02 },
		{ 0xfc800000, 0x00000000,	RTL_GIGA_MAC_VER_01 },

		/* Catch-all */
		{ 0x00000000, 0x00000000,	RTL_GIGA_MAC_NONE   }
	};
	const struct rtl_mac_info *p = mac_info;
	uint32_t reg;

	reg = RTL_R32(TxConfig);
	while ((reg & p->mask) != p->val)
		p++;
	tp->mac_version = p->mac_version;

	if (tp->mac_version == RTL_GIGA_MAC_NONE) {
		netif_notice(tp, probe, dev,
			     "unknown MAC, using family default\n");
		tp->mac_version = default_version;
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_42) {
		tp->mac_version = tp->mii.supports_gmii ?
				  RTL_GIGA_MAC_VER_42 :
				  RTL_GIGA_MAC_VER_43;
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_45) {
		tp->mac_version = tp->mii.supports_gmii ?
				  RTL_GIGA_MAC_VER_45 :
				  RTL_GIGA_MAC_VER_47;
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_46) {
		tp->mac_version = tp->mii.supports_gmii ?
				  RTL_GIGA_MAC_VER_46 :
				  RTL_GIGA_MAC_VER_48;
	}
}

static void rtl8169_print_mac_version(struct rtl8169_private *tp)
{
	dprintk("mac_version = 0x%02x\n", tp->mac_version);
}

struct phy_reg {
	uint16_t reg;
	uint16_t val;
};

static void rtl_writephy_batch(struct rtl8169_private *tp,
			       const struct phy_reg *regs, int len)
{
	while (len-- > 0) {
		rtl_writephy(tp, regs->reg, regs->val);
		regs++;
	}
}

#define PHY_READ		0x00000000
#define PHY_DATA_OR		0x10000000
#define PHY_DATA_AND		0x20000000
#define PHY_BJMPN		0x30000000
#define PHY_MDIO_CHG		0x40000000
#define PHY_CLEAR_READCOUNT	0x70000000
#define PHY_WRITE		0x80000000
#define PHY_READCOUNT_EQ_SKIP	0x90000000
#define PHY_COMP_EQ_SKIPN	0xa0000000
#define PHY_COMP_NEQ_SKIPN	0xb0000000
#define PHY_WRITE_PREVIOUS	0xc0000000
#define PHY_SKIPN		0xd0000000
#define PHY_DELAY_MS		0xe0000000

struct fw_info {
	uint32_t	magic;
	char	version[RTL_VER_SIZE];
	__le32	fw_start;
	__le32	fw_len;
	uint8_t	chksum;
} __packed;

#define FW_OPCODE_SIZE	sizeof(typeof(*((struct rtl_fw_phy_action *)0)->code))

static bool rtl_fw_format_ok(struct rtl8169_private *tp, struct rtl_fw *rtl_fw)
{
	const struct firmware *fw = rtl_fw->fw;
	struct fw_info *fw_info = (struct fw_info *)fw->data;
	struct rtl_fw_phy_action *pa = &rtl_fw->phy_action;
	char *version = rtl_fw->version;
	bool rc = false;

	if (fw->size < FW_OPCODE_SIZE)
		goto out;

	if (!fw_info->magic) {
		size_t i, size, start;
		uint8_t checksum = 0;

		if (fw->size < sizeof(*fw_info))
			goto out;

		for (i = 0; i < fw->size; i++)
			checksum += fw->data[i];
		if (checksum != 0)
			goto out;

		start = le32_to_cpu(fw_info->fw_start);
		if (start > fw->size)
			goto out;

		size = le32_to_cpu(fw_info->fw_len);
		if (size > (fw->size - start) / FW_OPCODE_SIZE)
			goto out;

		memcpy(version, fw_info->version, RTL_VER_SIZE);

		pa->code = (__le32 *)(fw->data + start);
		pa->size = size;
	} else {
		if (fw->size % FW_OPCODE_SIZE)
			goto out;

		strlcpy(version, rtl_lookup_firmware_name(tp), RTL_VER_SIZE);

		pa->code = (__le32 *)fw->data;
		pa->size = fw->size / FW_OPCODE_SIZE;
	}
	version[RTL_VER_SIZE - 1] = 0;

	rc = true;
out:
	return rc;
}

static bool rtl_fw_data_ok(struct rtl8169_private *tp, struct ether *dev,
			   struct rtl_fw_phy_action *pa)
{
	bool rc = false;
	size_t index;

	for (index = 0; index < pa->size; index++) {
		uint32_t action = le32_to_cpu(pa->code[index]);
		uint32_t regno = (action & 0x0fff0000) >> 16;

		switch(action & 0xf0000000) {
		case PHY_READ:
		case PHY_DATA_OR:
		case PHY_DATA_AND:
		case PHY_MDIO_CHG:
		case PHY_CLEAR_READCOUNT:
		case PHY_WRITE:
		case PHY_WRITE_PREVIOUS:
		case PHY_DELAY_MS:
			break;

		case PHY_BJMPN:
			if (regno > index) {
				netif_err(tp, ifup, tp->dev,
					  "Out of range of firmware\n");
				goto out;
			}
			break;
		case PHY_READCOUNT_EQ_SKIP:
			if (index + 2 >= pa->size) {
				netif_err(tp, ifup, tp->dev,
					  "Out of range of firmware\n");
				goto out;
			}
			break;
		case PHY_COMP_EQ_SKIPN:
		case PHY_COMP_NEQ_SKIPN:
		case PHY_SKIPN:
			if (index + 1 + regno >= pa->size) {
				netif_err(tp, ifup, tp->dev,
					  "Out of range of firmware\n");
				goto out;
			}
			break;

		default:
			netif_err(tp, ifup, tp->dev,
				  "Invalid action 0x%08x\n", action);
			goto out;
		}
	}
	rc = true;
out:
	return rc;
}

static int rtl_check_firmware(struct rtl8169_private *tp, struct rtl_fw *rtl_fw)
{
	struct ether *dev = tp->dev;
	int rc = -EINVAL;

	if (!rtl_fw_format_ok(tp, rtl_fw)) {
		netif_err(tp, ifup, dev, "invalid firmware\n");
		goto out;
	}

	if (rtl_fw_data_ok(tp, dev, &rtl_fw->phy_action))
		rc = 0;
out:
	return rc;
}

static void rtl_phy_write_fw(struct rtl8169_private *tp, struct rtl_fw *rtl_fw)
{
	struct rtl_fw_phy_action *pa = &rtl_fw->phy_action;
	struct mdio_ops org, *ops = &tp->mdio_ops;
	uint32_t predata, count;
	size_t index;

	predata = count = 0;
	org.write = ops->write;
	org.read = ops->read;

	for (index = 0; index < pa->size; ) {
		uint32_t action = le32_to_cpu(pa->code[index]);
		uint32_t data = action & 0x0000ffff;
		uint32_t regno = (action & 0x0fff0000) >> 16;

		if (!action)
			break;

		switch(action & 0xf0000000) {
		case PHY_READ:
			predata = rtl_readphy(tp, regno);
			count++;
			index++;
			break;
		case PHY_DATA_OR:
			predata |= data;
			index++;
			break;
		case PHY_DATA_AND:
			predata &= data;
			index++;
			break;
		case PHY_BJMPN:
			index -= regno;
			break;
		case PHY_MDIO_CHG:
			if (data == 0) {
				ops->write = org.write;
				ops->read = org.read;
			} else if (data == 1) {
				ops->write = mac_mcu_write;
				ops->read = mac_mcu_read;
			}

			index++;
			break;
		case PHY_CLEAR_READCOUNT:
			count = 0;
			index++;
			break;
		case PHY_WRITE:
			rtl_writephy(tp, regno, data);
			index++;
			break;
		case PHY_READCOUNT_EQ_SKIP:
			index += (count == data) ? 2 : 1;
			break;
		case PHY_COMP_EQ_SKIPN:
			if (predata == data)
				index += regno;
			index++;
			break;
		case PHY_COMP_NEQ_SKIPN:
			if (predata != data)
				index += regno;
			index++;
			break;
		case PHY_WRITE_PREVIOUS:
			rtl_writephy(tp, regno, predata);
			index++;
			break;
		case PHY_SKIPN:
			index += regno + 1;
			break;
		case PHY_DELAY_MS:
			mdelay(data);
			index++;
			break;

		default:
			panic("BUG");
		}
	}

	ops->write = org.write;
	ops->read = org.read;
}

static void rtl_release_firmware(struct rtl8169_private *tp)
{
	if (!IS_ERR_OR_NULL(tp->rtl_fw)) {
		release_firmware(tp->rtl_fw->fw);
		kfree(tp->rtl_fw);
	}
	tp->rtl_fw = RTL_FIRMWARE_UNKNOWN;
}

static void rtl_apply_firmware(struct rtl8169_private *tp)
{
	struct rtl_fw *rtl_fw = tp->rtl_fw;

	/* TODO: release firmware once rtl_phy_write_fw signals failures. */
	if (!IS_ERR_OR_NULL(rtl_fw))
		rtl_phy_write_fw(tp, rtl_fw);
}

static void rtl_apply_firmware_cond(struct rtl8169_private *tp, uint8_t reg,
				    uint16_t val)
{
	if (rtl_readphy(tp, reg) != val)
		netif_warn(tp, hw, tp->dev, "chipset not ready for firmware\n");
	else
		rtl_apply_firmware(tp);
}

static void rtl8169s_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x06, 0x006e },
		{ 0x08, 0x0708 },
		{ 0x15, 0x4000 },
		{ 0x18, 0x65c7 },

		{ 0x1f, 0x0001 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x0000 },

		{ 0x03, 0xff41 },
		{ 0x02, 0xdf60 },
		{ 0x01, 0x0140 },
		{ 0x00, 0x0077 },
		{ 0x04, 0x7800 },
		{ 0x04, 0x7000 },

		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf0f9 },
		{ 0x04, 0x9800 },
		{ 0x04, 0x9000 },

		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xa000 },

		{ 0x03, 0xff41 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x0140 },
		{ 0x00, 0x00bb },
		{ 0x04, 0xb800 },
		{ 0x04, 0xb000 },

		{ 0x03, 0xdf41 },
		{ 0x02, 0xdc60 },
		{ 0x01, 0x6340 },
		{ 0x00, 0x007d },
		{ 0x04, 0xd800 },
		{ 0x04, 0xd000 },

		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x100a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0xf000 },

		{ 0x1f, 0x0000 },
		{ 0x0b, 0x0000 },
		{ 0x00, 0x9200 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8169sb_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0002 },
		{ 0x01, 0x90d0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8169scd_hw_phy_config_quirk(struct rtl8169_private *tp)
{
	struct pci_device *pdev = tp->pci_dev;

	if ((pci_get_subvendor(pdev) != PCI_VENDOR_ID_GIGABYTE) ||
	    (pci_get_subdevice(pdev) != 0xe000))
		return;

	rtl_writephy(tp, 0x1f, 0x0001);
	rtl_writephy(tp, 0x10, 0xf01b);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8169scd_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x04, 0x0000 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x9000 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0xa000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x10, 0xf41b },
		{ 0x14, 0xfb54 },
		{ 0x18, 0xf5c7 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl8169scd_hw_phy_config_quirk(tp);
}

static void rtl8169sce_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x04, 0x0000 },
		{ 0x03, 0x00a1 },
		{ 0x02, 0x0008 },
		{ 0x01, 0x0120 },
		{ 0x00, 0x1000 },
		{ 0x04, 0x0800 },
		{ 0x04, 0x9000 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0xa000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0xff95 },
		{ 0x00, 0xba00 },
		{ 0x04, 0xa800 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x0b, 0x8480 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x18, 0x67c7 },
		{ 0x04, 0x2000 },
		{ 0x03, 0x002f },
		{ 0x02, 0x4360 },
		{ 0x01, 0x0109 },
		{ 0x00, 0x3022 },
		{ 0x04, 0x2800 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168bb_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x10, 0xf41b },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0001);
	rtl_patchphy(tp, 0x16, 1 << 0);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168bef_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x10, 0xf41b },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168cp_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0000 },
		{ 0x1d, 0x0f00 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x1ec8 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168cp_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168c_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x1f, 0x0002 },
		{ 0x00, 0x88d4 },
		{ 0x01, 0x82b1 },
		{ 0x03, 0x7002 },
		{ 0x08, 0x9e30 },
		{ 0x09, 0x01f0 },
		{ 0x0a, 0x5500 },
		{ 0x0c, 0x00c8 },
		{ 0x1f, 0x0003 },
		{ 0x12, 0xc096 },
		{ 0x16, 0x000a },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x09, 0x2000 },
		{ 0x09, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x03, 0x802f },
		{ 0x02, 0x4f02 },
		{ 0x01, 0x0409 },
		{ 0x00, 0xf099 },
		{ 0x04, 0x9800 },
		{ 0x04, 0x9000 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x7eb8 },
		{ 0x06, 0x0761 },
		{ 0x1f, 0x0003 },
		{ 0x16, 0x0f0a },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl_patchphy(tp, 0x16, 1 << 0);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_3_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x12, 0x2300 },
		{ 0x1d, 0x3d98 },
		{ 0x1f, 0x0002 },
		{ 0x0c, 0x7eb8 },
		{ 0x06, 0x5461 },
		{ 0x1f, 0x0003 },
		{ 0x16, 0x0f0a },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl_patchphy(tp, 0x16, 1 << 0);
	rtl_patchphy(tp, 0x14, 1 << 5);
	rtl_patchphy(tp, 0x0d, 1 << 5);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168c_4_hw_phy_config(struct rtl8169_private *tp)
{
	rtl8168c_3_hw_phy_config(tp);
}

static void rtl8168d_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init_0[] = {
		/* Channel Estimation */
		{ 0x1f, 0x0001 },
		{ 0x06, 0x4064 },
		{ 0x07, 0x2863 },
		{ 0x08, 0x059c },
		{ 0x09, 0x26b4 },
		{ 0x0a, 0x6a19 },
		{ 0x0b, 0xdcc8 },
		{ 0x10, 0xf06d },
		{ 0x14, 0x7f68 },
		{ 0x18, 0x7fd9 },
		{ 0x1c, 0xf0ff },
		{ 0x1d, 0x3d9c },
		{ 0x1f, 0x0003 },
		{ 0x12, 0xf49f },
		{ 0x13, 0x070b },
		{ 0x1a, 0x05ad },
		{ 0x14, 0x94c0 },

		/*
		 * Tx Error Issue
		 * Enhance line driver power
		 */
		{ 0x1f, 0x0002 },
		{ 0x06, 0x5561 },
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8332 },
		{ 0x06, 0x5561 },

		/*
		 * Can not link to 1Gbps with bad cable
		 * Decrease SNR threshold form 21.07dB to 19.04dB
		 */
		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0000 },
		{ 0x0d, 0xf880 }
	};

	rtl_writephy_batch(tp, phy_reg_init_0, ARRAY_SIZE(phy_reg_init_0));

	/*
	 * Rx Error Issue
	 * Fine Tune Switching regulator parameter
	 */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x0b, 0x0010, 0x00ef);
	rtl_w0w1_phy(tp, 0x0c, 0xa200, 0x5d00);

	if (rtl8168d_efuse_read(tp, 0x01) == 0xb1) {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x669a },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x669a },
			{ 0x1f, 0x0002 }
		};
		int val;

		rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

		val = rtl_readphy(tp, 0x0d);

		if ((val & 0x00ff) != 0x006c) {
			static const uint32_t set[] = {
				0x0065, 0x0066, 0x0067, 0x0068,
				0x0069, 0x006a, 0x006b, 0x006c
			};
			int i;

			rtl_writephy(tp, 0x1f, 0x0002);

			val &= 0xff00;
			for (i = 0; i < ARRAY_SIZE(set); i++)
				rtl_writephy(tp, 0x0d, val | set[i]);
		}
	} else {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x6662 },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x6662 }
		};

		rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
	}

	/* RSET couple improve */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_patchphy(tp, 0x0d, 0x0300);
	rtl_patchphy(tp, 0x0f, 0x0010);

	/* Fine tune PLL performance */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x02, 0x0100, 0x0600);
	rtl_w0w1_phy(tp, 0x03, 0x0000, 0xe000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x001b);

	rtl_apply_firmware_cond(tp, MII_EXPANSION, 0xbf00);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168d_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init_0[] = {
		/* Channel Estimation */
		{ 0x1f, 0x0001 },
		{ 0x06, 0x4064 },
		{ 0x07, 0x2863 },
		{ 0x08, 0x059c },
		{ 0x09, 0x26b4 },
		{ 0x0a, 0x6a19 },
		{ 0x0b, 0xdcc8 },
		{ 0x10, 0xf06d },
		{ 0x14, 0x7f68 },
		{ 0x18, 0x7fd9 },
		{ 0x1c, 0xf0ff },
		{ 0x1d, 0x3d9c },
		{ 0x1f, 0x0003 },
		{ 0x12, 0xf49f },
		{ 0x13, 0x070b },
		{ 0x1a, 0x05ad },
		{ 0x14, 0x94c0 },

		/*
		 * Tx Error Issue
		 * Enhance line driver power
		 */
		{ 0x1f, 0x0002 },
		{ 0x06, 0x5561 },
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8332 },
		{ 0x06, 0x5561 },

		/*
		 * Can not link to 1Gbps with bad cable
		 * Decrease SNR threshold form 21.07dB to 19.04dB
		 */
		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0000 },
		{ 0x0d, 0xf880 }
	};

	rtl_writephy_batch(tp, phy_reg_init_0, ARRAY_SIZE(phy_reg_init_0));

	if (rtl8168d_efuse_read(tp, 0x01) == 0xb1) {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x669a },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x669a },

			{ 0x1f, 0x0002 }
		};
		int val;

		rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

		val = rtl_readphy(tp, 0x0d);
		if ((val & 0x00ff) != 0x006c) {
			static const uint32_t set[] = {
				0x0065, 0x0066, 0x0067, 0x0068,
				0x0069, 0x006a, 0x006b, 0x006c
			};
			int i;

			rtl_writephy(tp, 0x1f, 0x0002);

			val &= 0xff00;
			for (i = 0; i < ARRAY_SIZE(set); i++)
				rtl_writephy(tp, 0x0d, val | set[i]);
		}
	} else {
		static const struct phy_reg phy_reg_init[] = {
			{ 0x1f, 0x0002 },
			{ 0x05, 0x2642 },
			{ 0x1f, 0x0005 },
			{ 0x05, 0x8330 },
			{ 0x06, 0x2642 }
		};

		rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
	}

	/* Fine tune PLL performance */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x02, 0x0100, 0x0600);
	rtl_w0w1_phy(tp, 0x03, 0x0000, 0xe000);

	/* Switching regulator Slew rate */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_patchphy(tp, 0x0f, 0x0017);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x001b);

	rtl_apply_firmware_cond(tp, MII_EXPANSION, 0xb300);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168d_3_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0002 },
		{ 0x10, 0x0008 },
		{ 0x0d, 0x006c },

		{ 0x1f, 0x0000 },
		{ 0x0d, 0xf880 },

		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0001 },
		{ 0x0b, 0xa4d8 },
		{ 0x09, 0x281c },
		{ 0x07, 0x2883 },
		{ 0x0a, 0x6b35 },
		{ 0x1d, 0x3da4 },
		{ 0x1c, 0xeffd },
		{ 0x14, 0x7f52 },
		{ 0x18, 0x7fc6 },
		{ 0x08, 0x0601 },
		{ 0x06, 0x4063 },
		{ 0x10, 0xf074 },
		{ 0x1f, 0x0003 },
		{ 0x13, 0x0789 },
		{ 0x12, 0xf4bd },
		{ 0x1a, 0x04fd },
		{ 0x14, 0x84b0 },
		{ 0x1f, 0x0000 },
		{ 0x00, 0x9200 },

		{ 0x1f, 0x0005 },
		{ 0x01, 0x0340 },
		{ 0x1f, 0x0001 },
		{ 0x04, 0x4000 },
		{ 0x03, 0x1d21 },
		{ 0x02, 0x0c32 },
		{ 0x01, 0x0200 },
		{ 0x00, 0x5554 },
		{ 0x04, 0x4800 },
		{ 0x04, 0x4000 },
		{ 0x04, 0xf000 },
		{ 0x03, 0xdf01 },
		{ 0x02, 0xdf20 },
		{ 0x01, 0x101a },
		{ 0x00, 0xa0ff },
		{ 0x04, 0xf800 },
		{ 0x04, 0xf000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0023 },
		{ 0x16, 0x0000 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8168d_4_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0001 },
		{ 0x17, 0x0cc0 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x002d },
		{ 0x18, 0x0040 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
	rtl_patchphy(tp, 0x0d, 1 << 5);
}

static void rtl8168e_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Enable Delay cap */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b80 },
		{ 0x06, 0xc896 },
		{ 0x1f, 0x0000 },

		/* Channel estimation fine tune */
		{ 0x1f, 0x0001 },
		{ 0x0b, 0x6c20 },
		{ 0x07, 0x2872 },
		{ 0x1c, 0xefff },
		{ 0x1f, 0x0003 },
		{ 0x14, 0x6420 },
		{ 0x1f, 0x0000 },

		/* Update PFM & 10M TX idle timer */
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x002f },
		{ 0x15, 0x1919 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0007 },
		{ 0x1e, 0x00ac },
		{ 0x18, 0x0006 },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	/* DCO enable for 10M IDLE Power */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0023);
	rtl_w0w1_phy(tp, 0x17, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* For impedance matching */
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_w0w1_phy(tp, 0x08, 0x8000, 0x7f00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0050, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0020);
	rtl_w0w1_phy(tp, 0x15, 0x0000, 0x1100);
	rtl_writephy(tp, 0x1f, 0x0006);
	rtl_writephy(tp, 0x00, 0x5a00);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0007);
	rtl_writephy(tp, 0x0e, 0x003c);
	rtl_writephy(tp, 0x0d, 0x4007);
	rtl_writephy(tp, 0x0e, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0000);
}

static void rtl_rar_exgmac_set(struct rtl8169_private *tp, uint8_t *addr)
{
	const uint16_t w[] = {
		addr[0] | (addr[1] << 8),
		addr[2] | (addr[3] << 8),
		addr[4] | (addr[5] << 8)
	};
	const struct exgmac_reg e[] = {
		{ .addr = 0xe0, ERIAR_MASK_1111, .val = w[0] | (w[1] << 16) },
		{ .addr = 0xe4, ERIAR_MASK_1111, .val = w[2] },
		{ .addr = 0xf0, ERIAR_MASK_1111, .val = w[0] << 16 },
		{ .addr = 0xf4, ERIAR_MASK_1111, .val = w[1] | (w[2] << 16) }
	};

	rtl_write_exgmac_batch(tp, e, ARRAY_SIZE(e));
}

static void rtl8168e_2_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Enable Delay cap */
		{ 0x1f, 0x0004 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x00ac },
		{ 0x18, 0x0006 },
		{ 0x1f, 0x0002 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },

		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0000 },

		/* Green Setting */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b5b },
		{ 0x06, 0x9222 },
		{ 0x05, 0x8b6d },
		{ 0x06, 0x8000 },
		{ 0x05, 0x8b76 },
		{ 0x06, 0x8000 },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	/* For 4-corner performance improve */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b80);
	rtl_w0w1_phy(tp, 0x17, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0010, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	/* improve 10M EEE waveform */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* EEE setting */
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_1111, 0x0000, 0x0003, ERIAR_EXGMAC);
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0020);
	rtl_w0w1_phy(tp, 0x15, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0002);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0007);
	rtl_writephy(tp, 0x0e, 0x003c);
	rtl_writephy(tp, 0x0d, 0x4007);
	rtl_writephy(tp, 0x0e, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0000);

	/* Green feature */
	rtl_writephy(tp, 0x1f, 0x0003);
	rtl_w0w1_phy(tp, 0x19, 0x0000, 0x0001);
	rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0400);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Broken BIOS workaround: feed GigaMAC registers with MAC address. */
	rtl_rar_exgmac_set(tp, tp->dev->ea);
}

static void rtl8168f_hw_phy_config(struct rtl8169_private *tp)
{
	/* For 4-corner performance improve */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b80);
	rtl_w0w1_phy(tp, 0x06, 0x0006, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x002d);
	rtl_w0w1_phy(tp, 0x18, 0x0010, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	/* Improve 10M EEE waveform */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b86);
	rtl_w0w1_phy(tp, 0x06, 0x0001, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168f_1_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },

		/* Modify green table for giga & fnet */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b55 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b5e },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b67 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b70 },
		{ 0x06, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0078 },
		{ 0x17, 0x0000 },
		{ 0x19, 0x00fb },
		{ 0x1f, 0x0000 },

		/* Modify green table for 10M */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b79 },
		{ 0x06, 0xaa00 },
		{ 0x1f, 0x0000 },

		/* Disable hiimpedance detection (RTCT) */
		{ 0x1f, 0x0003 },
		{ 0x01, 0x328a },
		{ 0x1f, 0x0000 }
	};

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl8168f_hw_phy_config(tp);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168f_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_apply_firmware(tp);

	rtl8168f_hw_phy_config(tp);
}

static void rtl8411_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		/* Channel estimation fine tune */
		{ 0x1f, 0x0003 },
		{ 0x09, 0xa20f },
		{ 0x1f, 0x0000 },

		/* Modify green table for giga & fnet */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b55 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b5e },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b67 },
		{ 0x06, 0x0000 },
		{ 0x05, 0x8b70 },
		{ 0x06, 0x0000 },
		{ 0x1f, 0x0000 },
		{ 0x1f, 0x0007 },
		{ 0x1e, 0x0078 },
		{ 0x17, 0x0000 },
		{ 0x19, 0x00aa },
		{ 0x1f, 0x0000 },

		/* Modify green table for 10M */
		{ 0x1f, 0x0005 },
		{ 0x05, 0x8b79 },
		{ 0x06, 0xaa00 },
		{ 0x1f, 0x0000 },

		/* Disable hiimpedance detection (RTCT) */
		{ 0x1f, 0x0003 },
		{ 0x01, 0x328a },
		{ 0x1f, 0x0000 }
	};


	rtl_apply_firmware(tp);

	rtl8168f_hw_phy_config(tp);

	/* Improve 2-pair detection performance */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x4000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	/* Modify green table for giga */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b54);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0800);
	rtl_writephy(tp, 0x05, 0x8b5d);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0800);
	rtl_writephy(tp, 0x05, 0x8a7c);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a7f);
	rtl_w0w1_phy(tp, 0x06, 0x0100, 0x0000);
	rtl_writephy(tp, 0x05, 0x8a82);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x05, 0x8a88);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* uc same-seed solution */
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* eee setting */
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0001, 0x00, 0x03, ERIAR_EXGMAC);
	rtl_writephy(tp, 0x1f, 0x0005);
	rtl_writephy(tp, 0x05, 0x8b85);
	rtl_w0w1_phy(tp, 0x06, 0x0000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x1f, 0x0007);
	rtl_writephy(tp, 0x1e, 0x0020);
	rtl_w0w1_phy(tp, 0x15, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0007);
	rtl_writephy(tp, 0x0e, 0x003c);
	rtl_writephy(tp, 0x0d, 0x4007);
	rtl_writephy(tp, 0x0e, 0x0000);
	rtl_writephy(tp, 0x0d, 0x0000);

	/* Green feature */
	rtl_writephy(tp, 0x1f, 0x0003);
	rtl_w0w1_phy(tp, 0x19, 0x0000, 0x0001);
	rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0400);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168g_1_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_apply_firmware(tp);

	rtl_writephy(tp, 0x1f, 0x0a46);
	if (rtl_readphy(tp, 0x10) & 0x0100) {
		rtl_writephy(tp, 0x1f, 0x0bcc);
		rtl_w0w1_phy(tp, 0x12, 0x0000, 0x8000);
	} else {
		rtl_writephy(tp, 0x1f, 0x0bcc);
		rtl_w0w1_phy(tp, 0x12, 0x8000, 0x0000);
	}

	rtl_writephy(tp, 0x1f, 0x0a46);
	if (rtl_readphy(tp, 0x13) & 0x0100) {
		rtl_writephy(tp, 0x1f, 0x0c41);
		rtl_w0w1_phy(tp, 0x15, 0x0002, 0x0000);
	} else {
		rtl_writephy(tp, 0x1f, 0x0c41);
		rtl_w0w1_phy(tp, 0x15, 0x0000, 0x0002);
	}

	/* Enable PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x000c, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0bcc);
	rtl_w0w1_phy(tp, 0x14, 0x0100, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x00c0, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8084);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x6000);
	rtl_w0w1_phy(tp, 0x10, 0x1003, 0x0000);

	/* EEE auto-fallback function */
	rtl_writephy(tp, 0x1f, 0x0a4b);
	rtl_w0w1_phy(tp, 0x11, 0x0004, 0x0000);

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0c42);
	rtl_w0w1_phy(tp, 0x11, 0x4000, 0x2000);

	/* Improve SWR Efficiency */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x5065);
	rtl_writephy(tp, 0x14, 0xd065);
	rtl_writephy(tp, 0x1f, 0x0bc8);
	rtl_writephy(tp, 0x11, 0x5655);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x14, 0x9065);
	rtl_writephy(tp, 0x14, 0x1065);

	/* Check ALDPS bit, disable it if enabled */
	rtl_writephy(tp, 0x1f, 0x0a43);
	if (rtl_readphy(tp, 0x10) & 0x0004)
		rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0004);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168g_2_hw_phy_config(struct rtl8169_private *tp)
{
	rtl_apply_firmware(tp);
}

static void rtl8168h_1_hw_phy_config(struct rtl8169_private *tp)
{
	uint16_t dout_tapbin;
	uint32_t data;

	rtl_apply_firmware(tp);

	/* CHN EST parameters adjust - giga master */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x809b);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0xf800);
	rtl_writephy(tp, 0x13, 0x80a2);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0xff00);
	rtl_writephy(tp, 0x13, 0x80a4);
	rtl_w0w1_phy(tp, 0x14, 0x8500, 0xff00);
	rtl_writephy(tp, 0x13, 0x809c);
	rtl_w0w1_phy(tp, 0x14, 0xbd00, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* CHN EST parameters adjust - giga slave */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80ad);
	rtl_w0w1_phy(tp, 0x14, 0x7000, 0xf800);
	rtl_writephy(tp, 0x13, 0x80b4);
	rtl_w0w1_phy(tp, 0x14, 0x5000, 0xff00);
	rtl_writephy(tp, 0x13, 0x80ac);
	rtl_w0w1_phy(tp, 0x14, 0x4000, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* CHN EST parameters adjust - fnet */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x808e);
	rtl_w0w1_phy(tp, 0x14, 0x1200, 0xff00);
	rtl_writephy(tp, 0x13, 0x8090);
	rtl_w0w1_phy(tp, 0x14, 0xe500, 0xff00);
	rtl_writephy(tp, 0x13, 0x8092);
	rtl_w0w1_phy(tp, 0x14, 0x9f00, 0xff00);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable R-tune & PGA-retune function */
	dout_tapbin = 0;
	rtl_writephy(tp, 0x1f, 0x0a46);
	data = rtl_readphy(tp, 0x13);
	data &= 3;
	data <<= 2;
	dout_tapbin |= data;
	data = rtl_readphy(tp, 0x12);
	data &= 0xc000;
	data >>= 14;
	dout_tapbin |= data;
	dout_tapbin = ~(dout_tapbin^0x08);
	dout_tapbin <<= 12;
	dout_tapbin &= 0xf000;
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x827a);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827b);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827c);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);
	rtl_writephy(tp, 0x13, 0x827d);
	rtl_w0w1_phy(tp, 0x14, dout_tapbin, 0xf000);

	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x0811);
	rtl_w0w1_phy(tp, 0x14, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a42);
	rtl_w0w1_phy(tp, 0x16, 0x0002, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable GPHY 10M */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* SAR ADC performance */
	rtl_writephy(tp, 0x1f, 0x0bca);
	rtl_w0w1_phy(tp, 0x17, 0x4000, 0x3000);
	rtl_writephy(tp, 0x1f, 0x0000);

	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x803f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8047);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x804f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8057);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x805f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x8067);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x13, 0x806f);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x3000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* disable phy pfm mode */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x0000, 0x0080);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Check ALDPS bit, disable it if enabled */
	rtl_writephy(tp, 0x1f, 0x0a43);
	if (rtl_readphy(tp, 0x10) & 0x0004)
		rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0004);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168h_2_hw_phy_config(struct rtl8169_private *tp)
{
	uint16_t ioffset_p3, ioffset_p2, ioffset_p1, ioffset_p0;
	uint16_t rlen;
	uint32_t data;

	rtl_apply_firmware(tp);

	/* CHIN EST parameter update */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x808a);
	rtl_w0w1_phy(tp, 0x14, 0x000a, 0x003f);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable R-tune & PGA-retune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x0811);
	rtl_w0w1_phy(tp, 0x14, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a42);
	rtl_w0w1_phy(tp, 0x16, 0x0002, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* enable GPHY 10M */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x0800, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	r8168_mac_ocp_write(tp, 0xdd02, 0x807d);
	data = r8168_mac_ocp_read(tp, 0xdd02);
	ioffset_p3 = ((data & 0x80)>>7);
	ioffset_p3 <<= 3;

	data = r8168_mac_ocp_read(tp, 0xdd00);
	ioffset_p3 |= ((data & (0xe000))>>13);
	ioffset_p2 = ((data & (0x1e00))>>9);
	ioffset_p1 = ((data & (0x01e0))>>5);
	ioffset_p0 = ((data & 0x0010)>>4);
	ioffset_p0 <<= 3;
	ioffset_p0 |= (data & (0x07));
	data = (ioffset_p3<<12)|(ioffset_p2<<8)|(ioffset_p1<<4)|(ioffset_p0);

	if ((ioffset_p3 != 0x0f) || (ioffset_p2 != 0x0f) ||
	    (ioffset_p1 != 0x0f) || (ioffset_p0 != 0x0f)) {
		rtl_writephy(tp, 0x1f, 0x0bcf);
		rtl_writephy(tp, 0x16, data);
		rtl_writephy(tp, 0x1f, 0x0000);
	}

	/* Modify rlen (TX LPF corner frequency) level */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	data = rtl_readphy(tp, 0x16);
	data &= 0x000f;
	rlen = 0;
	if (data > 3)
		rlen = data - 3;
	data = rlen | (rlen<<4) | (rlen<<8) | (rlen<<12);
	rtl_writephy(tp, 0x17, data);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* disable phy pfm mode */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x0000, 0x0080);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Check ALDPS bit, disable it if enabled */
	rtl_writephy(tp, 0x1f, 0x0a43);
	if (rtl_readphy(tp, 0x10) & 0x0004)
		rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0004);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168ep_1_hw_phy_config(struct rtl8169_private *tp)
{
	/* Enable PHY auto speed down */
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x000c, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* patch 10M & ALDPS */
	rtl_writephy(tp, 0x1f, 0x0bcc);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x00c0, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8084);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x6000);
	rtl_w0w1_phy(tp, 0x10, 0x1003, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Enable EEE auto-fallback function */
	rtl_writephy(tp, 0x1f, 0x0a4b);
	rtl_w0w1_phy(tp, 0x11, 0x0004, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* set rg_sel_sdm_rate */
	rtl_writephy(tp, 0x1f, 0x0c42);
	rtl_w0w1_phy(tp, 0x11, 0x4000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Check ALDPS bit, disable it if enabled */
	rtl_writephy(tp, 0x1f, 0x0a43);
	if (rtl_readphy(tp, 0x10) & 0x0004)
		rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0004);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8168ep_2_hw_phy_config(struct rtl8169_private *tp)
{
	/* patch 10M & ALDPS */
	rtl_writephy(tp, 0x1f, 0x0bcc);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x0100);
	rtl_writephy(tp, 0x1f, 0x0a44);
	rtl_w0w1_phy(tp, 0x11, 0x00c0, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8084);
	rtl_w0w1_phy(tp, 0x14, 0x0000, 0x6000);
	rtl_w0w1_phy(tp, 0x10, 0x1003, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Enable UC LPF tune function */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8012);
	rtl_w0w1_phy(tp, 0x14, 0x8000, 0x0000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Set rg_sel_sdm_rate */
	rtl_writephy(tp, 0x1f, 0x0c42);
	rtl_w0w1_phy(tp, 0x11, 0x4000, 0x2000);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Channel estimation parameters */
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80f3);
	rtl_w0w1_phy(tp, 0x14, 0x8b00, ~0x8bff);
	rtl_writephy(tp, 0x13, 0x80f0);
	rtl_w0w1_phy(tp, 0x14, 0x3a00, ~0x3aff);
	rtl_writephy(tp, 0x13, 0x80ef);
	rtl_w0w1_phy(tp, 0x14, 0x0500, ~0x05ff);
	rtl_writephy(tp, 0x13, 0x80f6);
	rtl_w0w1_phy(tp, 0x14, 0x6e00, ~0x6eff);
	rtl_writephy(tp, 0x13, 0x80ec);
	rtl_w0w1_phy(tp, 0x14, 0x6800, ~0x68ff);
	rtl_writephy(tp, 0x13, 0x80ed);
	rtl_w0w1_phy(tp, 0x14, 0x7c00, ~0x7cff);
	rtl_writephy(tp, 0x13, 0x80f2);
	rtl_w0w1_phy(tp, 0x14, 0xf400, ~0xf4ff);
	rtl_writephy(tp, 0x13, 0x80f4);
	rtl_w0w1_phy(tp, 0x14, 0x8500, ~0x85ff);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x8110);
	rtl_w0w1_phy(tp, 0x14, 0xa800, ~0xa8ff);
	rtl_writephy(tp, 0x13, 0x810f);
	rtl_w0w1_phy(tp, 0x14, 0x1d00, ~0x1dff);
	rtl_writephy(tp, 0x13, 0x8111);
	rtl_w0w1_phy(tp, 0x14, 0xf500, ~0xf5ff);
	rtl_writephy(tp, 0x13, 0x8113);
	rtl_w0w1_phy(tp, 0x14, 0x6100, ~0x61ff);
	rtl_writephy(tp, 0x13, 0x8115);
	rtl_w0w1_phy(tp, 0x14, 0x9200, ~0x92ff);
	rtl_writephy(tp, 0x13, 0x810e);
	rtl_w0w1_phy(tp, 0x14, 0x0400, ~0x04ff);
	rtl_writephy(tp, 0x13, 0x810c);
	rtl_w0w1_phy(tp, 0x14, 0x7c00, ~0x7cff);
	rtl_writephy(tp, 0x13, 0x810b);
	rtl_w0w1_phy(tp, 0x14, 0x5a00, ~0x5aff);
	rtl_writephy(tp, 0x1f, 0x0a43);
	rtl_writephy(tp, 0x13, 0x80d1);
	rtl_w0w1_phy(tp, 0x14, 0xff00, ~0xffff);
	rtl_writephy(tp, 0x13, 0x80cd);
	rtl_w0w1_phy(tp, 0x14, 0x9e00, ~0x9eff);
	rtl_writephy(tp, 0x13, 0x80d3);
	rtl_w0w1_phy(tp, 0x14, 0x0e00, ~0x0eff);
	rtl_writephy(tp, 0x13, 0x80d5);
	rtl_w0w1_phy(tp, 0x14, 0xca00, ~0xcaff);
	rtl_writephy(tp, 0x13, 0x80d7);
	rtl_w0w1_phy(tp, 0x14, 0x8400, ~0x84ff);

	/* Force PWM-mode */
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x5065);
	rtl_writephy(tp, 0x14, 0xd065);
	rtl_writephy(tp, 0x1f, 0x0bc8);
	rtl_writephy(tp, 0x12, 0x00ed);
	rtl_writephy(tp, 0x1f, 0x0bcd);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x14, 0x9065);
	rtl_writephy(tp, 0x14, 0x1065);
	rtl_writephy(tp, 0x1f, 0x0000);

	/* Check ALDPS bit, disable it if enabled */
	rtl_writephy(tp, 0x1f, 0x0a43);
	if (rtl_readphy(tp, 0x10) & 0x0004)
		rtl_w0w1_phy(tp, 0x10, 0x0000, 0x0004);

	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8102e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0003 },
		{ 0x08, 0x441d },
		{ 0x01, 0x9100 },
		{ 0x1f, 0x0000 }
	};

	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_patchphy(tp, 0x11, 1 << 12);
	rtl_patchphy(tp, 0x19, 1 << 13);
	rtl_patchphy(tp, 0x10, 1 << 15);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8105e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0005 },
		{ 0x1a, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0004 },
		{ 0x1c, 0x0000 },
		{ 0x1f, 0x0000 },

		{ 0x1f, 0x0001 },
		{ 0x15, 0x7701 },
		{ 0x1f, 0x0000 }
	};

	/* Disable ALDPS before ram code */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	kthread_usleep(1000 * 100);

	rtl_apply_firmware(tp);

	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));
}

static void rtl8402_hw_phy_config(struct rtl8169_private *tp)
{
	/* Disable ALDPS before setting firmware */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	kthread_usleep(1000 * 20);

	rtl_apply_firmware(tp);

	/* EEE setting */
	rtl_eri_write(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_writephy(tp, 0x1f, 0x0004);
	rtl_writephy(tp, 0x10, 0x401f);
	rtl_writephy(tp, 0x19, 0x7030);
	rtl_writephy(tp, 0x1f, 0x0000);
}

static void rtl8106e_hw_phy_config(struct rtl8169_private *tp)
{
	static const struct phy_reg phy_reg_init[] = {
		{ 0x1f, 0x0004 },
		{ 0x10, 0xc07f },
		{ 0x19, 0x7030 },
		{ 0x1f, 0x0000 }
	};

	/* Disable ALDPS before ram code */
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, 0x18, 0x0310);
	kthread_usleep(1000 * 100);

	rtl_apply_firmware(tp);

	rtl_eri_write(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_writephy_batch(tp, phy_reg_init, ARRAY_SIZE(phy_reg_init));

	rtl_eri_write(tp, 0x1d0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
}

static void rtl_hw_phy_config(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_print_mac_version(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_01:
		break;
	case RTL_GIGA_MAC_VER_02:
	case RTL_GIGA_MAC_VER_03:
		rtl8169s_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_04:
		rtl8169sb_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_05:
		rtl8169scd_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_06:
		rtl8169sce_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_07:
	case RTL_GIGA_MAC_VER_08:
	case RTL_GIGA_MAC_VER_09:
		rtl8102e_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_11:
		rtl8168bb_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_12:
		rtl8168bef_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_17:
		rtl8168bef_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_18:
		rtl8168cp_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_19:
		rtl8168c_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_20:
		rtl8168c_2_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_21:
		rtl8168c_3_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_22:
		rtl8168c_4_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
		rtl8168cp_2_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_25:
		rtl8168d_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_26:
		rtl8168d_2_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_27:
		rtl8168d_3_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_28:
		rtl8168d_4_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_29:
	case RTL_GIGA_MAC_VER_30:
		rtl8105e_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_31:
		/* None. */
		break;
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
		rtl8168e_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_34:
		rtl8168e_2_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_35:
		rtl8168f_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_36:
		rtl8168f_2_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_37:
		rtl8402_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_38:
		rtl8411_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_39:
		rtl8106e_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_40:
		rtl8168g_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
		rtl8168g_2_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_47:
		rtl8168h_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_48:
		rtl8168h_2_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_49:
		rtl8168ep_1_hw_phy_config(tp);
		break;
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl8168ep_2_hw_phy_config(tp);
		break;

	case RTL_GIGA_MAC_VER_41:
	default:
		break;
	}
}

static void rtl_phy_work(struct rtl8169_private *tp)
{
	struct timer_list *timer = &tp->timer;
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long timeout = RTL8169_PHY_TIMEOUT;

	assert(tp->mac_version > RTL_GIGA_MAC_VER_01);

	if (tp->phy_reset_pending(tp)) {
		/*
		 * A busy loop could burn quite a few cycles on nowadays CPU.
		 * Let's delay the execution of the timer for a few ticks.
		 */
		timeout = HZ/10;
		goto out_mod_timer;
	}

	if (tp->link_ok(ioaddr))
		return;

	netif_dbg(tp, link, tp->dev, "PHY reset until link up\n");

	tp->phy_reset_enable(tp);

out_mod_timer:
	mod_timer(timer, jiffies + timeout);
}

static void rtl_schedule_task(struct rtl8169_private *tp, enum rtl_flag flag)
{
	if (!test_and_set_bit(flag, tp->wk.flags))
		schedule_work(&tp->wk.work);
}

static void rtl8169_phy_timer(unsigned long __opaque)
{
	struct ether *dev = (struct ether *)__opaque;
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_schedule_task(tp, RTL_FLAG_TASK_PHY_PENDING);
}

static void rtl8169_release_board(struct pci_device *pdev, struct ether *dev,
				  void __iomem *ioaddr)
{
	iounmap(ioaddr);
	pci_release_regions(pdev);
	pci_clear_mwi(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}

DECLARE_RTL_COND(rtl_phy_reset_cond)
{
	return tp->phy_reset_pending(tp);
}

static void rtl8169_phy_reset(struct ether *dev, struct rtl8169_private *tp)
{
	tp->phy_reset_enable(tp);
	rtl_msleep_loop_wait_low(tp, &rtl_phy_reset_cond, 1, 100);
}

static bool rtl_tbi_enabled(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return (tp->mac_version == RTL_GIGA_MAC_VER_01) &&
	    (RTL_R8(PHYstatus) & TBI_Enable);
}

static void rtl8169_init_phy(struct ether *dev, struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_hw_phy_config(dev);

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06) {
		dprintk("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
		RTL_W8(0x82, 0x01);
	}

	pci_write_config_byte(tp->pci_dev, PCI_LATENCY_TIMER, 0x40);

	if (tp->mac_version <= RTL_GIGA_MAC_VER_06)
		pci_write_config_byte(tp->pci_dev, PCI_CACHE_LINE_SIZE, 0x08);

	if (tp->mac_version == RTL_GIGA_MAC_VER_02) {
		dprintk("Set MAC Reg C+CR Offset 0x82h = 0x01h\n");
		RTL_W8(0x82, 0x01);
		dprintk("Set PHY Reg 0x0bh = 0x00h\n");
		rtl_writephy(tp, 0x0b, 0x0000); //w 0x0b 15 0 0
	}

	rtl8169_phy_reset(dev, tp);

	rtl8169_set_speed(dev, AUTONEG_ENABLE, SPEED_1000, DUPLEX_FULL,
			  ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
			  ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
			  (tp->mii.supports_gmii ?
			   ADVERTISED_1000baseT_Half |
			   ADVERTISED_1000baseT_Full : 0));

	if (rtl_tbi_enabled(tp))
		netif_info(tp, link, dev, "TBI auto-negotiating\n");
}

static void rtl_rar_set(struct rtl8169_private *tp, uint8_t *addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_lock_work(tp);

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	RTL_W32(MAC4, addr[4] | addr[5] << 8);
	RTL_R32(MAC4);

	RTL_W32(MAC0, addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	RTL_R32(MAC0);

	if (tp->mac_version == RTL_GIGA_MAC_VER_34)
		rtl_rar_exgmac_set(tp, addr);

	RTL_W8(Cfg9346, Cfg9346_Lock);

	rtl_unlock_work(tp);
}

static int rtl_set_mac_address(struct ether *dev, void *p)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = &tp->pci_dev->device;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->ea, addr->sa_data, Eaddrlen);

	pm_runtime_get_noresume(d);

	if (pm_runtime_active(d))
		rtl_rar_set(tp, dev->ea);

	pm_runtime_put_noidle(d);

	return 0;
}

#if 0 // AKAROS_PORT
static int rtl8169_ioctl(struct ether *dev, struct ifreq *ifr, int cmd)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	return netif_running(dev) ? tp->do_ioctl(tp, data, cmd) : -ENODEV;
}

static int rtl_xmii_ioctl(struct rtl8169_private *tp,
			  struct mii_ioctl_data *data, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = 32; /* Internal PHY */
		return 0;

	case SIOCGMIIREG:
		data->val_out = rtl_readphy(tp, data->reg_num & 0x1f);
		return 0;

	case SIOCSMIIREG:
		rtl_writephy(tp, data->reg_num & 0x1f, data->val_in);
		return 0;
	}
	return -EOPNOTSUPP;
}

static int rtl_tbi_ioctl(struct rtl8169_private *tp, struct mii_ioctl_data *data, int cmd)
{
	return -EOPNOTSUPP;
}
#endif

static void rtl_disable_msi(struct pci_device *pdev,
			    struct rtl8169_private *tp)
{
	if (tp->features & RTL_FEATURE_MSI) {
/* On Akaros, this will only work if you do it before register_irq() */
#if 0 // AKAROS_PORT
		pci_disable_msi(pdev);
#endif
		tp->features &= ~RTL_FEATURE_MSI;
	}
}

static void rtl_init_mdio_ops(struct rtl8169_private *tp)
{
	struct mdio_ops *ops = &tp->mdio_ops;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_27:
		ops->write	= r8168dp_1_mdio_write;
		ops->read	= r8168dp_1_mdio_read;
		break;
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		ops->write	= r8168dp_2_mdio_write;
		ops->read	= r8168dp_2_mdio_read;
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		ops->write	= r8168g_mdio_write;
		ops->read	= r8168g_mdio_read;
		break;
	default:
		ops->write	= r8169_mdio_write;
		ops->read	= r8169_mdio_read;
		break;
	}
}

static void rtl_speed_down(struct rtl8169_private *tp)
{
	uint32_t adv;
	int lpa;

	rtl_writephy(tp, 0x1f, 0x0000);
	lpa = rtl_readphy(tp, MII_LPA);

	if (lpa & (LPA_10HALF | LPA_10FULL))
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full;
	else if (lpa & (LPA_100HALF | LPA_100FULL))
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
		      ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full;
	else
		adv = ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |
		      ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |
		      (tp->mii.supports_gmii ?
		       ADVERTISED_1000baseT_Half |
		       ADVERTISED_1000baseT_Full : 0);

	rtl8169_set_speed(tp->dev, AUTONEG_ENABLE, SPEED_1000, DUPLEX_FULL,
			  adv);
}

static void rtl_wol_suspend_quirk(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_29:
	case RTL_GIGA_MAC_VER_30:
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_39:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W32(RxConfig, RTL_R32(RxConfig) |
			AcceptBroadcast | AcceptMulticast | AcceptMyPhys);
		break;
	default:
		break;
	}
}

static bool rtl_wol_pll_power_down(struct rtl8169_private *tp)
{
	if (!(__rtl8169_get_wol(tp) & WAKE_ANY))
		return false;

	rtl_speed_down(tp);
	rtl_wol_suspend_quirk(tp);

	return true;
}

static void r810x_phy_power_down(struct rtl8169_private *tp)
{
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, MII_BMCR, BMCR_PDOWN);
}

static void r810x_phy_power_up(struct rtl8169_private *tp)
{
	rtl_writephy(tp, 0x1f, 0x0000);
	rtl_writephy(tp, MII_BMCR, BMCR_ANENABLE);
}

static void r810x_pll_power_down(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if (rtl_wol_pll_power_down(tp))
		return;

	r810x_phy_power_down(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_07:
	case RTL_GIGA_MAC_VER_08:
	case RTL_GIGA_MAC_VER_09:
	case RTL_GIGA_MAC_VER_10:
	case RTL_GIGA_MAC_VER_13:
	case RTL_GIGA_MAC_VER_16:
		break;
	default:
		RTL_W8(PMCH, RTL_R8(PMCH) & ~0x80);
		break;
	}
}

static void r810x_pll_power_up(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	r810x_phy_power_up(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_07:
	case RTL_GIGA_MAC_VER_08:
	case RTL_GIGA_MAC_VER_09:
	case RTL_GIGA_MAC_VER_10:
	case RTL_GIGA_MAC_VER_13:
	case RTL_GIGA_MAC_VER_16:
		break;
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
		RTL_W8(PMCH, RTL_R8(PMCH) | 0xc0);
		break;
	default:
		RTL_W8(PMCH, RTL_R8(PMCH) | 0x80);
		break;
	}
}

static void r8168_phy_power_up(struct rtl8169_private *tp)
{
	rtl_writephy(tp, 0x1f, 0x0000);
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
	case RTL_GIGA_MAC_VER_18:
	case RTL_GIGA_MAC_VER_19:
	case RTL_GIGA_MAC_VER_20:
	case RTL_GIGA_MAC_VER_21:
	case RTL_GIGA_MAC_VER_22:
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl_writephy(tp, 0x0e, 0x0000);
		break;
	default:
		break;
	}
	rtl_writephy(tp, MII_BMCR, BMCR_ANENABLE);
}

static void r8168_phy_power_down(struct rtl8169_private *tp)
{
	rtl_writephy(tp, 0x1f, 0x0000);
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
		rtl_writephy(tp, MII_BMCR, BMCR_ANENABLE | BMCR_PDOWN);
		break;

	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
	case RTL_GIGA_MAC_VER_18:
	case RTL_GIGA_MAC_VER_19:
	case RTL_GIGA_MAC_VER_20:
	case RTL_GIGA_MAC_VER_21:
	case RTL_GIGA_MAC_VER_22:
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
		rtl_writephy(tp, 0x0e, 0x0200);
	default:
		rtl_writephy(tp, MII_BMCR, BMCR_PDOWN);
		break;
	}
}

static void r8168_pll_power_down(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	if ((tp->mac_version == RTL_GIGA_MAC_VER_27 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_28 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_31 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_49 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_50 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_51) &&
	    r8168_check_dash(tp)) {
		return;
	}

	if ((tp->mac_version == RTL_GIGA_MAC_VER_23 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_24) &&
	    (RTL_R16(CPlusCmd) & ASF)) {
		return;
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_32 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_33)
		rtl_ephy_write(tp, 0x19, 0xff64);

	if (rtl_wol_pll_power_down(tp))
		return;

	r8168_phy_power_down(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W8(PMCH, RTL_R8(PMCH) & ~0x80);
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_49:
		rtl_w0w1_eri(tp, 0x1a8, ERIAR_MASK_1111, 0x00000000,
			     0xfc000000, ERIAR_EXGMAC);
		RTL_W8(PMCH, RTL_R8(PMCH) & ~0x80);
		break;
	}
}

static void r8168_pll_power_up(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
		RTL_W8(PMCH, RTL_R8(PMCH) | 0x80);
		break;
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W8(PMCH, RTL_R8(PMCH) | 0xc0);
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_49:
		RTL_W8(PMCH, RTL_R8(PMCH) | 0xc0);
		rtl_w0w1_eri(tp, 0x1a8, ERIAR_MASK_1111, 0xfc000000,
			     0x00000000, ERIAR_EXGMAC);
		break;
	}

	r8168_phy_power_up(tp);
}

static void rtl_generic_op(struct rtl8169_private *tp,
			   void (*op)(struct rtl8169_private *))
{
	if (op)
		op(tp);
}

static void rtl_pll_power_down(struct rtl8169_private *tp)
{
	rtl_generic_op(tp, tp->pll_power_ops.down);
}

static void rtl_pll_power_up(struct rtl8169_private *tp)
{
	rtl_generic_op(tp, tp->pll_power_ops.up);
}

static void rtl_init_pll_power_ops(struct rtl8169_private *tp)
{
	struct pll_power_ops *ops = &tp->pll_power_ops;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_07:
	case RTL_GIGA_MAC_VER_08:
	case RTL_GIGA_MAC_VER_09:
	case RTL_GIGA_MAC_VER_10:
	case RTL_GIGA_MAC_VER_16:
	case RTL_GIGA_MAC_VER_29:
	case RTL_GIGA_MAC_VER_30:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_39:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
		ops->down	= r810x_pll_power_down;
		ops->up		= r810x_pll_power_up;
		break;

	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
	case RTL_GIGA_MAC_VER_18:
	case RTL_GIGA_MAC_VER_19:
	case RTL_GIGA_MAC_VER_20:
	case RTL_GIGA_MAC_VER_21:
	case RTL_GIGA_MAC_VER_22:
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
	case RTL_GIGA_MAC_VER_31:
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		ops->down	= r8168_pll_power_down;
		ops->up		= r8168_pll_power_up;
		break;

	default:
		ops->down	= NULL;
		ops->up		= NULL;
		break;
	}
}

static void rtl_init_rxcfg(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_01:
	case RTL_GIGA_MAC_VER_02:
	case RTL_GIGA_MAC_VER_03:
	case RTL_GIGA_MAC_VER_04:
	case RTL_GIGA_MAC_VER_05:
	case RTL_GIGA_MAC_VER_06:
	case RTL_GIGA_MAC_VER_10:
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_13:
	case RTL_GIGA_MAC_VER_14:
	case RTL_GIGA_MAC_VER_15:
	case RTL_GIGA_MAC_VER_16:
	case RTL_GIGA_MAC_VER_17:
		RTL_W32(RxConfig, RX_FIFO_THRESH | RX_DMA_BURST);
		break;
	case RTL_GIGA_MAC_VER_18:
	case RTL_GIGA_MAC_VER_19:
	case RTL_GIGA_MAC_VER_20:
	case RTL_GIGA_MAC_VER_21:
	case RTL_GIGA_MAC_VER_22:
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
		RTL_W32(RxConfig, RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST);
		break;
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		RTL_W32(RxConfig, RX128_INT_EN | RX_MULTI_EN | RX_DMA_BURST | RX_EARLY_OFF);
		break;
	default:
		RTL_W32(RxConfig, RX128_INT_EN | RX_DMA_BURST);
		break;
	}
}

static void rtl8169_init_ring_indexes(struct rtl8169_private *tp)
{
	tp->dirty_tx = tp->cur_tx = tp->cur_rx = 0;
}

static void rtl_hw_jumbo_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	rtl_generic_op(tp, tp->jumbo_ops.enable);
	RTL_W8(Cfg9346, Cfg9346_Lock);
}

static void rtl_hw_jumbo_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	rtl_generic_op(tp, tp->jumbo_ops.disable);
	RTL_W8(Cfg9346, Cfg9346_Lock);
}

static void r8168c_hw_jumbo_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
	RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);
	rtl_tx_performance_tweak(tp->pci_dev, PCI_EXP_DEVCTL_READRQ_512B);
}

static void r8168c_hw_jumbo_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
	RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);
	rtl_tx_performance_tweak(tp->pci_dev, 0x5 << MAX_READ_REQUEST_SHIFT);
}

static void r8168dp_hw_jumbo_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
}

static void r8168dp_hw_jumbo_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
}

static void r8168e_hw_jumbo_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(MaxTxPacketSize, 0x3f);
	RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
	RTL_W8(Config4, RTL_R8(Config4) | 0x01);
	rtl_tx_performance_tweak(tp->pci_dev, PCI_EXP_DEVCTL_READRQ_512B);
}

static void r8168e_hw_jumbo_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(MaxTxPacketSize, 0x0c);
	RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
	RTL_W8(Config4, RTL_R8(Config4) & ~0x01);
	rtl_tx_performance_tweak(tp->pci_dev, 0x5 << MAX_READ_REQUEST_SHIFT);
}

static void r8168b_0_hw_jumbo_enable(struct rtl8169_private *tp)
{
	rtl_tx_performance_tweak(tp->pci_dev,
		PCI_EXP_DEVCTL_READRQ_512B | PCI_EXP_DEVCTL_NOSNOOP_EN);
}

static void r8168b_0_hw_jumbo_disable(struct rtl8169_private *tp)
{
	rtl_tx_performance_tweak(tp->pci_dev,
		(0x5 << MAX_READ_REQUEST_SHIFT) | PCI_EXP_DEVCTL_NOSNOOP_EN);
}

static void r8168b_1_hw_jumbo_enable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	r8168b_0_hw_jumbo_enable(tp);

	RTL_W8(Config4, RTL_R8(Config4) | (1 << 0));
}

static void r8168b_1_hw_jumbo_disable(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	r8168b_0_hw_jumbo_disable(tp);

	RTL_W8(Config4, RTL_R8(Config4) & ~(1 << 0));
}

static void rtl_init_jumbo_ops(struct rtl8169_private *tp)
{
	struct jumbo_ops *ops = &tp->jumbo_ops;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
		ops->disable	= r8168b_0_hw_jumbo_disable;
		ops->enable	= r8168b_0_hw_jumbo_enable;
		break;
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		ops->disable	= r8168b_1_hw_jumbo_disable;
		ops->enable	= r8168b_1_hw_jumbo_enable;
		break;
	case RTL_GIGA_MAC_VER_18: /* Wild guess. Needs info from Realtek. */
	case RTL_GIGA_MAC_VER_19:
	case RTL_GIGA_MAC_VER_20:
	case RTL_GIGA_MAC_VER_21: /* Wild guess. Needs info from Realtek. */
	case RTL_GIGA_MAC_VER_22:
	case RTL_GIGA_MAC_VER_23:
	case RTL_GIGA_MAC_VER_24:
	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
		ops->disable	= r8168c_hw_jumbo_disable;
		ops->enable	= r8168c_hw_jumbo_enable;
		break;
	case RTL_GIGA_MAC_VER_27:
	case RTL_GIGA_MAC_VER_28:
		ops->disable	= r8168dp_hw_jumbo_disable;
		ops->enable	= r8168dp_hw_jumbo_enable;
		break;
	case RTL_GIGA_MAC_VER_31: /* Wild guess. Needs info from Realtek. */
	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
	case RTL_GIGA_MAC_VER_34:
		ops->disable	= r8168e_hw_jumbo_disable;
		ops->enable	= r8168e_hw_jumbo_enable;
		break;

	/*
	 * No action needed for jumbo frames with 8169.
	 * No jumbo for 810x at all.
	 */
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
	default:
		ops->disable	= NULL;
		ops->enable	= NULL;
		break;
	}
}

DECLARE_RTL_COND(rtl_chipcmd_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(ChipCmd) & CmdReset;
}

static void rtl_hw_reset(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(ChipCmd, CmdReset);

	rtl_udelay_loop_wait_low(tp, &rtl_chipcmd_cond, 100, 100);
}

static void rtl_request_uncached_firmware(struct rtl8169_private *tp)
{
	struct rtl_fw *rtl_fw;
	const char *name;
	int rc = -ENOMEM;

	name = rtl_lookup_firmware_name(tp);
	if (!name)
		goto out_no_firmware;

	rtl_fw = kzmalloc(sizeof(*rtl_fw), MEM_WAIT);
	if (!rtl_fw)
		goto err_warn;

	rc = request_firmware(&rtl_fw->fw, name, &tp->pci_dev->device);
	if (rc < 0)
		goto err_free;

	rc = rtl_check_firmware(tp, rtl_fw);
	if (rc < 0)
		goto err_release_firmware;

	tp->rtl_fw = rtl_fw;
out:
	return;

err_release_firmware:
	release_firmware(rtl_fw->fw);
err_free:
	kfree(rtl_fw);
err_warn:
	netif_warn(tp, ifup, tp->dev, "unable to load firmware patch %s (%d)\n",
		   name, rc);
out_no_firmware:
	tp->rtl_fw = NULL;
	goto out;
}

static void rtl_request_firmware(struct rtl8169_private *tp)
{
	if (IS_ERR(tp->rtl_fw))
		rtl_request_uncached_firmware(tp);
}

static void rtl_rx_close(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(RxConfig, RTL_R32(RxConfig) & ~RX_CONFIG_ACCEPT_MASK);
}

DECLARE_RTL_COND(rtl_npq_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(TxPoll) & NPQ;
}

DECLARE_RTL_COND(rtl_txcfg_empty_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(TxConfig) & TXCFG_EMPTY;
}

static void rtl8169_hw_reset(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* Disable interrupts */
	rtl8169_irq_mask_and_ack(tp);

	rtl_rx_close(tp);

	if (tp->mac_version == RTL_GIGA_MAC_VER_27 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_28 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_31) {
		rtl_udelay_loop_wait_low(tp, &rtl_npq_cond, 20, 42*42);
	} else if (tp->mac_version == RTL_GIGA_MAC_VER_34 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_35 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_36 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_37 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_38 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_40 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_41 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_42 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_43 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_44 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_45 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_46 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_47 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_48 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_49 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_50 ||
		   tp->mac_version == RTL_GIGA_MAC_VER_51) {
		RTL_W8(ChipCmd, RTL_R8(ChipCmd) | StopReq);
		rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 666);
	} else {
		RTL_W8(ChipCmd, RTL_R8(ChipCmd) | StopReq);
		udelay(100);
	}

	rtl_hw_reset(tp);
}

static void rtl_set_rx_tx_config_registers(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* Set DMA burst size and Interframe Gap Time */
	RTL_W32(TxConfig, (TX_DMA_BURST << TxDMAShift) |
		(InterFrameGap << TxInterFrameGapShift));
}

static void rtl_hw_start(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	tp->hw_start(dev);

	rtl_irq_enable_all(tp);
}

static void rtl_set_rx_tx_desc_registers(struct rtl8169_private *tp,
					 void __iomem *ioaddr)
{
	/*
	 * Magic spell: some iop3xx ARM board needs the TxDescAddrHigh
	 * register to be written before TxDescAddrLow to work.
	 * Switching from MMIO to I/O access fixes the issue as well.
	 */
	RTL_W32(TxDescStartAddrHigh, ((uint64_t) tp->TxPhyAddr) >> 32);
	RTL_W32(TxDescStartAddrLow, ((uint64_t) tp->TxPhyAddr) & DMA_BIT_MASK(32));
	RTL_W32(RxDescAddrHigh, ((uint64_t) tp->RxPhyAddr) >> 32);
	RTL_W32(RxDescAddrLow, ((uint64_t) tp->RxPhyAddr) & DMA_BIT_MASK(32));
}

static uint16_t rtl_rw_cpluscmd(void __iomem *ioaddr)
{
	uint16_t cmd;

	cmd = RTL_R16(CPlusCmd);
	RTL_W16(CPlusCmd, cmd);
	return cmd;
}

static void rtl_set_rx_max_size(void __iomem *ioaddr, unsigned int rx_buf_sz)
{
	/* Low hurts. Let's disable the filtering. */
	RTL_W16(RxMaxSize, rx_buf_sz + 1);
}

static void rtl8169_set_magic_reg(void __iomem *ioaddr, unsigned mac_version)
{
	static const struct rtl_cfg2_info {
		uint32_t mac_version;
		uint32_t clk;
		uint32_t val;
	} cfg2_info [] = {
		{ RTL_GIGA_MAC_VER_05, PCI_Clock_33MHz, 0x000fff00 }, // 8110SCd
		{ RTL_GIGA_MAC_VER_05, PCI_Clock_66MHz, 0x000fffff },
		{ RTL_GIGA_MAC_VER_06, PCI_Clock_33MHz, 0x00ffff00 }, // 8110SCe
		{ RTL_GIGA_MAC_VER_06, PCI_Clock_66MHz, 0x00ffffff }
	};
	const struct rtl_cfg2_info *p = cfg2_info;
	unsigned int i;
	uint32_t clk;

	clk = RTL_R8(Config2) & PCI_Clock_66MHz;
	for (i = 0; i < ARRAY_SIZE(cfg2_info); i++, p++) {
		if ((p->mac_version == mac_version) && (p->clk == clk)) {
			RTL_W32(0x7c, p->val);
			break;
		}
	}
}

static void rtl_set_rx_mode(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t mc_filter[2];	/* Multicast hash filter */
	int rx_mode;
	uint32_t tmp = 0;

	if (dev->rx_mode & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		netif_notice(tp, link, dev, "Promiscuous mode enabled\n");
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		mc_filter[1] = mc_filter[0] = 0;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
	}
/* Here's how they do multicast, if we ever care */
#if 0 // AKAROS_PORT
	} else if ((netdev_mc_count(dev) > multicast_filter_limit) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct netdev_hw_addr *ha;

		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev) {
			int bit_nr = ether_crc(Eaddrlen, ha->addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}
#endif

	if (dev->feat & NETIF_F_RXALL)
		rx_mode |= (AcceptErr | AcceptRunt);

	tmp = (RTL_R32(RxConfig) & ~RX_CONFIG_ACCEPT_MASK) | rx_mode;

	if (tp->mac_version > RTL_GIGA_MAC_VER_06) {
		uint32_t data = mc_filter[0];

		mc_filter[0] = swab32(mc_filter[1]);
		mc_filter[1] = swab32(data);
	}

	if (tp->mac_version == RTL_GIGA_MAC_VER_35)
		mc_filter[1] = mc_filter[0] = 0xffffffff;

	RTL_W32(MAR0 + 4, mc_filter[1]);
	RTL_W32(MAR0 + 0, mc_filter[0]);

	RTL_W32(RxConfig, tmp);
}

static void rtl_hw_start_8169(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	if (tp->mac_version == RTL_GIGA_MAC_VER_05) {
		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) | PCIMulRW);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, 0x08);
	}

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	if (tp->mac_version == RTL_GIGA_MAC_VER_01 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_02 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_03 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_04)
		RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

	rtl_init_rxcfg(tp);

	RTL_W8(EarlyTxThres, NoEarlyTx);

	rtl_set_rx_max_size(ioaddr, rx_buf_sz);

	if (tp->mac_version == RTL_GIGA_MAC_VER_01 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_02 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_03 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_04)
		rtl_set_rx_tx_config_registers(tp);

	tp->cp_cmd |= rtl_rw_cpluscmd(ioaddr) | PCIMulRW;

	if (tp->mac_version == RTL_GIGA_MAC_VER_02 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_03) {
		dprintk("Set MAC Reg C+CR Offset 0xe0. "
			"Bit-3 and bit-14 MUST be 1\n");
		tp->cp_cmd |= (1 << 14);
	}

	RTL_W16(CPlusCmd, tp->cp_cmd);

	rtl8169_set_magic_reg(ioaddr, tp->mac_version);

	/*
	 * Undocumented corner. Supposedly:
	 * (TxTimer << 12) | (TxPackets << 8) | (RxTimer << 4) | RxPackets
	 */
	RTL_W16(IntrMitigate, 0x0000);

	rtl_set_rx_tx_desc_registers(tp, ioaddr);

	if (tp->mac_version != RTL_GIGA_MAC_VER_01 &&
	    tp->mac_version != RTL_GIGA_MAC_VER_02 &&
	    tp->mac_version != RTL_GIGA_MAC_VER_03 &&
	    tp->mac_version != RTL_GIGA_MAC_VER_04) {
		RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);
		rtl_set_rx_tx_config_registers(tp);
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	/* Initially a 10 us delay. Turned it into a PCI commit. - FR */
	RTL_R8(IntrMask);

	RTL_W32(RxMissed, 0);

	rtl_set_rx_mode(dev);

	/* no early-rx interrupts */
	RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xf000);
}

static void rtl_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	if (tp->csi_ops.write)
		tp->csi_ops.write(tp, addr, value);
}

static uint32_t rtl_csi_read(struct rtl8169_private *tp, int addr)
{
	return tp->csi_ops.read ? tp->csi_ops.read(tp, addr) : ~0;
}

static void rtl_csi_access_enable(struct rtl8169_private *tp, uint32_t bits)
{
	uint32_t csi;

	csi = rtl_csi_read(tp, 0x070c) & 0x00ffffff;
	rtl_csi_write(tp, 0x070c, csi | bits);
}

static void rtl_csi_access_enable_1(struct rtl8169_private *tp)
{
	rtl_csi_access_enable(tp, 0x17000000);
}

static void rtl_csi_access_enable_2(struct rtl8169_private *tp)
{
	rtl_csi_access_enable(tp, 0x27000000);
}

DECLARE_RTL_COND(rtl_csiar_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R32(CSIAR) & CSIAR_FLAG;
}

static void r8169_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIDR, value);
	RTL_W32(CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	rtl_udelay_loop_wait_low(tp, &rtl_csiar_cond, 10, 100);
}

static uint32_t r8169_csi_read(struct rtl8169_private *tp, int addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIAR, (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_csiar_cond, 10, 100) ?
		RTL_R32(CSIDR) : ~0;
}

static void r8402_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIDR, value);
	RTL_W32(CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT |
		CSIAR_FUNC_NIC);

	rtl_udelay_loop_wait_low(tp, &rtl_csiar_cond, 10, 100);
}

static uint32_t r8402_csi_read(struct rtl8169_private *tp, int addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIAR, (addr & CSIAR_ADDR_MASK) | CSIAR_FUNC_NIC |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_csiar_cond, 10, 100) ?
		RTL_R32(CSIDR) : ~0;
}

static void r8411_csi_write(struct rtl8169_private *tp, int addr, int value)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIDR, value);
	RTL_W32(CSIAR, CSIAR_WRITE_CMD | (addr & CSIAR_ADDR_MASK) |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT |
		CSIAR_FUNC_NIC2);

	rtl_udelay_loop_wait_low(tp, &rtl_csiar_cond, 10, 100);
}

static uint32_t r8411_csi_read(struct rtl8169_private *tp, int addr)
{
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W32(CSIAR, (addr & CSIAR_ADDR_MASK) | CSIAR_FUNC_NIC2 |
		CSIAR_BYTE_ENABLE << CSIAR_BYTE_ENABLE_SHIFT);

	return rtl_udelay_loop_wait_high(tp, &rtl_csiar_cond, 10, 100) ?
		RTL_R32(CSIDR) : ~0;
}

static void rtl_init_csi_ops(struct rtl8169_private *tp)
{
	struct csi_ops *ops = &tp->csi_ops;

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_01:
	case RTL_GIGA_MAC_VER_02:
	case RTL_GIGA_MAC_VER_03:
	case RTL_GIGA_MAC_VER_04:
	case RTL_GIGA_MAC_VER_05:
	case RTL_GIGA_MAC_VER_06:
	case RTL_GIGA_MAC_VER_10:
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_13:
	case RTL_GIGA_MAC_VER_14:
	case RTL_GIGA_MAC_VER_15:
	case RTL_GIGA_MAC_VER_16:
	case RTL_GIGA_MAC_VER_17:
		ops->write	= NULL;
		ops->read	= NULL;
		break;

	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_38:
		ops->write	= r8402_csi_write;
		ops->read	= r8402_csi_read;
		break;

	case RTL_GIGA_MAC_VER_44:
		ops->write	= r8411_csi_write;
		ops->read	= r8411_csi_read;
		break;

	default:
		ops->write	= r8169_csi_write;
		ops->read	= r8169_csi_read;
		break;
	}
}

struct ephy_info {
	unsigned int offset;
	uint16_t mask;
	uint16_t bits;
};

static void rtl_ephy_init(struct rtl8169_private *tp, const struct ephy_info *e,
			  int len)
{
	uint16_t w;

	while (len-- > 0) {
		w = (rtl_ephy_read(tp, e->offset) & ~e->mask) | e->bits;
		rtl_ephy_write(tp, e->offset, w);
		e++;
	}
}

static void rtl_disable_clock_request(struct pci_device *pdev)
{
	pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_CLKREQ_EN);
}

static void rtl_enable_clock_request(struct pci_device *pdev)
{
	pcie_capability_set_word(pdev, PCI_EXP_LNKCTL,
				 PCI_EXP_LNKCTL_CLKREQ_EN);
}

static void rtl_pcie_state_l2l3_enable(struct rtl8169_private *tp, bool enable)
{
	void __iomem *ioaddr = tp->mmio_addr;
	uint8_t data;

	data = RTL_R8(Config3);

	if (enable)
		data |= Rdy_to_L23;
	else
		data &= ~Rdy_to_L23;

	RTL_W8(Config3, data);
}

#define R8168_CPCMD_QUIRK_MASK (\
	EnableBist | \
	Mac_dbgo_oe | \
	Force_half_dup | \
	Force_rxflow_en | \
	Force_txflow_en | \
	Cxpl_dbg_sel | \
	ASF | \
	PktCntrDisable | \
	Mac_dbgo_sel)

static void rtl_hw_start_8168bb(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

	RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) & ~R8168_CPCMD_QUIRK_MASK);

	if (tp->dev->mtu <= ETHERMAXTU) {
		rtl_tx_performance_tweak(pdev, (0x5 << MAX_READ_REQUEST_SHIFT) |
					 PCI_EXP_DEVCTL_NOSNOOP_EN);
	}
}

static void rtl_hw_start_8168bef(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	rtl_hw_start_8168bb(tp);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	RTL_W8(Config4, RTL_R8(Config4) & ~(1 << 0));
}

static void __rtl_hw_start_8168cp(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	RTL_W8(Config1, RTL_R8(Config1) | Speed_down);

	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_disable_clock_request(pdev);

	RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) & ~R8168_CPCMD_QUIRK_MASK);
}

static void rtl_hw_start_8168cp_1(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168cp[] = {
		{ 0x01, 0,	0x0001 },
		{ 0x02, 0x0800,	0x1000 },
		{ 0x03, 0,	0x0042 },
		{ 0x06, 0x0080,	0x0000 },
		{ 0x07, 0,	0x2000 }
	};

	rtl_csi_access_enable_2(tp);

	rtl_ephy_init(tp, e_info_8168cp, ARRAY_SIZE(e_info_8168cp));

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168cp_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_2(tp);

	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) & ~R8168_CPCMD_QUIRK_MASK);
}

static void rtl_hw_start_8168cp_3(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_2(tp);

	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

	/* Magic. */
	RTL_W8(DBG_REG, 0x20);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) & ~R8168_CPCMD_QUIRK_MASK);
}

static void rtl_hw_start_8168c_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168c_1[] = {
		{ 0x02, 0x0800,	0x1000 },
		{ 0x03, 0,	0x0002 },
		{ 0x06, 0x0080,	0x0000 }
	};

	rtl_csi_access_enable_2(tp);

	RTL_W8(DBG_REG, 0x06 | FIX_NAK_1 | FIX_NAK_2);

	rtl_ephy_init(tp, e_info_8168c_1, ARRAY_SIZE(e_info_8168c_1));

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168c_2(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168c_2[] = {
		{ 0x01, 0,	0x0001 },
		{ 0x03, 0x0400,	0x0220 }
	};

	rtl_csi_access_enable_2(tp);

	rtl_ephy_init(tp, e_info_8168c_2, ARRAY_SIZE(e_info_8168c_2));

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168c_3(struct rtl8169_private *tp)
{
	rtl_hw_start_8168c_2(tp);
}

static void rtl_hw_start_8168c_4(struct rtl8169_private *tp)
{
	rtl_csi_access_enable_2(tp);

	__rtl_hw_start_8168cp(tp);
}

static void rtl_hw_start_8168d(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_2(tp);

	rtl_disable_clock_request(pdev);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) & ~R8168_CPCMD_QUIRK_MASK);
}

static void rtl_hw_start_8168dp(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_1(tp);

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	rtl_disable_clock_request(pdev);
}

static void rtl_hw_start_8168d_4(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	static const struct ephy_info e_info_8168d_4[] = {
		{ 0x0b, 0x0000,	0x0048 },
		{ 0x19, 0x0020,	0x0050 },
		{ 0x0c, 0x0100,	0x0020 }
	};

	rtl_csi_access_enable_1(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	rtl_ephy_init(tp, e_info_8168d_4, ARRAY_SIZE(e_info_8168d_4));

	rtl_enable_clock_request(pdev);
}

static void rtl_hw_start_8168e_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	static const struct ephy_info e_info_8168e_1[] = {
		{ 0x00, 0x0200,	0x0100 },
		{ 0x00, 0x0000,	0x0004 },
		{ 0x06, 0x0002,	0x0001 },
		{ 0x06, 0x0000,	0x0030 },
		{ 0x07, 0x0000,	0x2000 },
		{ 0x00, 0x0000,	0x0020 },
		{ 0x03, 0x5800,	0x2000 },
		{ 0x03, 0x0000,	0x0001 },
		{ 0x01, 0x0800,	0x1000 },
		{ 0x07, 0x0000,	0x4000 },
		{ 0x1e, 0x0000,	0x2000 },
		{ 0x19, 0xffff,	0xfe6c },
		{ 0x0a, 0x0000,	0x0040 }
	};

	rtl_csi_access_enable_2(tp);

	rtl_ephy_init(tp, e_info_8168e_1, ARRAY_SIZE(e_info_8168e_1));

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	rtl_disable_clock_request(pdev);

	/* Reset tx FIFO pointer */
	RTL_W32(MISC, RTL_R32(MISC) | TXPLA_RST);
	RTL_W32(MISC, RTL_R32(MISC) & ~TXPLA_RST);

	RTL_W8(Config5, RTL_R8(Config5) & ~Spi_en);
}

static void rtl_hw_start_8168e_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	static const struct ephy_info e_info_8168e_2[] = {
		{ 0x09, 0x0000,	0x0080 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_csi_access_enable_1(tp);

	rtl_ephy_init(tp, e_info_8168e_2, ARRAY_SIZE(e_info_8168e_2));

	if (tp->dev->mtu <= ETHERMAXTU)
		rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xc8, ERIAR_MASK_1111, 0x00100002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_1111, 0x00000050, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_1111, 0x07ff0060, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0001, 0x10, 0x00, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00, 0xff00, ERIAR_EXGMAC);

	RTL_W8(MaxTxPacketSize, EarlySize);

	rtl_disable_clock_request(pdev);

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);

	RTL_W8(DLLPR, RTL_R8(DLLPR) | PFM_EN);
	RTL_W32(MISC, RTL_R32(MISC) | PWM_EN);
	RTL_W8(Config5, RTL_R8(Config5) & ~Spi_en);
}

static void rtl_hw_start_8168f(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_2(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xc8, ERIAR_MASK_1111, 0x00100002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0001, 0x10, 0x00, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x1d0, ERIAR_MASK_0001, 0x10, 0x00, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_1111, 0x00000050, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_1111, 0x00000060, ERIAR_EXGMAC);

	RTL_W8(MaxTxPacketSize, EarlySize);

	rtl_disable_clock_request(pdev);

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);
	RTL_W8(DLLPR, RTL_R8(DLLPR) | PFM_EN);
	RTL_W32(MISC, RTL_R32(MISC) | PWM_EN);
	RTL_W8(Config5, RTL_R8(Config5) & ~Spi_en);
}

static void rtl_hw_start_8168f_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168f_1[] = {
		{ 0x06, 0x00c0,	0x0020 },
		{ 0x08, 0x0001,	0x0002 },
		{ 0x09, 0x0000,	0x0080 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_hw_start_8168f(tp);

	rtl_ephy_init(tp, e_info_8168f_1, ARRAY_SIZE(e_info_8168f_1));

	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00, 0xff00, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);
}

static void rtl_hw_start_8411(struct rtl8169_private *tp)
{
	static const struct ephy_info e_info_8168f_1[] = {
		{ 0x06, 0x00c0,	0x0020 },
		{ 0x0f, 0xffff,	0x5200 },
		{ 0x1e, 0x0000,	0x4000 },
		{ 0x19, 0x0000,	0x0224 }
	};

	rtl_hw_start_8168f(tp);
	rtl_pcie_state_l2l3_enable(tp, false);

	rtl_ephy_init(tp, e_info_8168f_1, ARRAY_SIZE(e_info_8168f_1));

	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0c00, 0x0000, ERIAR_EXGMAC);
}

static void rtl_hw_start_8168g(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);

	rtl_eri_write(tp, 0xc8, ERIAR_MASK_0101, 0x080002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_0001, 0x38, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_0001, 0x48, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);

	rtl_csi_access_enable_1(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0x2f8, ERIAR_MASK_0011, 0x1d8f, ERIAR_EXGMAC);

	RTL_W32(MISC, RTL_R32(MISC) & ~RXDV_GATED_EN);
	RTL_W8(MaxTxPacketSize, EarlySize);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);

	rtl_w0w1_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, 0x1000, ERIAR_EXGMAC);

	rtl_pcie_state_l2l3_enable(tp, false);
}

static void rtl_hw_start_8168g_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168g_1[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x37d0,	0x0820 },
		{ 0x1e, 0x0000,	0x0001 },
		{ 0x19, 0x8000,	0x0000 }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168g_1, ARRAY_SIZE(e_info_8168g_1));
}

static void rtl_hw_start_8168g_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168g_2[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x3df0,	0x0200 },
		{ 0x19, 0xffff,	0xfc00 },
		{ 0x1e, 0xffff,	0x20eb }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168g_2, ARRAY_SIZE(e_info_8168g_2));
}

static void rtl_hw_start_8411_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8411_2[] = {
		{ 0x00, 0x0000,	0x0008 },
		{ 0x0c, 0x3df0,	0x0200 },
		{ 0x0f, 0xffff,	0x5200 },
		{ 0x19, 0x0020,	0x0000 },
		{ 0x1e, 0x0000,	0x2000 }
	};

	rtl_hw_start_8168g(tp);

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8411_2, ARRAY_SIZE(e_info_8411_2));
}

static void rtl_hw_start_8168h_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	int rg_saw_cnt;
	uint32_t data;
	static const struct ephy_info e_info_8168h_1[] = {
		{ 0x1e, 0x0800,	0x0001 },
		{ 0x1d, 0x0000,	0x0800 },
		{ 0x05, 0xffff,	0x2089 },
		{ 0x06, 0xffff,	0x5881 },
		{ 0x04, 0xffff,	0x154a },
		{ 0x01, 0xffff,	0x068b }
	};

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168h_1, ARRAY_SIZE(e_info_8168h_1));

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);

	rtl_eri_write(tp, 0xc8, ERIAR_MASK_0101, 0x00080002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_0001, 0x38, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_0001, 0x48, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);

	rtl_csi_access_enable_1(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);

	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_1111, 0x0010, 0x00, ERIAR_EXGMAC);

	rtl_w0w1_eri(tp, 0xd4, ERIAR_MASK_1111, 0x1f00, 0x00, ERIAR_EXGMAC);

	rtl_eri_write(tp, 0x5f0, ERIAR_MASK_0011, 0x4f87, ERIAR_EXGMAC);

	RTL_W32(MISC, RTL_R32(MISC) & ~RXDV_GATED_EN);
	RTL_W8(MaxTxPacketSize, EarlySize);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);

	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~PFM_EN);
	RTL_W8(MISC_1, RTL_R8(MISC_1) & ~PFM_D3COLD_EN);

	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~TX_10M_PS_EN);

	rtl_w0w1_eri(tp, 0x1b0, ERIAR_MASK_0011, 0x0000, 0x1000, ERIAR_EXGMAC);

	rtl_pcie_state_l2l3_enable(tp, false);

	rtl_writephy(tp, 0x1f, 0x0c42);
	rg_saw_cnt = (rtl_readphy(tp, 0x13) & 0x3fff);
	rtl_writephy(tp, 0x1f, 0x0000);
	if (rg_saw_cnt > 0) {
		uint16_t sw_cnt_1ms_ini;

		sw_cnt_1ms_ini = 16000000/rg_saw_cnt;
		sw_cnt_1ms_ini &= 0x0fff;
		data = r8168_mac_ocp_read(tp, 0xd412);
		data &= ~0x0fff;
		data |= sw_cnt_1ms_ini;
		r8168_mac_ocp_write(tp, 0xd412, data);
	}

	data = r8168_mac_ocp_read(tp, 0xe056);
	data &= ~0xf0;
	data |= 0x70;
	r8168_mac_ocp_write(tp, 0xe056, data);

	data = r8168_mac_ocp_read(tp, 0xe052);
	data &= ~0x6000;
	data |= 0x8008;
	r8168_mac_ocp_write(tp, 0xe052, data);

	data = r8168_mac_ocp_read(tp, 0xe0d6);
	data &= ~0x01ff;
	data |= 0x017f;
	r8168_mac_ocp_write(tp, 0xe0d6, data);

	data = r8168_mac_ocp_read(tp, 0xd420);
	data &= ~0x0fff;
	data |= 0x047f;
	r8168_mac_ocp_write(tp, 0xd420, data);

	r8168_mac_ocp_write(tp, 0xe63e, 0x0001);
	r8168_mac_ocp_write(tp, 0xe63e, 0x0000);
	r8168_mac_ocp_write(tp, 0xc094, 0x0000);
	r8168_mac_ocp_write(tp, 0xc09e, 0x0000);
}

static void rtl_hw_start_8168ep(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl8168ep_stop_cmac(tp);

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);

	rtl_eri_write(tp, 0xc8, ERIAR_MASK_0101, 0x00080002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xcc, ERIAR_MASK_0001, 0x2f, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xd0, ERIAR_MASK_0001, 0x5f, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00100006, ERIAR_EXGMAC);

	rtl_csi_access_enable_1(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);

	rtl_w0w1_eri(tp, 0xd4, ERIAR_MASK_1111, 0x1f80, 0x00, ERIAR_EXGMAC);

	rtl_eri_write(tp, 0x5f0, ERIAR_MASK_0011, 0x4f87, ERIAR_EXGMAC);

	RTL_W32(MISC, RTL_R32(MISC) & ~RXDV_GATED_EN);
	RTL_W8(MaxTxPacketSize, EarlySize);

	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);

	/* Adjust EEE LED frequency */
	RTL_W8(EEE_LED, RTL_R8(EEE_LED) & ~0x07);

	rtl_w0w1_eri(tp, 0x2fc, ERIAR_MASK_0001, 0x01, 0x06, ERIAR_EXGMAC);

	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~TX_10M_PS_EN);

	rtl_pcie_state_l2l3_enable(tp, false);
}

static void rtl_hw_start_8168ep_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168ep_1[] = {
		{ 0x00, 0xffff,	0x10ab },
		{ 0x06, 0xffff,	0xf030 },
		{ 0x08, 0xffff,	0x2006 },
		{ 0x0d, 0xffff,	0x1666 },
		{ 0x0c, 0x3ff0,	0x0000 }
	};

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168ep_1, ARRAY_SIZE(e_info_8168ep_1));

	rtl_hw_start_8168ep(tp);
}

static void rtl_hw_start_8168ep_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8168ep_2[] = {
		{ 0x00, 0xffff,	0x10a3 },
		{ 0x19, 0xffff,	0xfc00 },
		{ 0x1e, 0xffff,	0x20ea }
	};

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168ep_2, ARRAY_SIZE(e_info_8168ep_2));

	rtl_hw_start_8168ep(tp);

	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~PFM_EN);
	RTL_W8(MISC_1, RTL_R8(MISC_1) & ~PFM_D3COLD_EN);
}

static void rtl_hw_start_8168ep_3(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t data;
	static const struct ephy_info e_info_8168ep_3[] = {
		{ 0x00, 0xffff,	0x10a3 },
		{ 0x19, 0xffff,	0x7c00 },
		{ 0x1e, 0xffff,	0x20eb },
		{ 0x0d, 0xffff,	0x1666 }
	};

	/* disable aspm and clock request before access ephy */
	RTL_W8(Config2, RTL_R8(Config2) & ~ClkReqEn);
	RTL_W8(Config5, RTL_R8(Config5) & ~ASPM_en);
	rtl_ephy_init(tp, e_info_8168ep_3, ARRAY_SIZE(e_info_8168ep_3));

	rtl_hw_start_8168ep(tp);

	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~PFM_EN);
	RTL_W8(MISC_1, RTL_R8(MISC_1) & ~PFM_D3COLD_EN);

	data = r8168_mac_ocp_read(tp, 0xd3e2);
	data &= 0xf000;
	data |= 0x0271;
	r8168_mac_ocp_write(tp, 0xd3e2, data);

	data = r8168_mac_ocp_read(tp, 0xd3e4);
	data &= 0xff00;
	r8168_mac_ocp_write(tp, 0xd3e4, data);

	data = r8168_mac_ocp_read(tp, 0xe860);
	data |= 0x0080;
	r8168_mac_ocp_write(tp, 0xe860, data);
}

static void rtl_hw_start_8168(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	rtl_set_rx_max_size(ioaddr, rx_buf_sz);

	tp->cp_cmd |= RTL_R16(CPlusCmd) | PktCntrDisable | INTT_1;

	RTL_W16(CPlusCmd, tp->cp_cmd);

	RTL_W16(IntrMitigate, 0x5151);

	/* Work around for RxFIFO overflow. */
	if (tp->mac_version == RTL_GIGA_MAC_VER_11) {
		tp->event_slow |= RxFIFOOver | PCSTimeout;
		tp->event_slow &= ~RxOverflow;
	}

	rtl_set_rx_tx_desc_registers(tp, ioaddr);

	rtl_set_rx_tx_config_registers(tp);

	RTL_R8(IntrMask);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
		rtl_hw_start_8168bb(tp);
		break;

	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		rtl_hw_start_8168bef(tp);
		break;

	case RTL_GIGA_MAC_VER_18:
		rtl_hw_start_8168cp_1(tp);
		break;

	case RTL_GIGA_MAC_VER_19:
		rtl_hw_start_8168c_1(tp);
		break;

	case RTL_GIGA_MAC_VER_20:
		rtl_hw_start_8168c_2(tp);
		break;

	case RTL_GIGA_MAC_VER_21:
		rtl_hw_start_8168c_3(tp);
		break;

	case RTL_GIGA_MAC_VER_22:
		rtl_hw_start_8168c_4(tp);
		break;

	case RTL_GIGA_MAC_VER_23:
		rtl_hw_start_8168cp_2(tp);
		break;

	case RTL_GIGA_MAC_VER_24:
		rtl_hw_start_8168cp_3(tp);
		break;

	case RTL_GIGA_MAC_VER_25:
	case RTL_GIGA_MAC_VER_26:
	case RTL_GIGA_MAC_VER_27:
		rtl_hw_start_8168d(tp);
		break;

	case RTL_GIGA_MAC_VER_28:
		rtl_hw_start_8168d_4(tp);
		break;

	case RTL_GIGA_MAC_VER_31:
		rtl_hw_start_8168dp(tp);
		break;

	case RTL_GIGA_MAC_VER_32:
	case RTL_GIGA_MAC_VER_33:
		rtl_hw_start_8168e_1(tp);
		break;
	case RTL_GIGA_MAC_VER_34:
		rtl_hw_start_8168e_2(tp);
		break;

	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
		rtl_hw_start_8168f_1(tp);
		break;

	case RTL_GIGA_MAC_VER_38:
		rtl_hw_start_8411(tp);
		break;

	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
		rtl_hw_start_8168g_1(tp);
		break;
	case RTL_GIGA_MAC_VER_42:
		rtl_hw_start_8168g_2(tp);
		break;

	case RTL_GIGA_MAC_VER_44:
		rtl_hw_start_8411_2(tp);
		break;

	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
		rtl_hw_start_8168h_1(tp);
		break;

	case RTL_GIGA_MAC_VER_49:
		rtl_hw_start_8168ep_1(tp);
		break;

	case RTL_GIGA_MAC_VER_50:
		rtl_hw_start_8168ep_2(tp);
		break;

	case RTL_GIGA_MAC_VER_51:
		rtl_hw_start_8168ep_3(tp);
		break;

	default:
		printk(KERN_ERR PFX "%s: unknown chipset (mac_version = %d).\n",
			dev->name, tp->mac_version);
		break;
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

	rtl_set_rx_mode(dev);

	RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xf000);
}

#define R810X_CPCMD_QUIRK_MASK (\
	EnableBist | \
	Mac_dbgo_oe | \
	Force_half_dup | \
	Force_rxflow_en | \
	Force_txflow_en | \
	Cxpl_dbg_sel | \
	ASF | \
	PktCntrDisable | \
	Mac_dbgo_sel)

static void rtl_hw_start_8102e_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	static const struct ephy_info e_info_8102e_1[] = {
		{ 0x01,	0, 0x6e65 },
		{ 0x02,	0, 0x091f },
		{ 0x03,	0, 0xc2f9 },
		{ 0x06,	0, 0xafb5 },
		{ 0x07,	0, 0x0e00 },
		{ 0x19,	0, 0xec80 },
		{ 0x01,	0, 0x2e65 },
		{ 0x01,	0, 0x6e65 }
	};
	uint8_t cfg1;

	rtl_csi_access_enable_2(tp);

	RTL_W8(DBG_REG, FIX_NAK_1);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W8(Config1,
	       LEDS1 | LEDS0 | Speed_down | MEMMAP | IOMAP | VPD | PMEnable);
	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

	cfg1 = RTL_R8(Config1);
	if ((cfg1 & LEDS0) && (cfg1 & LEDS1))
		RTL_W8(Config1, cfg1 & ~LEDS0);

	rtl_ephy_init(tp, e_info_8102e_1, ARRAY_SIZE(e_info_8102e_1));
}

static void rtl_hw_start_8102e_2(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	rtl_csi_access_enable_2(tp);

	rtl_tx_performance_tweak(pdev, 0x5 << MAX_READ_REQUEST_SHIFT);

	RTL_W8(Config1, MEMMAP | IOMAP | VPD | PMEnable);
	RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);
}

static void rtl_hw_start_8102e_3(struct rtl8169_private *tp)
{
	rtl_hw_start_8102e_2(tp);

	rtl_ephy_write(tp, 0x03, 0xc2f9);
}

static void rtl_hw_start_8105e_1(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8105e_1[] = {
		{ 0x07,	0, 0x4000 },
		{ 0x19,	0, 0x0200 },
		{ 0x19,	0, 0x0020 },
		{ 0x1e,	0, 0x2000 },
		{ 0x03,	0, 0x0001 },
		{ 0x19,	0, 0x0100 },
		{ 0x19,	0, 0x0004 },
		{ 0x0a,	0, 0x0020 }
	};

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(FuncEvent, RTL_R32(FuncEvent) | 0x002800);

	/* Disable Early Tally Counter */
	RTL_W32(FuncEvent, RTL_R32(FuncEvent) & ~0x010000);

	RTL_W8(MCU, RTL_R8(MCU) | EN_NDP | EN_OOB_RESET);
	RTL_W8(DLLPR, RTL_R8(DLLPR) | PFM_EN);

	rtl_ephy_init(tp, e_info_8105e_1, ARRAY_SIZE(e_info_8105e_1));

	rtl_pcie_state_l2l3_enable(tp, false);
}

static void rtl_hw_start_8105e_2(struct rtl8169_private *tp)
{
	rtl_hw_start_8105e_1(tp);
	rtl_ephy_write(tp, 0x1e, rtl_ephy_read(tp, 0x1e) | 0x8000);
}

static void rtl_hw_start_8402(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	static const struct ephy_info e_info_8402[] = {
		{ 0x19,	0xffff, 0xff64 },
		{ 0x1e,	0, 0x4000 }
	};

	rtl_csi_access_enable_2(tp);

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(FuncEvent, RTL_R32(FuncEvent) | 0x002800);

	RTL_W32(TxConfig, RTL_R32(TxConfig) | TXCFG_AUTO_FIFO);
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	rtl_ephy_init(tp, e_info_8402, ARRAY_SIZE(e_info_8402));

	rtl_tx_performance_tweak(tp->pci_dev, 0x5 << MAX_READ_REQUEST_SHIFT);

	rtl_eri_write(tp, 0xc8, ERIAR_MASK_1111, 0x00000002, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xe8, ERIAR_MASK_1111, 0x00000006, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x00, 0x01, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0xdc, ERIAR_MASK_0001, 0x01, 0x00, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xc0, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_eri_write(tp, 0xb8, ERIAR_MASK_0011, 0x0000, ERIAR_EXGMAC);
	rtl_w0w1_eri(tp, 0x0d4, ERIAR_MASK_0011, 0x0e00, 0xff00, ERIAR_EXGMAC);

	rtl_pcie_state_l2l3_enable(tp, false);
}

static void rtl_hw_start_8106(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* Force LAN exit from ASPM if Rx/Tx are not idle */
	RTL_W32(FuncEvent, RTL_R32(FuncEvent) | 0x002800);

	RTL_W32(MISC, (RTL_R32(MISC) | DISABLE_LAN_EN) & ~EARLY_TALLY_EN);
	RTL_W8(MCU, RTL_R8(MCU) | EN_NDP | EN_OOB_RESET);
	RTL_W8(DLLPR, RTL_R8(DLLPR) & ~PFM_EN);

	rtl_pcie_state_l2l3_enable(tp, false);
}

static void rtl_hw_start_8101(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;

	if (tp->mac_version >= RTL_GIGA_MAC_VER_30)
		tp->event_slow &= ~RxFIFOOver;

	if (tp->mac_version == RTL_GIGA_MAC_VER_13 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_16)
		pcie_capability_set_word(pdev, PCI_EXP_DEVCTL,
					 PCI_EXP_DEVCTL_NOSNOOP_EN);

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	RTL_W8(MaxTxPacketSize, TxPacketMax);

	rtl_set_rx_max_size(ioaddr, rx_buf_sz);

	tp->cp_cmd &= ~R810X_CPCMD_QUIRK_MASK;
	RTL_W16(CPlusCmd, tp->cp_cmd);

	rtl_set_rx_tx_desc_registers(tp, ioaddr);

	rtl_set_rx_tx_config_registers(tp);

	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_07:
		rtl_hw_start_8102e_1(tp);
		break;

	case RTL_GIGA_MAC_VER_08:
		rtl_hw_start_8102e_3(tp);
		break;

	case RTL_GIGA_MAC_VER_09:
		rtl_hw_start_8102e_2(tp);
		break;

	case RTL_GIGA_MAC_VER_29:
		rtl_hw_start_8105e_1(tp);
		break;
	case RTL_GIGA_MAC_VER_30:
		rtl_hw_start_8105e_2(tp);
		break;

	case RTL_GIGA_MAC_VER_37:
		rtl_hw_start_8402(tp);
		break;

	case RTL_GIGA_MAC_VER_39:
		rtl_hw_start_8106(tp);
		break;
	case RTL_GIGA_MAC_VER_43:
		rtl_hw_start_8168g_2(tp);
		break;
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
		rtl_hw_start_8168h_1(tp);
		break;
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	RTL_W16(IntrMitigate, 0x0000);

	RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

	rtl_set_rx_mode(dev);

	RTL_R8(IntrMask);

	RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xf000);
}

static int rtl8169_change_mtu(struct ether *dev, int new_mtu)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (new_mtu > ETHERMAXTU)
		rtl_hw_jumbo_enable(tp);
	else
		rtl_hw_jumbo_disable(tp);

	dev->mtu = new_mtu;
	netdev_update_features(dev);

	return 0;
}

static inline void rtl8169_make_unusable_by_asic(struct RxDesc *desc)
{
	desc->addr = cpu_to_le64(0x0badbadbadbadbadull);
	desc->opts1 &= ~cpu_to_le32(DescOwn | RsvdMask);
}

static void rtl8169_free_rx_databuff(struct rtl8169_private *tp,
				     void **data_buff, struct RxDesc *desc)
{
	dma_unmap_single(&tp->pci_dev->device, le64_to_cpu(desc->addr), rx_buf_sz,
			 DMA_FROM_DEVICE);

	kfree(*data_buff);
	*data_buff = NULL;
	rtl8169_make_unusable_by_asic(desc);
}

static inline void rtl8169_mark_to_asic(struct RxDesc *desc,
					uint32_t rx_buf_sz)
{
	uint32_t eor = le32_to_cpu(desc->opts1) & RingEnd;

	/* Force memory writes to complete before releasing descriptor */
	bus_wmb();

	desc->opts1 = cpu_to_le32(DescOwn | eor | rx_buf_sz);
}

static inline void rtl8169_map_to_asic(struct RxDesc *desc, dma_addr_t mapping,
				       uint32_t rx_buf_sz)
{
	desc->addr = cpu_to_le64(mapping);
	rtl8169_mark_to_asic(desc, rx_buf_sz);
}

static inline void *rtl8169_align(void *data)
{
	return (void *)ALIGN((long)data, 16);
}

/* Note this allocates memory and during RX, memcpys that to an skb/block. */
static void *rtl8169_alloc_rx_data(struct rtl8169_private *tp,
					     struct RxDesc *desc)
{
	void *data;
	dma_addr_t mapping;
	struct device *d = &tp->pci_dev->device;
	struct ether *dev = tp->dev;
#if 0 // AKAROS_PORT
	int node = dev->dev.parent ? dev_to_node(dev->dev.parent) : -1;
#else
	int node = 0;
#endif

	data = kmalloc_node(rx_buf_sz, MEM_WAIT, node);
	if (!data)
		return NULL;

	if (rtl8169_align(data) != data) {
		kfree(data);
		data = kmalloc_node(rx_buf_sz + 15, MEM_WAIT, node);
		if (!data)
			return NULL;
	}

	mapping = dma_map_single(d, rtl8169_align(data), rx_buf_sz,
				 DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		if (net_ratelimit())
			netif_err(tp, drv, tp->dev, "Failed to map RX DMA!\n");
		goto err_out;
	}

	rtl8169_map_to_asic(desc, mapping, rx_buf_sz);
	return data;

err_out:
	kfree(data);
	return NULL;
}

static void rtl8169_rx_clear(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->Rx_databuff[i]) {
			rtl8169_free_rx_databuff(tp, tp->Rx_databuff + i,
					    tp->RxDescArray + i);
		}
	}
}

static inline void rtl8169_mark_as_last_descriptor(struct RxDesc *desc)
{
	desc->opts1 |= cpu_to_le32(RingEnd);
}

static int rtl8169_rx_fill(struct rtl8169_private *tp)
{
	unsigned int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		void *data;

		if (tp->Rx_databuff[i])
			continue;

		data = rtl8169_alloc_rx_data(tp, tp->RxDescArray + i);
		if (!data) {
			rtl8169_make_unusable_by_asic(tp->RxDescArray + i);
			goto err_out;
		}
		tp->Rx_databuff[i] = data;
	}

	rtl8169_mark_as_last_descriptor(tp->RxDescArray + NUM_RX_DESC - 1);
	return 0;

err_out:
	rtl8169_rx_clear(tp);
	return -ENOMEM;
}

static int rtl8169_init_ring(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_init_ring_indexes(tp);

	memset(tp->tx_skb, 0x0, NUM_TX_DESC * sizeof(struct ring_info));
	memset(tp->Rx_databuff, 0x0, NUM_RX_DESC * sizeof(void *));

	return rtl8169_rx_fill(tp);
}

static void rtl8169_unmap_tx_skb(struct device *d, struct ring_info *tx_skb,
				 struct TxDesc *desc)
{
	unsigned int len = tx_skb->len;

	dma_unmap_single(d, le64_to_cpu(desc->addr), len, DMA_TO_DEVICE);

	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void rtl8169_tx_clear_range(struct rtl8169_private *tp, uint32_t start,
				   unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		unsigned int entry = (start + i) % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct block *bp = tx_skb->block;

			rtl8169_unmap_tx_skb(&tp->pci_dev->device, tx_skb,
					     tp->TxDescArray + entry);
			if (bp) {
				freeb(bp);
				tx_skb->block = NULL;
			}
		}
	}
}

static void rtl8169_tx_clear(struct rtl8169_private *tp)
{
	rtl8169_tx_clear_range(tp, tp->dirty_tx, NUM_TX_DESC);
	tp->cur_tx = tp->dirty_tx = 0;
}

static void rtl_reset_work(struct rtl8169_private *tp)
{
	struct ether *dev = tp->dev;
	int i;

	napi_disable(&tp->napi);
	netif_stop_queue(dev);
	synchronize_sched();

	rtl8169_hw_reset(tp);

	for (i = 0; i < NUM_RX_DESC; i++)
		rtl8169_mark_to_asic(tp->RxDescArray + i, rx_buf_sz);

	rtl8169_tx_clear(tp);
	rtl8169_init_ring_indexes(tp);

	napi_enable(&tp->napi);
	rtl_hw_start(dev);
	netif_wake_queue(dev);
	rtl8169_check_link_status(dev, tp, tp->mmio_addr);
}

/* In Linux, the network stack has a watchdog that triggers this.  We can ignore
 * it.  Probably. */
static void rtl8169_tx_timeout(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static int rtl8169_xmit_frags(struct rtl8169_private *tp, struct block *bp,
			      uint32_t *opts)
{
	unsigned int cur_frag, entry;
	struct TxDesc *uninitialized_var(txd);
	struct device *d = &tp->pci_dev->device;

	entry = tp->cur_tx;
	/* Don't use cur_frag for the loop.  ebd may have holes, but we only
	 * increment cur_frag for valid frags / ebds. */
	cur_frag = 0;
	for (int i = 0; i < bp->nr_extra_bufs; i++) {
		const struct extra_bdata *ebd = &bp->extra_data[i];
		dma_addr_t mapping;
		uint32_t status, len;
		void *addr;

		if (!ebd->base || !ebd->len)
			continue;
		entry = (entry + 1) % NUM_TX_DESC;

		txd = tp->TxDescArray + entry;
		len = ebd->len;
		addr = (void*)(ebd->base + ebd->off);
		mapping = dma_map_single(d, addr, len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(d, mapping))) {
			if (net_ratelimit())
				netif_err(tp, drv, tp->dev,
					  "Failed to map TX fragments DMA!\n");
			goto err_out;
		}

		/* Anti gcc 2.95.3 bugware (sic) */
		status = opts[0] | len |
			(RingEnd * !((entry + 1) % NUM_TX_DESC));

		txd->opts1 = cpu_to_le32(status);
		txd->opts2 = cpu_to_le32(opts[1]);
		txd->addr = cpu_to_le64(mapping);

		tp->tx_skb[entry].len = len;
		/* Tracking how many frags we're sending.  The expectation from
		 * the Linux code is that cur_frag is 0-indexed during the loop,
		 * and incremented at the end.  This only seems to matter for
		 * err_out. */
		cur_frag++;
	}

	if (cur_frag) {
		tp->tx_skb[entry].block = bp;
		txd->opts1 |= cpu_to_le32(LastFrag);
	}

	return cur_frag;

err_out:
	rtl8169_tx_clear_range(tp, tp->cur_tx + 1, cur_frag);
	return -EIO;
}

static bool rtl_test_hw_pad_bug(struct rtl8169_private *tp, struct block *bp)
{
	return BLEN(bp) < ETH_ZLEN && tp->mac_version == RTL_GIGA_MAC_VER_34;
}

static netdev_tx_t rtl8169_start_xmit(struct block *bp, struct ether *dev);

/* r8169_csum_workaround()
 * The hw limites the value the transport offset. When the offset is out of the
 * range, calculate the checksum by sw.
 */
static void r8169_csum_workaround(struct rtl8169_private *tp,
				  struct block *bp)
{
	if (bp->mss) {
		/* Need to break the large packet up into smaller segments.
		 * Would need support from higher in the stack. */
		panic("Not implemented");
#if 0 // AKAROS_PORT
		netdev_features_t features = tp->dev->feat;
		struct sk_buff *segs, *nskb;

		features &= ~(NETIF_F_SG | NETIF_F_IPV6_CSUM | NETIF_F_TSO6);
		segs = skb_gso_segment(skb, features);
		if (IS_ERR(segs) || !segs)
			goto drop;

		do {
			nskb = segs;
			segs = segs->next;
			nskb->next = NULL;
			rtl8169_start_xmit(nskb, tp->dev);
		} while (segs);

		dev_consume_skb_any(skb);
#endif
	} else if (bp->flag & BLOCK_TRANS_TX_CSUM) {
		/* Yes, finalize checks the bp->flag too, but we wanted to know
		 * if we should even try, and o/w drop. */
		ptclcsum_finalize(bp, 0);
		rtl8169_start_xmit(bp, tp->dev);
	} else {
		struct netif_stats *stats;

drop:
		stats = &tp->dev->stats;
		stats->tx_dropped++;
		freeb(bp);
	}
}

/* msdn_giant_send_check()
 * According to the document of microsoft, the TCP Pseudo Header excludes the
 * packet length for IPv6 TCP large packets.
 *
 * brho: This does a pseudo header xsum calculation, replacing whatever the
 * network stack did with a PH that didn't include the length (or rather treated
 * len = 0).  That's not in the spec for what the TCPv6 xsum should do, but it
 * might be an idiosyncracy of the NIC.
 *
 * If TCP xsums on IPv6 are broken, look here.  This is completely untested.  We
 * might be better off grabbing some of Linux's csum functions (TODO). */
static int msdn_giant_send_check(struct block *bp)
{
	struct ip6hdr *ipv6h;
	struct tcphdr *th;

	ipv6h = ipv6_hdr(bp);
	th = tcp_hdr(bp);

#if 0 // AKAROS_PORT
	th->tcpcksum = 0;
	th->tcpcksum = ~tcp_v6_check(0, &ipv6h->saddr, &ipv6h->daddr, 0);
#else
	/* Instead of recaclulating (which we aren't set up for), we'll try to
	 * change the len from len to 0. */
	uint16_t xsum = nhgets(th->tcpcksum);
	uint16_t len = nhgets(ipv6h->ploadlen);

	/* RFC 1624 */
	xsum = ~xsum + ~len + 0;
	while (xsum >> 16)
		xsum = (xsum & 0xffff) + (xsum >> 16);
	xsum = ~xsum;
	hnputs(th->tcpcksum, xsum);
#endif

	return 0;
}

static bool rtl8169_tso_csum_v1(struct rtl8169_private *tp,
				struct block *bp, uint32_t *opts)
{
	uint32_t mss = bp->mss;

	if (mss) {
		opts[0] |= TD_LSO;
		opts[0] |= MIN(mss, TD_MSS_MAX) << TD0_MSS_SHIFT;
	} else if (bp->flag & BLOCK_TRANS_TX_CSUM) {
		if (bp->flag & Btcpck)
			opts[0] |= TD0_IP_CS | TD0_TCP_CS;
		else if (bp->flag & Budpck)
			opts[0] |= TD0_IP_CS | TD0_UDP_CS;
		else
			warn_on_once(1);
	}

	return true;
}


static bool rtl8169_tso_csum_v2(struct rtl8169_private *tp,
				struct block *bp, uint32_t *opts)
{
	uint32_t transport_offset = (uint32_t)bp->transport_offset;
	uint32_t mss = bp->mss;

	if (mss) {
		if (transport_offset > GTTCPHO_MAX) {
			netif_warn(tp, tx_err, tp->dev,
				   "Invalid transport offset 0x%x for TSO\n",
				   transport_offset);
			return false;
		}

		switch (ip_version(bp)) {
		case 4:
			opts[0] |= TD1_GTSENV4;
			break;

		case 6:
			if (msdn_giant_send_check(bp))
				return false;

			opts[0] |= TD1_GTSENV6;
			break;

		default:
			warn_on_once(1);
			break;
		}

		opts[0] |= transport_offset << GTTCPHO_SHIFT;
		opts[1] |= MIN(mss, TD_MSS_MAX) << TD1_MSS_SHIFT;
	} else if (bp->flag & BLOCK_TRANS_TX_CSUM) {
		uint8_t ip_protocol;

		/* Linux would conditionally pad it the packet if the hardware
		 * can't do it (which is my interpretation of seeing eth_skb_pad
		 * / ETH_ZLEN padding/checks.  On Akaros, we'll tell the
		 * ethernet layer to do it for the bad models (grep
		 * NETF_PADMIN), and leave the checks in here for sanity. */
		assert(!rtl_test_hw_pad_bug(tp, bp));

		if (transport_offset > TCPHO_MAX) {
			netif_warn(tp, tx_err, tp->dev,
				   "Invalid transport offset 0x%x\n",
				   transport_offset);
			return false;
		}

		switch (ip_version(bp)) {
		case 4:
			opts[1] |= TD1_IPv4_CS;
			ip_protocol = ipv4_hdr(bp)->proto;
			break;

		case 6:
			opts[1] |= TD1_IPv6_CS;
			ip_protocol = ipv6_hdr(bp)->proto;
			break;

		default:
			ip_protocol = IPPROTO_RAW;
			break;
		}

		/* Note Akaros could check the specific BLOCK_TRANS_TX_CSUM flag
		 */
		if (ip_protocol == IPPROTO_TCP)
			opts[1] |= TD1_TCP_CS;
		else if (ip_protocol == IPPROTO_UDP)
			opts[1] |= TD1_UDP_CS;
		else
			warn_on_once(1);

		opts[1] |= transport_offset << TCPHO_SHIFT;
	} else {
		assert(!rtl_test_hw_pad_bug(tp, bp));
	}

	return true;
}

static netdev_tx_t rtl8169_start_xmit(struct block *bp, struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	unsigned int entry = tp->cur_tx % NUM_TX_DESC;
	struct TxDesc *txd = tp->TxDescArray + entry;
	void __iomem *ioaddr = tp->mmio_addr;
	struct device *d = &tp->pci_dev->device;
	dma_addr_t mapping;
	uint32_t status, len;
	uint32_t opts[2];
	int frags;

	/* TODO: This isn't quite nr_frags, since there could be holes.  Should
	 * fix the block extra data to track valid segments. */
	if (unlikely(!TX_FRAGS_READY_FOR(tp, bp->nr_extra_bufs))) {
		netif_err(tp, drv, dev,
			  "BUG! Tx Ring full when queue awake!\n");
		goto err_stop_0;
	}

	if (unlikely(le32_to_cpu(txd->opts1) & DescOwn))
		goto err_stop_0;

	opts[1] = cpu_to_le32(rtl8169_tx_vlan_tag(bp));
	opts[0] = DescOwn;

	if (!tp->tso_csum(tp, bp, opts)) {
		r8169_csum_workaround(tp, bp);
		return NETDEV_TX_OK;
	}

	len = BHLEN(bp);
	/* I think we always have some sort of header in BHLEN.  If not, change
	 * the rest of this to handle it. */
	assert(len);
	mapping = dma_map_single(d, bp->rp, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(d, mapping))) {
		if (net_ratelimit())
			netif_err(tp, drv, dev, "Failed to map TX DMA!\n");
		goto err_dma_0;
	}

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);

	frags = rtl8169_xmit_frags(tp, bp, opts);
	if (frags < 0)
		goto err_dma_1;
	else if (frags)
		opts[0] |= FirstFrag;
	else {
		opts[0] |= FirstFrag | LastFrag;
		tp->tx_skb[entry].block = bp;
	}

	txd->opts2 = cpu_to_le32(opts[1]);

	skb_tx_timestamp(bp);

	/* Force memory writes to complete before releasing descriptor */
	bus_wmb();

	/* Anti gcc 2.95.3 bugware (sic) */
	status = opts[0] | len | (RingEnd * !((entry + 1) % NUM_TX_DESC));
	txd->opts1 = cpu_to_le32(status);

	/* Force all memory writes to complete before notifying device */
	wmb();

	tp->cur_tx += frags + 1;

	RTL_W8(TxPoll, NPQ);

	bus_wmb();

	/* Linux calls netif_stop_queue if (!TX_FRAGS_READY_FOR(tp,
	 * MAX_SKB_FRAGS))
	 *
	 * We do our backpressure in __rtl_xmit_poke. */

	return NETDEV_TX_OK;

err_dma_1:
	rtl8169_unmap_tx_skb(d, tp->tx_skb + entry, txd);
err_dma_0:
	freeb(bp);
	dev->stats.tx_dropped++;
	return NETDEV_TX_OK;

err_stop_0:
	netif_stop_queue(dev);
	dev->stats.tx_dropped++;
	return NETDEV_TX_BUSY;
}

static void rtl8169_pcierr_interrupt(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_device *pdev = tp->pci_dev;
	uint16_t pci_status, pci_cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
	pci_read_config_word(pdev, PCI_STATUS, &pci_status);

	netif_err(tp, intr, dev, "PCI error (cmd = 0x%04x, status = 0x%04x)\n",
		  pci_cmd, pci_status);

	/*
	 * The recovery sequence below admits a very elaborated explanation:
	 * - it seems to work;
	 * - I did not see what else could be done;
	 * - it makes iop3xx happy.
	 *
	 * Feel free to adjust to your needs.
	 */
/* Wasn't clear who sets broken_parity_status */
#if 0 // AKAROS_PORT
	if (pdev->broken_parity_status)
		pci_cmd &= ~PCI_COMMAND_PARITY;
	else
#endif
		pci_cmd |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY;

	pci_write_config_word(pdev, PCI_COMMAND, pci_cmd);

	pci_write_config_word(pdev, PCI_STATUS,
		pci_status & (PCI_STATUS_DETECTED_PARITY |
		PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_REC_MASTER_ABORT |
		PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_SIG_TARGET_ABORT));

	/* The infamous DAC f*ckup only happens at boot time */
	if ((tp->cp_cmd & PCIDAC) && !tp->cur_rx) {
		void __iomem *ioaddr = tp->mmio_addr;

		netif_info(tp, intr, dev, "disabling PCI DAC\n");
		tp->cp_cmd &= ~PCIDAC;
		RTL_W16(CPlusCmd, tp->cp_cmd);
		dev->feat &= ~NETIF_F_HIGHDMA;
	}

	rtl8169_hw_reset(tp);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static void rtl_tx(struct ether *dev, struct rtl8169_private *tp)
{
	unsigned int dirty_tx, tx_left;

	dirty_tx = tp->dirty_tx;
	rmb();
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		uint32_t status;

		status = le32_to_cpu(tp->TxDescArray[entry].opts1);
		if (status & DescOwn)
			break;

		/* This barrier is needed to keep us from reading
		 * any other fields out of the Tx descriptor until
		 * we know the status of DescOwn
		 */
		bus_rmb();

		rtl8169_unmap_tx_skb(&tp->pci_dev->device, tx_skb,
				     tp->TxDescArray + entry);
		if (status & LastFrag) {
			u64_stats_update_begin(&tp->tx_stats.syncp);
			tp->tx_stats.packets++;
			tp->tx_stats.bytes += BLEN(tx_skb->block);
			u64_stats_update_end(&tp->tx_stats.syncp);
			freeb(tx_skb->block);
			tx_skb->block = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		/* Linux stops the netif here (based on TX_FRAGS_READY_FOR(tp,
		 * MAX_SKB_FRAGS)).  It stops the netif before the hack. */

		/*
		 * 8168 hack: TxPoll requests are lost when the Tx packets are
		 * too close. Let's kick an extra TxPoll request when a burst
		 * of start_xmit activity is detected (if it is not detected,
		 * it is slow enough). -- FR
		 */
		if (tp->cur_tx != dirty_tx) {
			void __iomem *ioaddr = tp->mmio_addr;

			RTL_W8(TxPoll, NPQ);
		}

		/* I'm not sure if this needs to happen after the TxPoll hack.
		 * The netif is woken first on linux, but there might be a napi
		 * ordering, where the expectation is that the TxPoll hack
		 * happens before the netif stop (and potentially start) */
		struct rtl_poke_args args[1];

		args->edev = dev;
		args->tp = tp;
		poke(&tp->poker, args);
	}
}

static inline int rtl8169_fragmented_frame(uint32_t status)
{
	return (status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
}

static inline void rtl8169_rx_csum(struct block *bp, uint32_t opts1)
{
	uint32_t status = opts1 & RxProtoMask;

	/* Linux marked CHECKSUM_UNNECESSARY here.  We might need to do
	 * something with Bipck still. */
	if (((status == RxProtoTCP) && !(opts1 & TCPFail)) ||
	    ((status == RxProtoUDP) && !(opts1 & UDPFail)))
		bp->flag |= Btcpck | Budpck;
	else
		assert(!(bp->flag & BLOCK_RX_CSUM));
}

static struct block *rtl8169_try_rx_copy(void *data, struct rtl8169_private *tp,
                                         int pkt_size, dma_addr_t addr)
{
	struct block *bp;
	struct device *d = &tp->pci_dev->device;

	data = rtl8169_align(data);
	dma_sync_single_for_cpu(d, addr, pkt_size, DMA_FROM_DEVICE);
	prefetch(data);
	bp = block_alloc(pkt_size, MEM_ATOMIC);
	if (bp)
		memcpy(bp->wp, data, pkt_size);
	dma_sync_single_for_device(d, addr, pkt_size, DMA_FROM_DEVICE);

	return bp;
}

static int rtl_rx(struct ether *dev, struct rtl8169_private *tp,
		  uint32_t budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int count;

	cur_rx = tp->cur_rx;

	for (rx_left = MIN(budget, NUM_RX_DESC); rx_left > 0; rx_left--,
	     cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct RxDesc *desc = tp->RxDescArray + entry;
		uint32_t status;

		status = le32_to_cpu(desc->opts1) & tp->opts1_mask;
		if (status & DescOwn)
			break;

		/* This barrier is needed to keep us from reading
		 * any other fields out of the Rx descriptor until
		 * we know the status of DescOwn
		 */
		bus_rmb();

		if (unlikely(status & RxRES)) {
			netif_info(tp, rx_err, dev, "Rx ERROR. status = %08x\n",
				   status);
			dev->stats.rx_errors++;
			if (status & (RxRWT | RxRUNT))
				dev->stats.rx_length_errors++;
			if (status & RxCRC)
				dev->stats.rx_crc_errors++;
			if (status & RxFOVF) {
				rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
				dev->stats.rx_fifo_errors++;
			}
			if ((status & (RxRUNT | RxCRC)) &&
			    !(status & (RxRWT | RxFOVF)) &&
			    (dev->feat & NETIF_F_RXALL))
				goto process_pkt;
		} else {
			struct block *bp;
			dma_addr_t addr;
			int pkt_size;

process_pkt:
			addr = le64_to_cpu(desc->addr);
			if (likely(!(dev->feat & NETIF_F_RXFCS)))
				pkt_size = (status & 0x00003fff) - 4;
			else
				pkt_size = status & 0x00003fff;

			/*
			 * The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8169_fragmented_frame(status))) {
				dev->stats.rx_dropped++;
				dev->stats.rx_length_errors++;
				goto release_descriptor;
			}

			bp = rtl8169_try_rx_copy(tp->Rx_databuff[entry],
						  tp, pkt_size, addr);
			if (!bp) {
				dev->stats.rx_dropped++;
				goto release_descriptor;
			}

			rtl8169_rx_csum(bp, status);
			bp->wp += pkt_size;

/* On Akaros, we either don't bother with this stuff or deal with it in
 * devether.  (don't track the ethernet protocol, haven't hooked into vlan
 * stuff, and multicast should be dealt with higher. */
#if 0 // AKAROS_PORT
			skb->protocol = eth_type_trans(skb, dev);

			rtl8169_rx_vlan_tag(desc, skb);

			if (skb->pkt_type == PACKET_MULTICAST)
				dev->stats.multicast++;
#endif
			etheriq(dev, bp, TRUE);

			u64_stats_update_begin(&tp->rx_stats.syncp);
			tp->rx_stats.packets++;
			tp->rx_stats.bytes += pkt_size;
			u64_stats_update_end(&tp->rx_stats.syncp);
		}
release_descriptor:
		desc->opts2 = 0;
		rtl8169_mark_to_asic(desc, rx_buf_sz);
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	return count;
}

/* Interesting - there's no spinlocks in r8169.  The IRQ handler mostly checks
 * status and schedules work.  Not sure if we're guaranteed only one IRQ per
 * device at a time or if the ops here (like irq_disable) are SMP-safe. */
static void rtl8169_interrupt(struct hw_trapframe *hw_tf, void *dev_instance)
{
	struct ether *dev = dev_instance;
	struct rtl8169_private *tp = netdev_priv(dev);
	int handled = 0;
	uint16_t status;

	status = rtl_get_events(tp);
	if (status && status != 0xffff) {
		status &= RTL_EVENT_NAPI | tp->event_slow;
		if (status) {
			handled = 1;
			rtl_irq_disable(tp);
			/* It's not clear if there's only one IRQ at a time
			 * (SMP) or not.  Safest way is to use a ktask. */
			rendez_wakeup(&tp->irq_task);
		}
	}
}

/*
 * Workqueue context.
 */
static void rtl_slow_event_work(struct rtl8169_private *tp)
{
	struct ether *dev = tp->dev;
	uint16_t status;

	status = rtl_get_events(tp) & tp->event_slow;
	rtl_ack_events(tp, status);

	if (unlikely(status & RxFIFOOver)) {
		switch (tp->mac_version) {
		/* Work around for rx fifo overflow */
		case RTL_GIGA_MAC_VER_11:
			/* This might not work, given how our workqueue shims go
			 */
			panic("Not implemented");
			netif_stop_queue(dev);
			/* XXX - Hack alert. See rtl_task(). */
			set_bit(RTL_FLAG_TASK_RESET_PENDING, tp->wk.flags);
		default:
			break;
		}
	}

	if (unlikely(status & SYSErr))
		rtl8169_pcierr_interrupt(dev);

	if (status & LinkChg)
		__rtl8169_check_link_status(dev, tp, tp->mmio_addr, true);

	rtl_irq_enable_all(tp);
}

static void rtl_task(struct work_struct *work)
{
	static const struct {
		int bitnr;
		void (*action)(struct rtl8169_private *);
	} rtl_work[] = {
		/* XXX - keep rtl_slow_event_work() as first element. */
		{ RTL_FLAG_TASK_SLOW_PENDING,	rtl_slow_event_work },
		{ RTL_FLAG_TASK_RESET_PENDING,	rtl_reset_work },
		{ RTL_FLAG_TASK_PHY_PENDING,	rtl_phy_work }
	};
	struct rtl8169_private *tp =
		container_of(work, struct rtl8169_private, wk.work);
	struct ether *dev = tp->dev;
	int i;

	rtl_lock_work(tp);

	if (!netif_running(dev) ||
	    !test_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags))
		goto out_unlock;

	for (i = 0; i < ARRAY_SIZE(rtl_work); i++) {
		bool pending;

		pending = test_and_clear_bit(rtl_work[i].bitnr, tp->wk.flags);
		if (pending)
			rtl_work[i].action(tp);
	}

out_unlock:
	rtl_unlock_work(tp);
}

static int rtl_wake_cond(void *arg)
{
	struct rtl8169_private *tp = arg;

	return rtl_get_events(tp) & (RTL_EVENT_NAPI | tp->event_slow) ? 1 : 0;
}

/* Replaces rtl8169_poll (NAPI).  This is a ktask that runs and sleeps on a
 * rendez, woken up by IRQs.  Compared to mlx4, we just pass rtl_rx a large
 * budget.  If that's a problem, we can do smaller budgets and kthread_yield().
 *
 * The original ran in NAPI context and spawned a WQ task for slow stuff.
 * We'll just run it all in the ktask/rproc.  One issue with the NAPI/WQ stuff
 * was that it was assumed the WQ ran after the NAPI loop.  If that's not the
 * case, one race was on enabling interrupts.  The WQ enables all; NAPI enables
 * only Rx/Tx.  If the NAPI bit ran later, it'd clobber the WQ's settings. */
static void rtl8169_irq_ktask(void *arg)
{
	struct ether *dev = arg;
	struct rtl8169_private *tp = netdev_priv(dev);
	uint16_t enable_mask = RTL_EVENT_NAPI | tp->event_slow;
	uint16_t status;

	while (1) {
		status = rtl_get_events(tp);
		rtl_ack_events(tp, status & ~tp->event_slow);

		if (status & RTL_EVENT_NAPI_RX) {
			/* careful of budget's type */
			rtl_rx(dev, tp, UINT32_MAX);
		}

		if (status & RTL_EVENT_NAPI_TX)
			rtl_tx(dev, tp);

		if (status & tp->event_slow) {
			/* This calls rtl_irq_enable_all() */
			rtl_slow_event_work(tp);
		} else {
			rtl_irq_enable(tp, enable_mask);
			bus_wmb();
		}

		rendez_sleep(&tp->irq_task, rtl_wake_cond, tp);
	}
}

static void rtl8169_rx_missed(struct ether *dev, void __iomem *ioaddr)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (tp->mac_version > RTL_GIGA_MAC_VER_06)
		return;

	dev->stats.rx_missed_errors += (RTL_R32(RxMissed) & 0xffffff);
	RTL_W32(RxMissed, 0);
}

static void rtl8169_down(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	del_timer_sync(&tp->timer);

	napi_disable(&tp->napi);
	netif_stop_queue(dev);

	rtl8169_hw_reset(tp);
	/*
	 * At this point device interrupts can not be enabled in any function,
	 * as netif_running is not true (rtl8169_interrupt, rtl8169_reset_task)
	 * and napi is disabled (rtl8169_poll).
	 */
	rtl8169_rx_missed(dev, ioaddr);

	/* Give a racing hard_start_xmit a few cycles to complete. */
	synchronize_sched();

	rtl8169_tx_clear(tp);

	rtl8169_rx_clear(tp);

	rtl_pll_power_down(tp);
}

static int rtl8169_close(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	struct pci_device *pdev = tp->pci_dev;

	pm_runtime_get_sync(&pdev->device);

	/* Update counters before going down */
	rtl8169_update_counters(dev);

	rtl_lock_work(tp);
	clear_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);

	rtl8169_down(dev);
	rtl_unlock_work(tp);

	cancel_work_sync(&tp->wk.work);

	free_irq(pdev->irqline, dev);

	dma_free_coherent(&pdev->device, R8169_RX_RING_BYTES, tp->RxDescArray,
			  tp->RxPhyAddr);
	dma_free_coherent(&pdev->device, R8169_TX_RING_BYTES, tp->TxDescArray,
			  tp->TxPhyAddr);
	tp->TxDescArray = NULL;
	tp->RxDescArray = NULL;

	pm_runtime_put_sync(&pdev->device);

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void rtl8169_netpoll(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_interrupt(tp->pci_dev->irqline, dev);
}
#endif

static int rtl_open(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	int retval = -ENOMEM;

	pm_runtime_get_sync(&pdev->device);

	/*
	 * Rx and Tx descriptors needs 256 bytes alignment.
	 * dma_alloc_coherent provides more.
	 */
	tp->TxDescArray = dma_zalloc_coherent(&pdev->device,
					      R8169_TX_RING_BYTES,
					      &tp->TxPhyAddr, MEM_WAIT);
	if (!tp->TxDescArray)
		goto err_pm_runtime_put;

	tp->RxDescArray = dma_zalloc_coherent(&pdev->device,
					      R8169_RX_RING_BYTES,
					      &tp->RxPhyAddr, MEM_WAIT);
	if (!tp->RxDescArray)
		goto err_free_tx_0;

	retval = rtl8169_init_ring(dev);
	if (retval < 0)
		goto err_free_rx_1;

	INIT_WORK(&tp->wk.work, rtl_task);

	mb();

	rtl_request_firmware(tp);

	retval = register_irq(pdev->irqline, rtl8169_interrupt, dev,
			      pci_to_tbdf(pdev));

	if (retval < 0)
		goto err_release_fw_2;

	rtl_lock_work(tp);

	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);

	napi_enable(&tp->napi);

	rtl8169_init_phy(dev, tp);

	__rtl8169_set_features(dev, dev->feat);

	rtl_pll_power_up(tp);

	rtl_hw_start(dev);

	if (!rtl8169_init_counter_offsets(dev))
		netif_warn(tp, hw, dev, "counter reset/update failed\n");

	netif_start_queue(dev);

	rtl_unlock_work(tp);

	tp->saved_wolopts = 0;
	pm_runtime_put_noidle(&pdev->device);

	rtl8169_check_link_status(dev, tp, ioaddr);
out:
	return retval;

err_release_fw_2:
	rtl_release_firmware(tp);
	rtl8169_rx_clear(tp);
err_free_rx_1:
	dma_free_coherent(&pdev->device, R8169_RX_RING_BYTES, tp->RxDescArray,
			  tp->RxPhyAddr);
	tp->RxDescArray = NULL;
err_free_tx_0:
	dma_free_coherent(&pdev->device, R8169_TX_RING_BYTES, tp->TxDescArray,
			  tp->TxPhyAddr);
	tp->TxDescArray = NULL;
err_pm_runtime_put:
	pm_runtime_put_noidle(&pdev->device);
	goto out;
}

static void
rtl8169_get_stats64(struct ether *dev, struct netif_stats *stats)
{
	struct rtl8169_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_device *pdev = tp->pci_dev;
	struct rtl8169_counters *counters = tp->counters;
	unsigned int start;

	pm_runtime_get_noresume(&pdev->device);

	if (netif_running(dev) && pm_runtime_active(&pdev->device))
		rtl8169_rx_missed(dev, ioaddr);

	do {
		start = u64_stats_fetch_begin_irq(&tp->rx_stats.syncp);
		stats->rx_packets = tp->rx_stats.packets;
		stats->rx_bytes	= tp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->rx_stats.syncp, start));

	do {
		start = u64_stats_fetch_begin_irq(&tp->tx_stats.syncp);
		stats->tx_packets = tp->tx_stats.packets;
		stats->tx_bytes	= tp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_irq(&tp->tx_stats.syncp, start));

	stats->rx_dropped	= dev->stats.rx_dropped;
	stats->tx_dropped	= dev->stats.tx_dropped;
	stats->rx_length_errors = dev->stats.rx_length_errors;
	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_crc_errors	= dev->stats.rx_crc_errors;
	stats->rx_fifo_errors	= dev->stats.rx_fifo_errors;
	stats->rx_missed_errors = dev->stats.rx_missed_errors;
	stats->multicast	= dev->stats.multicast;

	/*
	 * Fetch additonal counter values missing in stats collected by driver
	 * from tally counters.
	 */
	if (pm_runtime_active(&pdev->device))
		rtl8169_update_counters(dev);

	/*
	 * Subtract values fetched during initalization.
	 * See rtl8169_init_counter_offsets for a description why we do that.
	 */
	stats->tx_errors = le64_to_cpu(counters->tx_errors) -
		le64_to_cpu(tp->tc_offset.tx_errors);
	stats->collisions = le32_to_cpu(counters->tx_multi_collision) -
		le32_to_cpu(tp->tc_offset.tx_multi_collision);
	stats->tx_aborted_errors = le16_to_cpu(counters->tx_aborted) -
		le16_to_cpu(tp->tc_offset.tx_aborted);

	pm_runtime_put_noidle(&pdev->device);
}

static void rtl8169_net_suspend(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!netif_running(dev))
		return;

	netif_device_detach(dev);
	netif_stop_queue(dev);

	rtl_lock_work(tp);
	napi_disable(&tp->napi);
	clear_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);
	rtl_unlock_work(tp);

	rtl_pll_power_down(tp);
}

#ifdef CONFIG_PM

static int rtl8169_suspend(struct device *device)
{
	struct pci_device *pdev = to_pci_dev(device);
	struct ether *dev = pci_get_drvdata(pdev);

	rtl8169_net_suspend(dev);

	return 0;
}

static void __rtl8169_resume(struct ether *dev)
{
	struct rtl8169_private *tp = netdev_priv(dev);

	netif_device_attach(dev);

	rtl_pll_power_up(tp);

	rtl_lock_work(tp);
	napi_enable(&tp->napi);
	set_bit(RTL_FLAG_TASK_ENABLED, tp->wk.flags);
	rtl_unlock_work(tp);

	rtl_schedule_task(tp, RTL_FLAG_TASK_RESET_PENDING);
}

static int rtl8169_resume(struct device *device)
{
	struct pci_device *pdev = to_pci_dev(device);
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	rtl8169_init_phy(dev, tp);

	if (netif_running(dev))
		__rtl8169_resume(dev);

	return 0;
}

static int rtl8169_runtime_suspend(struct device *device)
{
	struct pci_device *pdev = to_pci_dev(device);
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	if (!tp->TxDescArray)
		return 0;

	rtl_lock_work(tp);
	tp->saved_wolopts = __rtl8169_get_wol(tp);
	__rtl8169_set_wol(tp, WAKE_ANY);
	rtl_unlock_work(tp);

	rtl8169_net_suspend(dev);

	/* Update counters before going runtime suspend */
	rtl8169_rx_missed(dev, tp->mmio_addr);
	rtl8169_update_counters(dev);

	return 0;
}

static int rtl8169_runtime_resume(struct device *device)
{
	struct pci_device *pdev = to_pci_dev(device);
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);
	rtl_rar_set(tp, dev->ea);

	if (!tp->TxDescArray)
		return 0;

	rtl_lock_work(tp);
	__rtl8169_set_wol(tp, tp->saved_wolopts);
	tp->saved_wolopts = 0;
	rtl_unlock_work(tp);

	rtl8169_init_phy(dev, tp);

	__rtl8169_resume(dev);

	return 0;
}

static int rtl8169_runtime_idle(struct device *device)
{
	struct pci_device *pdev = to_pci_dev(device);
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	return tp->TxDescArray ? -EBUSY : 0;
}

static const struct dev_pm_ops rtl8169_pm_ops = {
	.suspend		= rtl8169_suspend,
	.resume			= rtl8169_resume,
	.freeze			= rtl8169_suspend,
	.thaw			= rtl8169_resume,
	.poweroff		= rtl8169_suspend,
	.restore		= rtl8169_resume,
	.runtime_suspend	= rtl8169_runtime_suspend,
	.runtime_resume		= rtl8169_runtime_resume,
	.runtime_idle		= rtl8169_runtime_idle,
};

#define RTL8169_PM_OPS	(&rtl8169_pm_ops)

#else /* !CONFIG_PM */

#define RTL8169_PM_OPS	NULL

#endif /* !CONFIG_PM */

static void rtl_wol_shutdown_quirk(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;

	/* WoL fails with 8168b when the receiver is disabled. */
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_11:
	case RTL_GIGA_MAC_VER_12:
	case RTL_GIGA_MAC_VER_17:
		pci_clr_bus_master(tp->pci_dev);

		RTL_W8(ChipCmd, CmdRxEnb);
		/* PCI commit */
		RTL_R8(ChipCmd);
		break;
	default:
		break;
	}
}

static void rtl_shutdown(struct pci_device *pdev)
{
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);
	struct device *d = &pdev->device;

	pm_runtime_get_sync(d);

	rtl8169_net_suspend(dev);

#if 0 // AKAROS_PORT
	/* Restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);
#endif

	rtl8169_hw_reset(tp);

#if 0 // AKAROS_PORT
	if (system_state == SYSTEM_POWER_OFF) {
		if (__rtl8169_get_wol(tp) & WAKE_ANY) {
			rtl_wol_suspend_quirk(tp);
			rtl_wol_shutdown_quirk(tp);
		}

		pci_wake_from_d3(pdev, true);
		pci_set_power_state(pdev, PCI_D3hot);
	}
#endif

	pm_runtime_put_noidle(d);
}

static void rtl_remove_one(struct pci_device *pdev)
{
	struct ether *dev = pci_get_drvdata(pdev);
	struct rtl8169_private *tp = netdev_priv(dev);

	if ((tp->mac_version == RTL_GIGA_MAC_VER_27 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_28 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_31 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_49 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_50 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_51) &&
	    r8168_check_dash(tp)) {
		rtl8168_driver_stop(tp);
	}

	netif_napi_del(&tp->napi);

	unregister_netdev(dev);

	dma_free_coherent(&tp->pci_dev->device, sizeof(*tp->counters),
			  tp->counters, tp->counters_phys_addr);

	rtl_release_firmware(tp);

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->device);

#if 0 // AKAROS_PORT
	/* restore original MAC address */
	rtl_rar_set(tp, dev->perm_addr);
#endif


	rtl_disable_msi(pdev, tp);
	rtl8169_release_board(pdev, dev, tp->mmio_addr);
}

#if 0 // AKAROS_PORT
static const struct net_device_ops rtl_netdev_ops = {
	.ndo_open		= rtl_open,
	.ndo_stop		= rtl8169_close,
	.ndo_get_stats64	= rtl8169_get_stats64,
	.ndo_start_xmit		= rtl8169_start_xmit,
	.ndo_tx_timeout		= rtl8169_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= rtl8169_change_mtu,
	.ndo_fix_features	= rtl8169_fix_features,
	.ndo_set_features	= rtl8169_set_features,
	.ndo_set_mac_address	= rtl_set_mac_address,
	.ndo_do_ioctl		= rtl8169_ioctl,
	.ndo_set_rx_mode	= rtl_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= rtl8169_netpoll,
#endif

};
#endif

static const struct rtl_cfg_info {
	void (*hw_start)(struct ether *);
	unsigned int region;
	unsigned int align;
	uint16_t event_slow;
	unsigned features;
	uint8_t default_ver;
} rtl_cfg_infos [] = {
	[RTL_CFG_0] = {
		.hw_start	= rtl_hw_start_8169,
		.region		= 1,
		.align		= 0,
		.event_slow	= SYSErr | LinkChg | RxOverflow | RxFIFOOver,
		.features	= RTL_FEATURE_GMII,
		.default_ver	= RTL_GIGA_MAC_VER_01,
	},
	[RTL_CFG_1] = {
		.hw_start	= rtl_hw_start_8168,
		.region		= 2,
		.align		= 8,
		.event_slow	= SYSErr | LinkChg | RxOverflow,
		.features	= RTL_FEATURE_GMII | RTL_FEATURE_MSI,
		.default_ver	= RTL_GIGA_MAC_VER_11,
	},
	[RTL_CFG_2] = {
		.hw_start	= rtl_hw_start_8101,
		.region		= 2,
		.align		= 8,
		.event_slow	= SYSErr | LinkChg | RxOverflow | RxFIFOOver |
				  PCSTimeout,
		.features	= RTL_FEATURE_MSI,
		.default_ver	= RTL_GIGA_MAC_VER_13,
	}
};

/* Cfg9346_Unlock assumed. */
static unsigned rtl_try_msi(struct rtl8169_private *tp,
			    const struct rtl_cfg_info *cfg)
{
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned msi = 0;
	uint8_t cfg2;

	cfg2 = RTL_R8(Config2) & ~MSIEnable;
	if (cfg->features & RTL_FEATURE_MSI) {
/* Akaros will attempt to support MSI if the device has it.  It'll do the MSI
 * setup at run time.  It'll actually try for MSI-X, then MSI, then INT.  Here,
 * we'll assume MSI(X) will work. */
#if 0 // AKAROS_PORT
		if (pci_enable_msi(tp->pci_dev)) {
#else
		if (0) {
#endif
			netif_info(tp, hw, tp->dev, "no MSI. Back to INTx.\n");
		} else {
			cfg2 |= MSIEnable;
			msi = RTL_FEATURE_MSI;
		}
	}
	if (tp->mac_version <= RTL_GIGA_MAC_VER_06)
		RTL_W8(Config2, cfg2);
	return msi;
}

DECLARE_RTL_COND(rtl_link_list_ready_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return RTL_R8(MCU) & LINK_LIST_RDY;
}

DECLARE_RTL_COND(rtl_rxtx_empty_cond)
{
	void __iomem *ioaddr = tp->mmio_addr;

	return (RTL_R8(MCU) & RXTX_EMPTY) == RXTX_EMPTY;
}

static void rtl_hw_init_8168g(struct rtl8169_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t data;

	tp->ocp_base = OCP_STD_PHY_BASE;

	RTL_W32(MISC, RTL_R32(MISC) | RXDV_GATED_EN);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_txcfg_empty_cond, 100, 42))
		return;

	if (!rtl_udelay_loop_wait_high(tp, &rtl_rxtx_empty_cond, 100, 42))
		return;

	RTL_W8(ChipCmd, RTL_R8(ChipCmd) & ~(CmdTxEnb | CmdRxEnb));
	msleep(1);
	RTL_W8(MCU, RTL_R8(MCU) & ~NOW_IS_OOB);

	data = r8168_mac_ocp_read(tp, 0xe8de);
	data &= ~(1 << 14);
	r8168_mac_ocp_write(tp, 0xe8de, data);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;

	data = r8168_mac_ocp_read(tp, 0xe8de);
	data |= (1 << 15);
	r8168_mac_ocp_write(tp, 0xe8de, data);

	if (!rtl_udelay_loop_wait_high(tp, &rtl_link_list_ready_cond, 100, 42))
		return;
}

static void rtl_hw_init_8168ep(struct rtl8169_private *tp)
{
	rtl8168ep_stop_cmac(tp);
	rtl_hw_init_8168g(tp);
}

static void rtl_hw_initialize(struct rtl8169_private *tp)
{
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
		rtl_hw_init_8168g(tp);
		break;
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		rtl_hw_init_8168ep(tp);
		break;
	default:
		break;
	}
}

static int rtl_init_one(struct ether *dev, struct pci_device *pdev,
			const struct pci_device_id *ent)
{
	const struct rtl_cfg_info *cfg = rtl_cfg_infos + ent->driver_data;
	const unsigned int region = cfg->region;
	struct rtl8169_private *tp;
	struct mii_if_info *mii;
	void __iomem *ioaddr;
	int chipset, i;
	int rc;

	if (netif_msg_drv(&debug)) {
		printk(KERN_INFO "%s Gigabit Ethernet driver %s loaded\n",
		       MODULENAME, RTL8169_VERSION);
	}

	SET_NETDEV_DEV(dev, &pdev->device);
	tp = netdev_priv(dev);
	tp->dev = dev;
	tp->pci_dev = pdev;
	tp->msg_enable = netif_msg_init(debug.msg_enable, R8169_MSG_DEFAULT);

	mii = &tp->mii;
	mii->dev = dev;
	mii->mdio_read = rtl_mdio_read;
	mii->mdio_write = rtl_mdio_write;
	mii->phy_id_mask = 0x1f;
	mii->reg_num_mask = 0x1f;
	mii->supports_gmii = !!(cfg->features & RTL_FEATURE_GMII);

	/* disable ASPM completely as that cause random device stop working
	 * problems as well as full system hangs for some PCIe devices users */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1 |
				     PCIE_LINK_STATE_CLKPM);

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device(pdev);
	if (rc < 0) {
		netif_err(tp, probe, dev, "enable failure\n");
		goto out;
	}

	if (pci_set_mwi(pdev) < 0)
		netif_info(tp, probe, dev, "Mem-Wr-Inval unavailable\n");

	/* make sure PCI base addr 1 is MMIO */
	if (!(pci_resource_flags(pdev, region) & IORESOURCE_MEM)) {
		netif_err(tp, probe, dev,
			  "region #%d not an MMIO resource, aborting\n",
			  region);
		rc = -ENODEV;
		goto err_out_mwi_2;
	}

	/* check for weird/broken PCI region reporting */
	if (pci_resource_len(pdev, region) < R8169_REGS_SIZE) {
		netif_err(tp, probe, dev,
			  "Invalid PCI region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out_mwi_2;
	}

	rc = pci_request_regions(pdev, MODULENAME);
	if (rc < 0) {
		netif_err(tp, probe, dev, "could not request regions\n");
		goto err_out_mwi_2;
	}

	/* ioremap MMIO region */
	ioaddr = ioremap(pci_resource_start(pdev, region), R8169_REGS_SIZE);
	if (!ioaddr) {
		netif_err(tp, probe, dev, "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res_3;
	}
	tp->mmio_addr = ioaddr;

	if (!pci_is_pcie(pdev))
		netif_info(tp, probe, dev, "not PCI Express\n");

	/* Identify chip attached to board */
	rtl8169_get_mac_version(tp, dev, cfg->default_ver);

	tp->cp_cmd = 0;

	if ((sizeof(dma_addr_t) > 4) &&
	    (use_dac == 1 || (use_dac == -1 && pci_is_pcie(pdev) &&
			      tp->mac_version >= RTL_GIGA_MAC_VER_18)) &&
	    !pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {

		/* CPlusCmd Dual Access Cycle is only needed for non-PCIe */
		if (!pci_is_pcie(pdev))
			tp->cp_cmd |= PCIDAC;
		dev->feat |= NETIF_F_HIGHDMA;
	} else {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc < 0) {
			netif_err(tp, probe, dev, "DMA configuration failed\n");
			goto err_out_unmap_4;
		}
	}

	rtl_init_rxcfg(tp);

	rtl_irq_disable(tp);

	rtl_hw_initialize(tp);

	rtl_hw_reset(tp);

	rtl_ack_events(tp, 0xffff);

	pci_set_bus_master(pdev);

	rtl_init_mdio_ops(tp);
	rtl_init_pll_power_ops(tp);
	rtl_init_jumbo_ops(tp);
	rtl_init_csi_ops(tp);

	rtl8169_print_mac_version(tp);

	chipset = tp->mac_version;
	tp->txd_version = rtl_chip_infos[chipset].txd_version;

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W8(Config1, RTL_R8(Config1) | PMEnable);
	RTL_W8(Config5, RTL_R8(Config5) & (BWF | MWF | UWF | LanWake | PMEStatus));
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
	case RTL_GIGA_MAC_VER_35:
	case RTL_GIGA_MAC_VER_36:
	case RTL_GIGA_MAC_VER_37:
	case RTL_GIGA_MAC_VER_38:
	case RTL_GIGA_MAC_VER_40:
	case RTL_GIGA_MAC_VER_41:
	case RTL_GIGA_MAC_VER_42:
	case RTL_GIGA_MAC_VER_43:
	case RTL_GIGA_MAC_VER_44:
	case RTL_GIGA_MAC_VER_45:
	case RTL_GIGA_MAC_VER_46:
	case RTL_GIGA_MAC_VER_47:
	case RTL_GIGA_MAC_VER_48:
	case RTL_GIGA_MAC_VER_49:
	case RTL_GIGA_MAC_VER_50:
	case RTL_GIGA_MAC_VER_51:
		if (rtl_eri_read(tp, 0xdc, ERIAR_EXGMAC) & MagicPacket_v2)
			tp->features |= RTL_FEATURE_WOL;
		if ((RTL_R8(Config3) & LinkUp) != 0)
			tp->features |= RTL_FEATURE_WOL;
		break;
	default:
		if ((RTL_R8(Config3) & (LinkUp | MagicPacket)) != 0)
			tp->features |= RTL_FEATURE_WOL;
		break;
	}
	if ((RTL_R8(Config5) & (UWF | BWF | MWF)) != 0)
		tp->features |= RTL_FEATURE_WOL;
	tp->features |= rtl_try_msi(tp, cfg);
	RTL_W8(Cfg9346, Cfg9346_Lock);

	if (rtl_tbi_enabled(tp)) {
		tp->set_speed = rtl8169_set_speed_tbi;
#if 0 // AKAROS_PORT
		tp->get_link_ksettings = rtl8169_get_link_ksettings_tbi;
#endif
		tp->phy_reset_enable = rtl8169_tbi_reset_enable;
		tp->phy_reset_pending = rtl8169_tbi_reset_pending;
		tp->link_ok = rtl8169_tbi_link_ok;
#if 0 // AKAROS_PORT
		tp->do_ioctl = rtl_tbi_ioctl;
#endif
	} else {
		tp->set_speed = rtl8169_set_speed_xmii;
#if 0 // AKAROS_PORT
		tp->get_link_ksettings = rtl8169_get_link_ksettings_xmii;
#endif
		tp->phy_reset_enable = rtl8169_xmii_reset_enable;
		tp->phy_reset_pending = rtl8169_xmii_reset_pending;
		tp->link_ok = rtl8169_xmii_link_ok;
#if 0 // AKAROS_PORT
		tp->do_ioctl = rtl_xmii_ioctl;
#endif
	}

	qlock_init(&tp->wk.mutex);
	u64_stats_init(&tp->rx_stats.syncp);
	u64_stats_init(&tp->tx_stats.syncp);

	/* Get MAC address */
	if (tp->mac_version == RTL_GIGA_MAC_VER_35 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_36 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_37 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_38 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_40 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_41 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_42 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_43 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_44 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_45 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_46 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_47 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_48 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_49 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_50 ||
	    tp->mac_version == RTL_GIGA_MAC_VER_51) {
		uint16_t mac_addr[3];

		*(uint32_t *)&mac_addr[0] = rtl_eri_read(tp, 0xe0, ERIAR_EXGMAC);
		*(uint16_t *)&mac_addr[2] = rtl_eri_read(tp, 0xe4, ERIAR_EXGMAC);

		if (is_valid_ether_addr((uint8_t *)mac_addr))
			rtl_rar_set(tp, (uint8_t *)mac_addr);
	}
	for (i = 0; i < Eaddrlen; i++)
		dev->ea[i] = RTL_R8(MAC0 + i);

	netif_napi_add(dev, &tp->napi, rtl8169_poll, R8169_NAPI_WEIGHT);

	/* don't enable SG, IP_CSUM and TSO by default - it might not work
	 * properly for all devices */
	dev->feat |= NETIF_F_RXCSUM |
		NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;
	/* AKAROS: We don't have a way (currently) to turn on certain features
	 * at runtime.  Linux's NIC didn't want SG, CSUM, and TSO by default,
	 * but we'll turn it on until we find a NIC with a problem. */
	dev->feat |= NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO;

	/* AKAROS: Some versions don't pad to the mintu. */
	switch (tp->mac_version) {
	case RTL_GIGA_MAC_VER_34:
		break;
	default:
		dev->feat |= NETF_PADMIN;
	}

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_TSO |
		NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX;

	tp->cp_cmd |= RxChkSum | RxVlan;

	/*
	 * Pretend we are using VLANs; This bypasses a nasty bug where
	 * Interrupts stop flowing on high load on 8110SCd controllers.
	 */
	if (tp->mac_version == RTL_GIGA_MAC_VER_05)
		/* Disallow toggling */
		dev->hw_features &= ~NETIF_F_HW_VLAN_CTAG_RX;

	if (tp->txd_version == RTL_TD_0)
		tp->tso_csum = rtl8169_tso_csum_v1;
	else if (tp->txd_version == RTL_TD_1) {
		tp->tso_csum = rtl8169_tso_csum_v2;
		dev->hw_features |= NETIF_F_IPV6_CSUM | NETIF_F_TSO6;
	} else
		warn_on_once(1);

	dev->hw_features |= NETIF_F_RXALL;
	dev->hw_features |= NETIF_F_RXFCS;

	/* MTU range: 60 - hw-specific max */
	dev->min_mtu = ETH_ZLEN;
	dev->max_mtu = rtl_chip_infos[chipset].jumbo_max;

	tp->hw_start = cfg->hw_start;
	tp->event_slow = cfg->event_slow;

	tp->opts1_mask = (tp->mac_version != RTL_GIGA_MAC_VER_01) ?
		~(RxBOVF | RxFOVF) : ~0;

	setup_timer(&tp->timer, rtl8169_phy_timer, (unsigned long)dev);

	tp->rtl_fw = RTL_FIRMWARE_UNKNOWN;

	tp->counters = dma_zalloc_coherent(&pdev->device, sizeof(*tp->counters),
					   &tp->counters_phys_addr, MEM_WAIT);
	if (!tp->counters) {
		rc = -ENOMEM;
		goto err_out_msi_5;
	}

	rc = register_netdev(dev);
	if (rc < 0)
		goto err_out_cnt_6;

	pci_set_drvdata(pdev, dev);

	netif_info(tp, probe, dev, "%s at 0x%lx, %E, XID %08x IRQ %d\n",
		   rtl_chip_infos[chipset].name, ioaddr, dev->ea,
		   (uint32_t)(RTL_R32(TxConfig) & 0x9cf0f8ff), pdev->irqline);
	if (rtl_chip_infos[chipset].jumbo_max != JUMBO_1K) {
		netif_info(tp, probe, dev, "jumbo features [frames: %d bytes, "
			   "tx checksumming: %s]\n",
			   rtl_chip_infos[chipset].jumbo_max,
			   rtl_chip_infos[chipset].jumbo_tx_csum ? "ok" : "ko");
	}

	if ((tp->mac_version == RTL_GIGA_MAC_VER_27 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_28 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_31 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_49 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_50 ||
	     tp->mac_version == RTL_GIGA_MAC_VER_51) &&
	    r8168_check_dash(tp)) {
		rtl8168_driver_start(tp);
	}

	device_set_wakeup_enable(&pdev->device,
				 tp->features & RTL_FEATURE_WOL);

	if (pci_dev_run_wake(pdev))
		pm_runtime_put_noidle(&pdev->device);

	netif_carrier_off(dev);

out:
	return rc;

err_out_cnt_6:
	dma_free_coherent(&pdev->device, sizeof(*tp->counters), tp->counters,
			  tp->counters_phys_addr);
err_out_msi_5:
	netif_napi_del(&tp->napi);
	rtl_disable_msi(pdev, tp);
err_out_unmap_4:
	iounmap(ioaddr);
err_out_free_res_3:
	pci_release_regions(pdev);
err_out_mwi_2:
	pci_clear_mwi(pdev);
	pci_disable_device(pdev);
	goto out;
}

#if 0 // AKAROS_PORT
static struct pci_driver rtl8169_pci_driver = {
	.name		= MODULENAME,
	.id_table	= rtl8169_pci_tbl,
	.probe		= rtl_init_one,
	.remove		= rtl_remove_one,
	.shutdown	= rtl_shutdown,
	.driver.pm	= RTL8169_PM_OPS,
};

module_pci_driver(rtl8169_pci_driver);
#endif

static spinlock_t rtl_9ns_lock = SPINLOCK_INITIALIZER;
TAILQ_HEAD(rtl_tq, rtl8169_private);
static struct rtl_tq rtl_tq = TAILQ_HEAD_INITIALIZER(rtl_tq);

static void rtl8169_attach(struct ether *edev)
{
	struct rtl8169_private *tp = edev->ctlr;
	char *task_name;

	spin_lock(&rtl_9ns_lock);
	if (tp->attached) {
		spin_unlock(&rtl_9ns_lock);
		return;
	}
	/* If we fail to actually attach, we can either undo this, or just not
	 * attempt to attach again (which will most likely fail again). */
	tp->attached = TRUE;
	spin_unlock(&rtl_9ns_lock);

	if (rtl_open(edev)) {
		warn("Unable to attach rtl8169!");
		return;
	}
	/* If we want an rproc, nows the time to ktask it */
	task_name = kmalloc(KNAMELEN, MEM_WAIT);
	snprintf(task_name, KNAMELEN, "#l%d_irq_ktask", edev->ctlrno);
	ktask(task_name, rtl8169_irq_ktask, edev);
}

static void __rtl_xmit_poke(void *args)
{
	struct ether *edev = ((struct rtl_poke_args*)args)->edev;
	struct rtl8169_private *tp = ((struct rtl_poke_args*)args)->tp;
	struct block *bp;

	while (TX_FRAGS_READY_FOR(tp, MAX_SKB_FRAGS)) {
		bp = qget(edev->oq);
		if (!bp)
			break;
		/* TODO: If this is a problem, we can peak at the first block
		 * and wait til we have enough room.  Though we are still capped
		 * by the max TX possible, and it might not be worth doing
		 * (leaving the NIC idle and blasting packets.
		 *
		 * We probably need qclone to respect MAX_SKB_FRAGS or some
		 * other system-wide constant. */
		if (bp->nr_extra_bufs > MAX_SKB_FRAGS)
			bp = linearizeblock(bp);
		rtl8169_start_xmit(bp, edev);
	}
}

static void rtl8169_transmit(struct ether *edev)
{
	struct rtl8169_private *tp = netdev_priv(edev);
	struct rtl_poke_args args[1];

	args->edev = edev;
	args->tp = tp;
	poke(&tp->poker, args);
}

static long rtl8169_ifstat(struct ether *edev, void *a, long n, uint32_t offset)
{
	rtl8169_get_stats64(edev, &edev->stats);
	return linux_ifstat(&edev->stats, a, n, offset);
}

static long rtl8169_ctl(struct ether *edev, void *buf, size_t n)
{
	ERRSTACK(1);
	int v;
	char *p;
	struct bnx2x *ctlr;
	struct cmdbuf *cb;
	struct cmdtab *ct;

	ctlr = edev->ctlr;
	if (!ctlr)
		error(ENODEV, "edev has no controller!");
	cb = parsecmd(buf, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	if (cb->nf < 1)
		error(EFAIL, "short control request");

	/* TODO: handle ctl command somehow.  igbe did the following: */
	//ct = lookupcmd(cb, igbectlmsg, ARRAY_SIZE(igbectlmsg));

	kfree(cb);
	poperror();
	return n;
}

static void rtl8169_shutdown(struct ether *ether)
{
	struct rtl8169_private *tp = netdev_priv(ether);

	/* 9ns doesn't even call into devether shutdown - not sure what the hell
	 * is going on there.  Completely untested, and I don't know if the
	 * order of shutdown / remove_one is right, or if other things need to
	 * be done. */
	warn("Untested NIC shutdown!");
	rtl_shutdown(tp->pcidev);
	rtl_remove_one(tp->pcidev);
}

static void rtl8169_promiscuous(void *arg, int on)
{
	struct ether *edev = arg;

	if (on)
		edev->rx_mode |= IFF_PROMISC;
	else
		edev->rx_mode &= ~IFF_PROMISC;
	rtl_set_rx_mode(edev);
}

/* For every RTL device, we alloc a controller, but do little else until
 * devether asks us to.  These aren't attached to ether devices yet. */
static void rtl8169_pci(void)
{
	struct pci_device *pcidev;
	const struct pci_device_id *pci_id;
	struct rtl8169_private *ctlr;

	STAILQ_FOREACH(pcidev, &pci_devices, all_dev) {
		/* This checks that pcidev is a Network Controller for Ethernet
		 */
		if (pcidev->class != 0x02 || pcidev->subclass != 0x00)
			continue;
		pci_id = srch_linux_pci_tbl(rtl8169_pci_tbl, pcidev);
		if (!pci_id)
			continue;
		/* If we have a module init method, call it here via run_once()
		 */
		printk("rtl8169 driver found 0x%04x:%04x at %02x:%02x.%x\n",
			   pcidev->ven_id, pcidev->dev_id,
			   pcidev->bus, pcidev->dev, pcidev->func);

		ctlr = kzmalloc(sizeof(struct rtl8169_private), MEM_WAIT);
		rendez_init(&ctlr->irq_task);
		poke_init(&ctlr->poker, __rtl_xmit_poke);
		ctlr->pcidev = pcidev;
		ctlr->pci_id = pci_id;

		spin_lock(&rtl_9ns_lock);
		TAILQ_INSERT_TAIL(&rtl_tq, ctlr, link9ns);
		spin_unlock(&rtl_9ns_lock);
	}
}

/* Called by devether's probe routines.  Return -1 if the edev does not match
 * any of your ctlrs.
 *
 * Each controller must be used only once.  They were set up in rtl8169_pci.
 * We're responsible for initializing edev, providing the connections from
 * devether to this driver. */
static int rtl8169_pnp(struct ether *edev)
{
	struct rtl8169_private *ctlr;
	struct pci_device *pcidev;
	int rc;

	run_once(rtl8169_pci());

	spin_lock(&rtl_9ns_lock);
	TAILQ_FOREACH(ctlr, &rtl_tq, link9ns) {
		/* just take the first inactive ctlr on the list */
		if (ctlr->active)
			continue;
		ctlr->active = 1;
		break;
	}
	spin_unlock(&rtl_9ns_lock);
	if (ctlr == NULL)
		return -1;

	edev->ctlr = ctlr;
	ctlr->edev = edev;
	pcidev = ctlr->pcidev;
	strlcpy(edev->drv_name, "r8169", KNAMELEN);

	/* Unlike bnx2x, we need to do the device init early so we can extract
	 * info like the MAC addr. */
	rc = rtl_init_one(edev, pcidev, ctlr->pci_id);
	if (rc) {
		printk("Failed to init r8169 0x%04x:%04x at %02x:%02x.%x\n",
			   pcidev->ven_id, pcidev->dev_id,
			   pcidev->bus, pcidev->dev, pcidev->func);
		/* We could clear 'active', but if it failed once, it'll fail
		 * again. */
		return -1;
	}

	edev->irq = pcidev->irqline;
	edev->tbdf = MKBUS(BusPCI, pcidev->bus, pcidev->dev, pcidev->func);
	/* edev->mbps will get set during attach->rtl8169_set_speed. */

	edev->attach = rtl8169_attach;
	edev->transmit = rtl8169_transmit;
	edev->ifstat = rtl8169_ifstat;
	edev->ctl = rtl8169_ctl;
	edev->shutdown = rtl8169_shutdown;

	edev->arg = edev;
	edev->promiscuous = rtl8169_promiscuous;
	edev->multicast = NULL;	/* see note in rtl_set_rx_mode */

	return 0;
}

linker_func_3(ether_rtl8169_link)
{
	addethercard("r8169", rtl8169_pnp);
}
