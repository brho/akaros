/* Copyright (c) 2015 Google Inc.
 *
 * Dumping ground for converting between Akaros and other OSs. */

#ifndef ROS_KERN_AKAROS_COMPAT_H
#define ROS_KERN_AKAROS_COMPAT_H

/* Common headers that most driver files will need */

#include <assert.h>
#include <error.h>
#include <ip.h>
#include <kmalloc.h>
#include <kref.h>
#include <pmap.h>
#include <slab.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <bitmap.h>
#include <mii.h>
#include <umem.h>
#include <mmio.h>
#include <taskqueue.h>

#define __rcu
#define unlikely(x) (x)
#define likely(x) (x)
#define UINT_MAX UINT64_MAX
#define L1_CACHE_SHIFT (LOG2_UP(ARCH_CL_SIZE))

/* Wanted to keep the _t variants in the code, in case that's useful in the
 * future */
#define MIN_T(t, a, b) MIN(a, b)
#define MAX_T(t, a, b) MAX(a, b)
#define CLAMP(val, lo, hi) MIN((typeof(val))MAX(val, lo), hi)
#define CLAMP_T(t, val, lo, hi) CLAMP(val, lo, hi)

/* I'm okay with these versions of the asserts. */
#define WARN(args...) warn(args)
#define WARN_ONCE(args...) warn_once(args)
#define WARN_ON(x) warn_on(x)
#define WARN_ON_ONCE(x) warn_on_once(x)
#define BUG(args...) panic(args)
#define BUG_ON(x) assert(x)

typedef physaddr_t dma_addr_t;
typedef int gfp_t;

/* these dma funcs are empty in linux with !CONFIG_NEED_DMA_MAP_STATE */
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
#define DMA_NONE				0
#define DMA_TO_DEVICE			1
#define DMA_FROM_DEVICE			2
#define DMA_BIDIRECTIONAL		3

static inline void *__dma_alloc_coherent(size_t size, dma_addr_t *dma_handle,
                                         gfp_t flags)
{
	size_t order = LOG2_UP(nr_pages(size));
	void *vaddr = get_cont_pages(order, flags);
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
	size_t order = LOG2_UP(nr_pages(size));
	free_cont_pages(cpu_addr, order);
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

/* Linux printk front ends */
#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define KERN_EMERG
#define KERN_ALERT
#define KERN_CRIT
#define KERN_ERR
#define KERN_WARNING
#define KERN_NOTICE
#define KERN_INFO
#define KERN_CONT
#define KERN_DEBUG

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
	printk(KERN_CONT fmt, ##__VA_ARGS__)
#define netdev_printk(lvl, dev, fmt, ...) \
	printk(fmt, ##__VA_ARGS__)


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
#define MODULE_PARM_DESC(...)
#define MODULE_DEVICE_TABLE(...)
#define THIS_MODULE ((void*)0)

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

#endif /* ROS_KERN_AKAROS_COMPAT_H */
