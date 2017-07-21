/* Copyright (c) 2015 Google Inc.
 *
 * Dumping ground for converting between Akaros and Linux. */

#pragma once

#define ROS_KERN_LINUX_COMPAT_H

/* Common headers that most driver files will need */

#include <ros/common.h>
#include <assert.h>
#include <error.h>
#include <net/ip.h>
#include <kmalloc.h>
#include <kref.h>
#include <pmap.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <bitmap.h>
#include <umem.h>
#include <mmio.h>
#include <taskqueue.h>
#include <zlib.h>
#include <list.h>
#include <refd_pages.h>
#include <linux/errno.h>
/* temporary dumping ground */
#include "compat_todo.h"

//#define CONFIG_DCB
//#define CONFIG_NET_RX_BUSY_POLL 1
//#define CONFIG_NET_POLL_CONTROLLER 1
//#define CONFIG_INET 1 	// will deal with this manually
#define CONFIG_PCI_MSI 1

#define __rcu
#define rcu_read_lock()
#define rcu_read_unlock()
#define rcu_dereference(x) (x)
#define rcu_dereference_protected(x, y) (x)
#ifndef rcu_assign_pointer
#define rcu_assign_pointer(dst, src) (dst) = (src)
#endif
#define RCU_INIT_POINTER(dst, src) rcu_assign_pointer(dst, src)
#define synchronize_rcu()

#define atomic_cmpxchg(_addr, _old, _new)                                      \
({                                                                             \
	typeof(_old) _ret;                                                         \
	if (atomic_cas((_addr), (_old), (_new)))                                   \
		_ret = _old;                                                           \
	else                                                                       \
		_ret = atomic_read(_addr);                                             \
	_ret;                                                                      \
})

#define UINT_MAX UINT64_MAX
#define L1_CACHE_SHIFT (LOG2_UP(ARCH_CL_SIZE))
#define __stringify(x...) STRINGIFY(x)

/* Wanted to keep the _t variants in the code, in case that's useful in the
 * future */
#define MIN_T(t, a, b) MIN(a, b)
#define MAX_T(t, a, b) MAX(a, b)
#define CLAMP(val, lo, hi) MIN((typeof(val))MAX(val, lo), hi)
#define CLAMP_T(t, val, lo, hi) CLAMP(val, lo, hi)

typedef physaddr_t dma_addr_t;
typedef int gfp_t;

/* these dma funcs are empty in linux with !CONFIG_NEED_DMA_MAP_STATE */
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};

static inline void *__dma_alloc_coherent(size_t size, dma_addr_t *dma_handle,
                                         gfp_t flags)
{
	void *vaddr = get_cont_pages(LOG2_UP(nr_pages(size)), flags);

	if (!vaddr) {
		*dma_handle = 0;
		return 0;
	}
	*dma_handle = PADDR(vaddr);
	return vaddr;
}

static inline void *__dma_zalloc_coherent(size_t size, dma_addr_t *dma_handle,
                                          gfp_t flags)
{
	void *vaddr = __dma_alloc_coherent(size, dma_handle, flags);
	if (vaddr)
		memset(vaddr, 0, size);
	return vaddr;
}

static inline void __dma_free_coherent(size_t size, void *cpu_addr,
                                       dma_addr_t dma_handle)
{
	free_cont_pages(cpu_addr, LOG2_UP(nr_pages(size)));
}

static inline dma_addr_t __dma_map_single(void *cpu_addr, size_t size,
                                          int direction)
{
	return PADDR(cpu_addr);
}

static inline dma_addr_t __dma_map_page(struct page *page,
                                        unsigned long offset, size_t size,
                                        int direction)
{
	assert(offset == 0);
	return page2pa(page);
}

static inline int __dma_mapping_error(dma_addr_t dma_addr)
{
	return (dma_addr == 0);
}

#define dma_unmap_single(...)
#define dma_unmap_page(...)
#define dma_set_mask_and_coherent(...) (0)
#define dma_sync_single_for_cpu(...)
#define dma_sync_single_for_device(...)

/* Wrappers to avoid struct device.  Might want that one of these days */
#define dma_alloc_coherent(dev, size, dma_handlep, flag)                       \
	__dma_alloc_coherent(size, dma_handlep, flag)

#define dma_zalloc_coherent(dev, size, dma_handlep, flag)                      \
	__dma_zalloc_coherent(size, dma_handlep, flag)

#define dma_free_coherent(dev, size, dma_handle, flag)                         \
	__dma_free_coherent(size, dma_handle, flag)

#define dma_map_single(dev, addr, size, direction)                             \
	__dma_map_single(addr, size, direction)

#define dma_map_page(dev, page, offset, size, direction)                       \
	__dma_map_page(page, offset, size, direction)

#define dma_mapping_error(dev, handle)                                         \
	__dma_mapping_error(handle)

static void *vmalloc(size_t size)
{
	void *vaddr = get_cont_pages(LOG2_UP(nr_pages(size)), MEM_WAIT);
	/* zalloc, to be safe */
	if (vaddr)
		memset(vaddr, 0, size);
	return vaddr;
}

/* Akaros needs to know the size, for now.  So it's not quite compatible */
static void vfree(void *vaddr, size_t size)
{
	free_cont_pages(vaddr, LOG2_UP(nr_pages(size)));
}

typedef int pci_power_t;
typedef int pm_message_t;

#define DEFINE_SEMAPHORE(name)  \
    struct semaphore name = SEMAPHORE_INITIALIZER_IRQSAVE(name, 1)
#define sema_init(sem, val) sem_init_irqsave(sem, val)
#define up(sem) sem_up(sem)
#define down(sem) sem_down(sem)
#define down_trylock(sem) ({!sem_trydown(sem);})
/* In lieu of spatching, I wanted to keep the distinction between down and
 * down_interruptible/down_timeout.  Akaros doesn't have the latter. */
#define down_interruptible(sem) ({sem_down(sem); 0;})
#define down_timeout(sem, timeout) ({sem_down(sem); 0;})

#define local_bh_disable() cmb()
#define local_bh_enable() cmb()

/* Linux printk front ends */
#ifndef pr_fmt
#define pr_fmt(fmt) "bnx2x:" fmt
#endif

#define KERN_EMERG ""
#define KERN_ALERT ""
#define KERN_CRIT ""
#define KERN_ERR ""
#define KERN_WARNING ""
#define KERN_NOTICE ""
#define KERN_INFO ""
#define KERN_CONT ""
#define KERN_DEBUG ""

/*
 * These can be used to print at the various log levels.
 * All of these will print unconditionally, although note that pr_debug()
 * and other debug macros are compiled out unless either DEBUG is defined
 * or CONFIG_DYNAMIC_DEBUG is set.
 */
#define pr_emerg(fmt, ...) \
	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...) \
	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...) \
	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning(fmt, ...) \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn pr_warning
#define pr_notice(fmt, ...) \
	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont(fmt, ...) \
	printk(KERN_CONT pr_fmt(fmt), ##__VA_ARGS__)
#define netdev_printk(lvl, dev, fmt, ...) \
	printk("[netdev]: " fmt, ##__VA_ARGS__)
#define netdev_err(dev, fmt, ...) \
	printk("[netdev]: " fmt, ##__VA_ARGS__)
#define netdev_info(dev, fmt, ...) \
	printk("[netdev]: " fmt, ##__VA_ARGS__)
#define netdev_dbg(dev, fmt, ...) \
	printk("[netdev]: " fmt, ##__VA_ARGS__)
#define dev_err(dev, fmt, ...) \
	printk("[dev]: " fmt, ##__VA_ARGS__)
#define dev_info(dev, fmt, ...) \
	printk("[dev]: " fmt, ##__VA_ARGS__)
#define dev_alert(dev, fmt, ...) \
	printk("[dev]: " fmt, ##__VA_ARGS__)

#ifdef DEBUG

#define might_sleep() assert(can_block(&per_cpu_info[core_id()]))
#define pr_devel(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

#else

#define might_sleep()
#define pr_devel(fmt, ...) \
	printd(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

#endif
#define pr_debug pr_devel


enum {
	NETIF_MSG_DRV		= 0x0001,
	NETIF_MSG_PROBE		= 0x0002,
	NETIF_MSG_LINK		= 0x0004,
	NETIF_MSG_TIMER		= 0x0008,
	NETIF_MSG_IFDOWN	= 0x0010,
	NETIF_MSG_IFUP		= 0x0020,
	NETIF_MSG_RX_ERR	= 0x0040,
	NETIF_MSG_TX_ERR	= 0x0080,
	NETIF_MSG_TX_QUEUED	= 0x0100,
	NETIF_MSG_INTR		= 0x0200,
	NETIF_MSG_TX_DONE	= 0x0400,
	NETIF_MSG_RX_STATUS	= 0x0800,
	NETIF_MSG_PKTDATA	= 0x1000,
	NETIF_MSG_HW		= 0x2000,
	NETIF_MSG_WOL		= 0x4000,
};

#define MODULE_AUTHOR(...)
#define MODULE_DESCRIPTION(...)
#define MODULE_LICENSE(...)
#define MODULE_VERSION(...)
#define MODULE_FIRMWARE(...)
#define module_param(...)
#define module_param_named(...)
#define MODULE_PARM_DESC(...)
#define MODULE_DEVICE_TABLE(...)
#define THIS_MODULE ((void*)0)
#define EXPORT_SYMBOL(...)
#define __init
#define __exit
#define module_init(...)
#define module_exit(...)

#define is_kdump_kernel() (0)

/* from Linux's ethtool.h.  We probably won't use any of this code, but at
 * least we can keep it quiet during porting. */
#define SPEED_10        10
#define SPEED_100       100
#define SPEED_1000      1000
#define SPEED_2500      2500
#define SPEED_10000     10000
#define SPEED_20000     20000
#define SPEED_40000     40000
#define SPEED_56000     56000
#define SPEED_UNKNOWN   -1

/* Duplex, half or full. */
#define DUPLEX_HALF     0x00
#define DUPLEX_FULL     0x01
#define DUPLEX_UNKNOWN  0xff

#define SUPPORTED_10baseT_Half      (1 << 0)
#define SUPPORTED_10baseT_Full      (1 << 1)
#define SUPPORTED_100baseT_Half     (1 << 2)
#define SUPPORTED_100baseT_Full     (1 << 3)
#define SUPPORTED_1000baseT_Half    (1 << 4)
#define SUPPORTED_1000baseT_Full    (1 << 5)
#define SUPPORTED_Autoneg       (1 << 6)
#define SUPPORTED_TP            (1 << 7)
#define SUPPORTED_AUI           (1 << 8)
#define SUPPORTED_MII           (1 << 9)
#define SUPPORTED_FIBRE         (1 << 10)
#define SUPPORTED_BNC           (1 << 11)
#define SUPPORTED_10000baseT_Full   (1 << 12)
#define SUPPORTED_Pause         (1 << 13)
#define SUPPORTED_Asym_Pause        (1 << 14)
#define SUPPORTED_2500baseX_Full    (1 << 15)
#define SUPPORTED_Backplane     (1 << 16)
#define SUPPORTED_1000baseKX_Full   (1 << 17)
#define SUPPORTED_10000baseKX4_Full (1 << 18)
#define SUPPORTED_10000baseKR_Full  (1 << 19)
#define SUPPORTED_10000baseR_FEC    (1 << 20)
#define SUPPORTED_20000baseMLD2_Full    (1 << 21)
#define SUPPORTED_20000baseKR2_Full (1 << 22)
#define SUPPORTED_40000baseKR4_Full (1 << 23)
#define SUPPORTED_40000baseCR4_Full (1 << 24)
#define SUPPORTED_40000baseSR4_Full (1 << 25)
#define SUPPORTED_40000baseLR4_Full (1 << 26)
#define SUPPORTED_56000baseKR4_Full (1 << 27)
#define SUPPORTED_56000baseCR4_Full (1 << 28)
#define SUPPORTED_56000baseSR4_Full (1 << 29)
#define SUPPORTED_56000baseLR4_Full (1 << 30)

#define ADVERTISED_10baseT_Half     (1 << 0)
#define ADVERTISED_10baseT_Full     (1 << 1)
#define ADVERTISED_100baseT_Half    (1 << 2)
#define ADVERTISED_100baseT_Full    (1 << 3)
#define ADVERTISED_1000baseT_Half   (1 << 4)
#define ADVERTISED_1000baseT_Full   (1 << 5)
#define ADVERTISED_Autoneg      (1 << 6)
#define ADVERTISED_TP           (1 << 7)
#define ADVERTISED_AUI          (1 << 8)
#define ADVERTISED_MII          (1 << 9)
#define ADVERTISED_FIBRE        (1 << 10)
#define ADVERTISED_BNC          (1 << 11)
#define ADVERTISED_10000baseT_Full  (1 << 12)
#define ADVERTISED_Pause        (1 << 13)
#define ADVERTISED_Asym_Pause       (1 << 14)
#define ADVERTISED_2500baseX_Full   (1 << 15)
#define ADVERTISED_Backplane        (1 << 16)
#define ADVERTISED_1000baseKX_Full  (1 << 17)
#define ADVERTISED_10000baseKX4_Full    (1 << 18)
#define ADVERTISED_10000baseKR_Full (1 << 19)
#define ADVERTISED_10000baseR_FEC   (1 << 20)
#define ADVERTISED_20000baseMLD2_Full   (1 << 21)
#define ADVERTISED_20000baseKR2_Full    (1 << 22)
#define ADVERTISED_40000baseKR4_Full    (1 << 23)
#define ADVERTISED_40000baseCR4_Full    (1 << 24)
#define ADVERTISED_40000baseSR4_Full    (1 << 25)
#define ADVERTISED_40000baseLR4_Full    (1 << 26)
#define ADVERTISED_56000baseKR4_Full    (1 << 27)
#define ADVERTISED_56000baseCR4_Full    (1 << 28)
#define ADVERTISED_56000baseSR4_Full    (1 << 29)
#define ADVERTISED_56000baseLR4_Full    (1 << 30)

enum ethtool_test_flags {
	ETH_TEST_FL_OFFLINE = (1 << 0),
	ETH_TEST_FL_FAILED  = (1 << 1),
	ETH_TEST_FL_EXTERNAL_LB = (1 << 2),
	ETH_TEST_FL_EXTERNAL_LB_DONE    = (1 << 3),
};

enum ethtool_stringset {
	ETH_SS_TEST     = 0,
	ETH_SS_STATS,
	ETH_SS_PRIV_FLAGS,
	ETH_SS_NTUPLE_FILTERS,
	ETH_SS_FEATURES,
	ETH_SS_RSS_HASH_FUNCS,
};

enum {
	ETH_RSS_HASH_TOP_BIT, /* Configurable RSS hash function - Toeplitz */
	ETH_RSS_HASH_XOR_BIT, /* Configurable RSS hash function - Xor */

	ETH_RSS_HASH_FUNCS_COUNT
};

#define __ETH_RSS_HASH_BIT(bit) ((uint32_t)1 << (bit))
#define __ETH_RSS_HASH(name)    __ETH_RSS_HASH_BIT(ETH_RSS_HASH_##name##_BIT)

#define ETH_RSS_HASH_TOP    __ETH_RSS_HASH(TOP)
#define ETH_RSS_HASH_XOR    __ETH_RSS_HASH(XOR)

#define ETH_RSS_HASH_UNKNOWN    0
#define ETH_RSS_HASH_NO_CHANGE  0


/* EEPROM Standards for plug in modules */
#define ETH_MODULE_SFF_8079     0x1
#define ETH_MODULE_SFF_8079_LEN     256
#define ETH_MODULE_SFF_8472     0x2
#define ETH_MODULE_SFF_8472_LEN     512
#define ETH_MODULE_SFF_8636     0x3
#define ETH_MODULE_SFF_8636_LEN     256
#define ETH_MODULE_SFF_8436     0x4
#define ETH_MODULE_SFF_8436_LEN     256

#define ETH_GSTRING_LEN     32

/* ethernet protocol ids.  the plan 9 equivalent enum only exists in
 * ethermedium.c. */
#define ETH_P_IP    0x0800      /* Internet Protocol packet */
#define ETH_P_IPV6  0x86DD      /* IPv6 over bluebook       */
#define ETH_P_ARP   0x0806      /* Address Resolution packet    */
#define ETH_P_FIP   0x8914      /* FCoE Initialization Protocol */
#define ETH_P_8021Q 0x8100          /* 802.1Q VLAN Extended Header  */

/* Sockaddr structs */
struct sockaddr {
	uint16_t				sa_family;
	char					sa_data[14];
};

struct in_addr {
	uint32_t		s_addr;
};
struct sockaddr_in {
	uint16_t				sin_family;
	uint16_t				sin_port;
	struct in_addr			sin_addr;
	uint8_t					sin_zero[8]; /* padding */
};

struct in6_addr {
	/* this is actually a weird union in glibc */
	uint8_t					s6_addr[16];
};

struct sockaddr_in6 {
	uint16_t				sin6_family;
	uint16_t				sin6_port;
	uint32_t				sin6_flowinfo;
	struct in6_addr			sin6_addr;
	uint32_t				sin6_scope_id;
};

/* Common way to go from netdev (ether / netif) to driver-private ctlr */
static inline void *netdev_priv(struct ether *dev)
{
	return dev->ctlr;
}

/* u64 on linux, but a u32 on plan 9.  the typedef is probably a good idea */
typedef unsigned int netdev_features_t;

/* Linux has features, hw_features, and a couple others.  Plan 9 just has
 * features.  This #define should work for merging hw and regular features.  We
 * spatched away the hw_enc and vlan feats. */
#define hw_features feat

/* Attempted conversions for plan 9 features.  For some things, like rx
 * checksums, the driver flags the block (e.g. Budpck) to say if a receive
 * checksum was already done.  There is no flag for saying the device can do
 * it.  For transmits, the stack needs to know in advance if the device can
 * handle the checksum or not. */
#define NETIF_F_RXHASH				0
#define NETIF_F_RXCSUM				0
#define NETIF_F_LRO					NETF_LRO
#define NETIF_F_GRO					0
#define NETIF_F_LOOPBACK			0
#define NETIF_F_TSO					NETF_TSO
#define NETIF_F_SG					NETF_SG
#define NETIF_F_IP_CSUM				(NETF_IPCK | NETF_UDPCK | NETF_TCPCK)
#define NETIF_F_IPV6_CSUM			(NETF_IPCK | NETF_UDPCK | NETF_TCPCK)
#define NETIF_F_GSO_GRE				0
#define NETIF_F_GSO_UDP_TUNNEL		0
#define NETIF_F_GSO_IPIP			0
#define NETIF_F_GSO_SIT				0
#define NETIF_F_TSO_ECN				0
#define NETIF_F_TSO6				0
#define NETIF_F_HW_VLAN_CTAG_TX		0
#define NETIF_F_HIGHDMA				0
#define NETIF_F_HW_VLAN_CTAG_RX		0

#define netif_msg_drv(p)		((p)->msg_enable & NETIF_MSG_DRV)
#define netif_msg_probe(p)		((p)->msg_enable & NETIF_MSG_PROBE)
#define netif_msg_link(p)		((p)->msg_enable & NETIF_MSG_LINK)
#define netif_msg_timer(p)		((p)->msg_enable & NETIF_MSG_TIMER)
#define netif_msg_ifdown(p)		((p)->msg_enable & NETIF_MSG_IFDOWN)
#define netif_msg_ifup(p)		((p)->msg_enable & NETIF_MSG_IFUP)
#define netif_msg_rx_err(p)		((p)->msg_enable & NETIF_MSG_RX_ERR)
#define netif_msg_tx_err(p)		((p)->msg_enable & NETIF_MSG_TX_ERR)
#define netif_msg_tx_queued(p)	((p)->msg_enable & NETIF_MSG_TX_QUEUED)
#define netif_msg_intr(p)		((p)->msg_enable & NETIF_MSG_INTR)
#define netif_msg_tx_done(p)	((p)->msg_enable & NETIF_MSG_TX_DONE)
#define netif_msg_rx_status(p)	((p)->msg_enable & NETIF_MSG_RX_STATUS)
#define netif_msg_pktdata(p)	((p)->msg_enable & NETIF_MSG_PKTDATA)
#define netif_msg_hw(p)			((p)->msg_enable & NETIF_MSG_HW)
#define netif_msg_wol(p)		((p)->msg_enable & NETIF_MSG_WOL)

enum netdev_state_t {
	__LINK_STATE_START,
	__LINK_STATE_PRESENT,
	__LINK_STATE_NOCARRIER,
	__LINK_STATE_LINKWATCH_PENDING,
	__LINK_STATE_DORMANT,
};

enum netdev_tx {
	__NETDEV_TX_MIN	 = INT32_MIN,	/* make sure enum is signed */
	NETDEV_TX_OK	 = 0x00,		/* driver took care of packet */
	NETDEV_TX_BUSY	 = 0x10,		/* driver tx path was busy*/
	NETDEV_TX_LOCKED = 0x20,		/* driver tx lock was already taken */
};
typedef enum netdev_tx netdev_tx_t;

/* Global mutex in linux for "routing netlink".  Not sure if we have an
 * equivalent or not in Plan 9. */
#define rtnl_lock()
#define rtnl_unlock()
#define ASSERT_RTNL(...)

#define synchronize_irq(x) warn_once("Asked to sync IRQ %d, unsupported", x)
#define HZ 100

/* Linux has a PCI device id struct.  Drivers make tables of their supported
 * devices, and this table is handled by higher level systems.  We don't have
 * those systems, but we probably want the table still for our own parsing. */
struct pci_device_id {
	uint32_t vendor, device;		/* Vendor and device ID or PCI_ANY_ID*/
	uint32_t subvendor, subdevice;	/* Subsystem ID's or PCI_ANY_ID */
	uint32_t class, class_mask;		/* (class,subclass,prog-if) triplet */
	unsigned long driver_data;		/* Data private to the driver */
};

#define PCI_ANY_ID (~0)
/* This macro is used in setting device_id entries */
#define PCI_VDEVICE(vend, dev) \
    .vendor = PCI_VENDOR_ID_##vend, .device = (dev), \
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

/* Linux also has its own table of vendor ids.  We have the pci_defs table, but
 * this is a bootstrap issue. */
#define PCI_VENDOR_ID_BROADCOM      0x14e4

/* I'd like to spatch all of the pci methods, but I don't know how to do the
 * reads.  Since we're not doing the reads, then no sense doing the writes. */
static inline int pci_read_config_byte(struct pci_device *dev, uint32_t off,
                                       uint8_t *val)
{
	*val = pcidev_read8(dev, off);
	return 0;
}

static inline int pci_read_config_word(struct pci_device *dev, uint32_t off,
                                       uint16_t *val)
{
	*val = pcidev_read16(dev, off);
	return 0;
}

static inline int pci_read_config_dword(struct pci_device *dev, uint32_t off,
                                        uint32_t *val)
{
	*val = pcidev_read32(dev, off);
	return 0;
}

static inline int pci_write_config_byte(struct pci_device *dev, uint32_t off,
                                        uint8_t val)
{
	pcidev_write8(dev, off, val);
	return 0;
}

static inline int pci_write_config_word(struct pci_device *dev, uint32_t off,
                                        uint16_t val)
{
	pcidev_write16(dev, off, val);
	return 0;
}

static inline int pci_write_config_dword(struct pci_device *dev, uint32_t off,
                                         uint32_t val)
{
	pcidev_write32(dev, off, val);
	return 0;
}

static inline void pci_disable_device(struct pci_device *dev)
{
	pci_clr_bus_master(dev);
}

static inline int pci_enable_device(struct pci_device *dev)
{
	pci_set_bus_master(dev);
	return 0;
}

static inline uint32_t pci_resource_len(struct pci_device *dev, int bir)
{
	return pci_get_membar_sz(dev, bir);
}

static inline void *pci_resource_start(struct pci_device *dev, int bir)
{
	return (void*)pci_get_membar(dev, bir);
}

static inline void *pci_resource_end(struct pci_device *dev, int bir)
{
	return (void*)(pci_get_membar(dev, bir) + pci_resource_len(dev, bir));
}

/* Hacked up version of Linux's.  Assuming reg's are implemented and
 * read_config never fails. */
static int pcie_capability_read_word(struct pci_device *dev, int pos,
                                     uint16_t *val)
{
	uint32_t pcie_cap;
	*val = 0;
	if (pos & 1)
		return -EINVAL;
	if (pci_find_cap(dev, PCI_CAP_ID_EXP, &pcie_cap))
		return -EINVAL;
	pci_read_config_word(dev, pcie_cap + pos, val);
	return 0;
}

#define ioremap_nocache(paddr, sz) \
        (void*)vmap_pmem_nocache((uintptr_t)paddr, sz)
#define ioremap(paddr, sz) (void*)vmap_pmem((uintptr_t)paddr, sz)
#define pci_ioremap_bar(dev, bir) (void*)pci_map_membar(dev, bir)
#define pci_set_master(x) pci_set_bus_master(x)

#define dev_addr_add(dev, addr, type) ({memcpy((dev)->ea, addr, Eaddrlen); 0;})
#define dev_addr_del(...)

#define SET_NETDEV_DEV(...)
#define netif_carrier_off(...)
#define netif_carrier_on(...)
/* May need to do something with edev's queues or flags. */
#define netif_tx_wake_all_queues(...)
#define netif_tx_wake_queue(...)
#define netif_tx_start_all_queues(...)
#define netif_tx_start_queue(...)
#define netif_tx_stop_queue(...)
#define netif_napi_add(...)
#define napi_hash_add(...)
#define napi_enable(...)
#define napi_disable(...)
#define napi_schedule(...)
#define napi_schedule_irqoff(...)
#define napi_complete(...)
/* picks a random, valid mac addr for dev */
#define eth_hw_addr_random(...)
/* checks if the MAC is not 0 and not multicast (all 1s) */
#define is_valid_ether_addr(...) (TRUE)
/* The flag this checks is set on before open.  Turned off on failure, etc. */
#define netif_running(dev) (TRUE)
#define netdev_tx_sent_queue(...)
#define skb_tx_timestamp(...)

#define NET_SKB_PAD 0 		/* padding for SKBs.  Ignoring it for now */
#define MAX_SKB_FRAGS 16	/* we'll probably delete code using this */
#define VLAN_VID_MASK 0x0fff /* VLAN Identifier */

/* Could spatch this:
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
	to:
	if (!pci_get_membar(pdev, 0)) {

	eth_zero_addr(bp->dev->ea);
	to:
	memset(bp->dev->ea, 0, Eaddrlen);
*/

struct firmware {
	const uint8_t *data;
	size_t size;
};

/* the ignored param is a &pcidev->dev in linux, which is a struct dev.  our
 * pcidev->dev is the "slot" */
static inline int request_firmware(const struct firmware **fwp,
                                   const char *file_name, uint8_t *ignored)
{
	struct firmware *ret_fw;
	struct file *fw_file;
	void *fw_data;
	char dirname[] = "/lib/firmware/";
	/* could dynamically allocate the min of this and some MAX */
	char fullpath[sizeof(dirname) + strlen(file_name) + 1];

	snprintf(fullpath, sizeof(fullpath), "%s%s", dirname, file_name);
	fw_file = do_file_open(fullpath, O_READ, 0);
	if (!fw_file) {
		printk("Unable to find firmware file %s!\n", fullpath);
		return -1;
	}
	fw_data = kread_whole_file(fw_file);
	if (!fw_data) {
		printk("Unable to load firmware file %s!\n", fullpath);
		kref_put(&fw_file->f_kref);
		return -1;
	}
	ret_fw = kmalloc(sizeof(struct firmware), MEM_WAIT);
	ret_fw->data = fw_data;
	ret_fw->size = fw_file->f_dentry->d_inode->i_size;
	*fwp = ret_fw;
	kref_put(&fw_file->f_kref);
	return 0;
}

static inline void release_firmware(const struct firmware *fw)
{
	if (fw) {
		kfree((void*)fw->data);
		kfree((void*)fw);
	}
}

static inline uint32_t ethtool_rxfh_indir_default(uint32_t index,
                                                  uint32_t n_rx_rings)
{
	return index % n_rx_rings;
}

/* Plan 9 does a memcmp for this.  We should probably have a helper, like for
 * IP addrs. */
static inline bool ether_addr_equal(const uint8_t *addr1, const uint8_t *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	uint32_t fold = ((*(const uint32_t *)addr1) ^ (*(const uint32_t *)addr2)) |
	((*(const uint16_t *)(addr1 + 4)) ^ (*(const uint16_t *)(addr2 + 4)));

	return fold == 0;
#else
	const uint16_t *a = (const uint16_t *)addr1;
	const uint16_t *b = (const uint16_t *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
#endif
}

#include <linux/compat_todo.h>
