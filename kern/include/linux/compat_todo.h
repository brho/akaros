/*
 * Copyright (C) 1991-2015, the Linux Kernel authors
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 *
 * This file consists of various bits of Linux code used in porting drivers to
 * Akaros. */

#pragma once

#include <arch/types.h>
#include <ros/common.h>
#include <atomic.h>
#include <bitops.h>
#include <kthread.h>
#include <list.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <taskqueue.h>
#include <rbtree.h>

#define ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define ETH_ZLEN	60		/* Min. octets in frame sans FCS */
#define ETH_FCS_LEN	4		/* Octets in the FCS		 */

struct ethhdr {
	unsigned char	h_dest[ETH_ALEN];	/* destination eth addr	*/
	unsigned char	h_source[ETH_ALEN];	/* source ether addr	*/
	uint16_t	h_proto;		/* packet type ID field	*/
} __attribute__((packed));

extern int ____ilog2_NaN;

/**
 * ilog2 - log of base 2 of 32-bit or a 64-bit unsigned value
 * @n - parameter
 *
 * constant-capable log of base 2 calculation
 * - this can be used to initialise global variables from constant data, hence
 *   the massive ternary operator construction
 *
 * selects the appropriately-sized optimised version depending on sizeof(n)
 */
#define ilog2(n)				\
(						\
	__builtin_constant_p(n) ? (		\
		(n) < 1 ? ____ilog2_NaN :	\
		(n) & (1ULL << 63) ? 63 :	\
		(n) & (1ULL << 62) ? 62 :	\
		(n) & (1ULL << 61) ? 61 :	\
		(n) & (1ULL << 60) ? 60 :	\
		(n) & (1ULL << 59) ? 59 :	\
		(n) & (1ULL << 58) ? 58 :	\
		(n) & (1ULL << 57) ? 57 :	\
		(n) & (1ULL << 56) ? 56 :	\
		(n) & (1ULL << 55) ? 55 :	\
		(n) & (1ULL << 54) ? 54 :	\
		(n) & (1ULL << 53) ? 53 :	\
		(n) & (1ULL << 52) ? 52 :	\
		(n) & (1ULL << 51) ? 51 :	\
		(n) & (1ULL << 50) ? 50 :	\
		(n) & (1ULL << 49) ? 49 :	\
		(n) & (1ULL << 48) ? 48 :	\
		(n) & (1ULL << 47) ? 47 :	\
		(n) & (1ULL << 46) ? 46 :	\
		(n) & (1ULL << 45) ? 45 :	\
		(n) & (1ULL << 44) ? 44 :	\
		(n) & (1ULL << 43) ? 43 :	\
		(n) & (1ULL << 42) ? 42 :	\
		(n) & (1ULL << 41) ? 41 :	\
		(n) & (1ULL << 40) ? 40 :	\
		(n) & (1ULL << 39) ? 39 :	\
		(n) & (1ULL << 38) ? 38 :	\
		(n) & (1ULL << 37) ? 37 :	\
		(n) & (1ULL << 36) ? 36 :	\
		(n) & (1ULL << 35) ? 35 :	\
		(n) & (1ULL << 34) ? 34 :	\
		(n) & (1ULL << 33) ? 33 :	\
		(n) & (1ULL << 32) ? 32 :	\
		(n) & (1ULL << 31) ? 31 :	\
		(n) & (1ULL << 30) ? 30 :	\
		(n) & (1ULL << 29) ? 29 :	\
		(n) & (1ULL << 28) ? 28 :	\
		(n) & (1ULL << 27) ? 27 :	\
		(n) & (1ULL << 26) ? 26 :	\
		(n) & (1ULL << 25) ? 25 :	\
		(n) & (1ULL << 24) ? 24 :	\
		(n) & (1ULL << 23) ? 23 :	\
		(n) & (1ULL << 22) ? 22 :	\
		(n) & (1ULL << 21) ? 21 :	\
		(n) & (1ULL << 20) ? 20 :	\
		(n) & (1ULL << 19) ? 19 :	\
		(n) & (1ULL << 18) ? 18 :	\
		(n) & (1ULL << 17) ? 17 :	\
		(n) & (1ULL << 16) ? 16 :	\
		(n) & (1ULL << 15) ? 15 :	\
		(n) & (1ULL << 14) ? 14 :	\
		(n) & (1ULL << 13) ? 13 :	\
		(n) & (1ULL << 12) ? 12 :	\
		(n) & (1ULL << 11) ? 11 :	\
		(n) & (1ULL << 10) ? 10 :	\
		(n) & (1ULL <<  9) ?  9 :	\
		(n) & (1ULL <<  8) ?  8 :	\
		(n) & (1ULL <<  7) ?  7 :	\
		(n) & (1ULL <<  6) ?  6 :	\
		(n) & (1ULL <<  5) ?  5 :	\
		(n) & (1ULL <<  4) ?  4 :	\
		(n) & (1ULL <<  3) ?  3 :	\
		(n) & (1ULL <<  2) ?  2 :	\
		(n) & (1ULL <<  1) ?  1 :	\
		(n) & (1ULL <<  0) ?  0 :	\
		____ilog2_NaN			\
				   ) :		\
	LOG2_UP(n)				\
 )

#define __printf(...)

#define sprintf(s, fmt, ...) ({ \
	int ret = -1; \
	if (__builtin_types_compatible_p(typeof(s), char[])) \
		ret = snprintf(s, sizeof(s), fmt, ##__VA_ARGS__); \
	else \
		panic("Not implemented"); \
	ret; \
})

#define printk_once(fmt, ...) ({ \
	static bool __print_once; \
	if (!__print_once) { \
		__print_once = true; \
		printk(fmt, ##__VA_ARGS__); \
	} \
})

#define dev_warn(dev, format, ...) \
	pr_warn(format, ## __VA_ARGS__)

#define dev_printk(level, dev, fmt, ...) \
	printk(level fmt, ## __VA_ARGS__)

/* XXX This is not a tree. */
struct radix_tree_node {
	struct list_head	linkage;
	unsigned long		index;
	void			*item;
};

struct radix_tree_root {
	struct list_head	hlinks;
};

static inline void INIT_RADIX_TREE(struct radix_tree_root *rp, int mask)
{
	INIT_LIST_HEAD(&rp->hlinks);
}

static inline int radix_tree_insert(struct radix_tree_root *root,
				    unsigned long index, void *item)
{
	struct list_head *lp = root->hlinks.next;
	struct radix_tree_node *p;

	while (lp != &root->hlinks) {
		p = (struct radix_tree_node *)lp;
		if (p->index == index)
			return -EEXIST;
		lp = lp->next;
	}

	p = kmalloc(sizeof(*p), MEM_WAIT);
	if (!p)
		return -ENOMEM;
	p->index = index;
	p->item = item;
	list_add(&p->linkage, &root->hlinks);
	return 0;
}

static inline void *radix_tree_lookup(struct radix_tree_root *root,
				      unsigned long index)
{
	struct list_head *lp = root->hlinks.next;
	struct radix_tree_node *p;

	while (lp != &root->hlinks) {
		p = (struct radix_tree_node *)lp;
		if (p->index == index)
			return p->item;
		lp = lp->next;
	}

	return NULL;
}

static inline void radix_tree_delete(struct radix_tree_root *root,
				      unsigned long index)
{
	struct list_head *lp = root->hlinks.next;
	struct radix_tree_node *p;

	while (lp != &root->hlinks) {
		p = (struct radix_tree_node *)lp;
		if (p->index == index) {
			list_del(lp);
			return;
		}
		lp = lp->next;
	}
	panic("Node not found\n");
}

#define INIT_DEFERRABLE_WORK(_work, _func) \
	INIT_DELAYED_WORK(_work, _func) /* XXX */

struct tasklet_struct {
	uint32_t state;
	void (*func)(unsigned long);
	unsigned long data;
};

static void __tasklet_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	struct tasklet_struct *t = (struct tasklet_struct *)a0;

	if (atomic_cas_u32(&t->state, 1, 0))
		t->func(t->data);
}

static inline void tasklet_schedule(struct tasklet_struct *t)
{
	if (atomic_cas_u32(&t->state, 0, 1)) {
		send_kernel_message(core_id(), __tasklet_wrapper, (long)t, 0, 0,
				    KMSG_ROUTINE);
	}
}

static inline void tasklet_disable(struct tasklet_struct *t)
{
	panic("tasklet_disable unimplemented");
	// XXX t->state = 2
}

static inline void tasklet_init(struct tasklet_struct *t,
				void (*func)(unsigned long), unsigned long data)
{
	t->state = 0;
	t->func = func;
	t->data = data;
}

struct completion {
	unsigned int done;
};

static inline void init_completion(struct completion *x)
{
	x->done = 0;
}

static inline void reinit_completion(struct completion *x)
{
	printd("Core %d: reinit_completion %p\n", core_id(), x);
	x->done = 0;
}

static inline void complete(struct completion *x)
{
	printd("Core %d: complete %p\n", core_id(), x);
	x->done = 1;
	wmb();
}

static inline void wait_for_completion(struct completion *x)
{
	while (!x->done)
		cmb();
}

static inline unsigned long wait_for_completion_timeout(struct completion *x,
							unsigned long timeout)
{
	printd("Core %d: wait_for_completion_timeout %p\n", core_id(), x);
	while (!x->done) {
		if (timeout) {
			kthread_usleep(1000);
			timeout--;
			cmb();
		} else {
			break;
		}
	}
	return timeout;
}

/* The timer functions (e.g. mod_timer) expect to set an actual time.  These
 * hacks do relative time.  Hopefully some users will use jiffies + rel_time
 * when using mod_timer (like r8169 does). */
#define jiffies 0

struct timer_list {
	spinlock_t lock;
	bool scheduled;
	unsigned long delay;
	void (*function)(unsigned long);
	unsigned long data;
};

static inline void init_timer(struct timer_list *timer)
{
	spinlock_init_irqsave(&timer->lock);
	timer->scheduled = false;
	timer->delay = 0;
	timer->function = 0;
	timer->data = 0;
}

static inline void setup_timer(struct timer_list *timer,
                               void (*func)(unsigned long), unsigned long data)
{
	init_timer(timer);
	timer->function = func;
	timer->data = data;
}

static void __timer_wrapper(uint32_t srcid, long a0, long a1, long a2)
{
	struct timer_list *timer = (struct timer_list *)a0;
	unsigned long delay = 0;

	spin_lock_irqsave(&timer->lock);
	delay = timer->delay;
	timer->scheduled = false;
	spin_unlock_irqsave(&timer->lock);

	kthread_usleep(delay * 10000);
	timer->function(timer->data);
}

static inline void add_timer(struct timer_list *timer)
{
	timer->scheduled = true;
	send_kernel_message(core_id(), __timer_wrapper, (long)timer, 0, 0,
			    KMSG_ROUTINE);
}

static inline void mod_timer(struct timer_list *timer, unsigned long delay)
{
	spin_lock_irqsave(&timer->lock);
	timer->delay = delay;
	if (timer->scheduled) {
		spin_unlock_irqsave(&timer->lock);
		return;
	}
	timer->scheduled = true;
	spin_unlock_irqsave(&timer->lock);
	send_kernel_message(core_id(), __timer_wrapper, (long)timer, 0, 0,
			    KMSG_ROUTINE);
}

static inline void del_timer_sync(struct timer_list *timer)
{
	panic("del_timer_sync unimplemented");
}

struct cpu_rmap {
};

extern unsigned long saved_max_pfn;

struct device_attribute {
};

#define DEFINE_MUTEX(mutexname) \
	qlock_t mutexname = QLOCK_INITIALIZER(mutexname);

#define IS_ENABLED(...) (0)

/* linux/compiler-gcc.h */
#define __packed                        __attribute__((packed))
#define __attribute_const__             __attribute__((__const__))
#define __aligned(n)                    __attribute__((aligned(n)))
#define __always_unused                 __attribute__((unused))

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/*
 * Check at compile time that 'function' is a certain type, or is a pointer
 * to that type (needs to use typedef for the function type.)
 */
#define typecheck_fn(type,function) \
({	typeof(type) __tmp = function; \
	(void)__tmp; \
})

#if BITS_PER_LONG > 32
#define NET_SKBUFF_DATA_USES_OFFSET 1
#endif

#ifdef NET_SKBUFF_DATA_USES_OFFSET
typedef unsigned int sk_buff_data_t;
#else
typedef unsigned char *sk_buff_data_t;
#endif

typedef struct skb_frag_struct skb_frag_t;

struct skb_frag_struct {
	struct {
		struct page *p;
	} page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	uint32_t page_offset;
	uint32_t size;
#else
	uint16_t page_offset;
	uint16_t size;
#endif
};

/* asm-generic/getorder.h */
/*
 * Runtime evaluation of get_order()
 */
static inline __attribute_const__
int __get_order(unsigned long size)
{
	int order;

	size--;
	size >>= PAGE_SHIFT;
#if BITS_PER_LONG == 32
	order = fls(size);
#else
	order = fls64(size);
#endif
	return order;
}

/**
 * get_order - Determine the allocation order of a memory size
 * @size: The size for which to get the order
 *
 * Determine the allocation order of a particular sized block of memory.  This
 * is on a logarithmic scale, where:
 *
 *	0 -> 2^0 * PAGE_SIZE and below
 *	1 -> 2^1 * PAGE_SIZE to 2^0 * PAGE_SIZE + 1
 *	2 -> 2^2 * PAGE_SIZE to 2^1 * PAGE_SIZE + 1
 *	3 -> 2^3 * PAGE_SIZE to 2^2 * PAGE_SIZE + 1
 *	4 -> 2^4 * PAGE_SIZE to 2^3 * PAGE_SIZE + 1
 *	...
 *
 * The order returned is used to find the smallest allocation granule required
 * to hold an object of the specified size.
 *
 * The result is undefined if the size is 0.
 *
 * This function may be used to initialise variables with compile time
 * evaluations of constants.
 */
#define get_order(n)						\
(								\
	__builtin_constant_p(n) ? (				\
		((n) == 0UL) ? BITS_PER_LONG - PAGE_SHIFT :	\
		(((n) < (1UL << PAGE_SHIFT)) ? 0 :		\
		 ilog2((n) - 1) - PAGE_SHIFT + 1)		\
	) :							\
	__get_order(n)						\
)

/* asm-generic/io.h */
static inline void __raw_writeq(uint64_t value, volatile void *addr)
{
	*(volatile uint64_t *)addr = value;
}

/* linux/export.h */
#define EXPORT_SYMBOL_GPL(...)

/* linux/gfp.h */
/* These are all set to 0.  Silently passing flags to Akaros's memory allocator
 * is dangerous - we could be turning on some Akaros feature that shares the
 * same bit.
 *
 * Similarly, note that some Linux flags (__GFP_ZERO) is not here.  Those flags
 * need to be dealt with.  Silently ignoring them will cause errors. */
#define  __GFP_DMA 0
#define  __GFP_HIGHMEM 0
#define  __GFP_DMA32 0
#define  __GFP_MOVABLE 0
#define  __GFP_WAIT 0
#define  __GFP_HIGH 0
#define  __GFP_IO 0
#define  __GFP_FS 0
#define  __GFP_COLD 0
#define  __GFP_NOWARN 0
#define  __GFP_REPEAT 0
#define  __GFP_NOFAIL 0
#define  __GFP_NORETRY 0
#define  __GFP_MEMALLOC 0
#define  __GFP_COMP 0
#define  __GFP_NOMEMALLOC 0
#define  __GFP_HARDWALL 0
#define  __GFP_THISNODE 0
#define  __GFP_RECLAIMABLE 0
#define  __GFP_NOACCOUNT 0
#define  __GFP_NOTRACK 0
#define  __GFP_NO_KSWAPD 0
#define  __GFP_OTHER_NODE 0
#define  __GFP_WRITE 0
#define GFP_HIGHUSER 0

/* linux/kernel.h */
#define min3(x, y, z) MIN((typeof(x))MIN(x, y), z)
#define max3(x, y, z) MAX((typeof(x))MAX(x, y), z)

typedef unsigned int pci_channel_state_t;

enum pci_channel_state {
	/* I/O channel is in normal state */
	pci_channel_io_normal = 1,

	/* I/O to channel is blocked */
	pci_channel_io_frozen = 2,

	/* PCI card is dead */
	pci_channel_io_perm_failure = 3,
};

typedef unsigned int pci_ers_result_t;

enum pci_ers_result {
	/* no result/none/not supported in device driver */
	PCI_ERS_RESULT_NONE = 1,

	/* Device driver can recover without slot reset */
	PCI_ERS_RESULT_CAN_RECOVER = 2,

	/* Device driver wants slot to be reset. */
	PCI_ERS_RESULT_NEED_RESET = 3,

	/* Device has completely failed, is unrecoverable */
	PCI_ERS_RESULT_DISCONNECT = 4,

	/* Device driver is fully recovered and operational */
	PCI_ERS_RESULT_RECOVERED = 5,

	/* No AER capabilities registered for the driver */
	PCI_ERS_RESULT_NO_AER_DRIVER = 6,
};

/* These values come from the PCI Express Spec */
enum pcie_link_width {
	PCIE_LNK_WIDTH_RESRV	= 0x00,
	PCIE_LNK_X1		= 0x01,
	PCIE_LNK_X2		= 0x02,
	PCIE_LNK_X4		= 0x04,
	PCIE_LNK_X8		= 0x08,
	PCIE_LNK_X12		= 0x0C,
	PCIE_LNK_X16		= 0x10,
	PCIE_LNK_X32		= 0x20,
	PCIE_LNK_WIDTH_UNKNOWN  = 0xFF,
};

/* Based on the PCI Hotplug Spec, but some values are made up by us */
enum pci_bus_speed {
	PCI_SPEED_33MHz			= 0x00,
	PCI_SPEED_66MHz			= 0x01,
	PCI_SPEED_66MHz_PCIX		= 0x02,
	PCI_SPEED_100MHz_PCIX		= 0x03,
	PCI_SPEED_133MHz_PCIX		= 0x04,
	PCI_SPEED_66MHz_PCIX_ECC	= 0x05,
	PCI_SPEED_100MHz_PCIX_ECC	= 0x06,
	PCI_SPEED_133MHz_PCIX_ECC	= 0x07,
	PCI_SPEED_66MHz_PCIX_266	= 0x09,
	PCI_SPEED_100MHz_PCIX_266	= 0x0a,
	PCI_SPEED_133MHz_PCIX_266	= 0x0b,
	AGP_UNKNOWN			= 0x0c,
	AGP_1X				= 0x0d,
	AGP_2X				= 0x0e,
	AGP_4X				= 0x0f,
	AGP_8X				= 0x10,
	PCI_SPEED_66MHz_PCIX_533	= 0x11,
	PCI_SPEED_100MHz_PCIX_533	= 0x12,
	PCI_SPEED_133MHz_PCIX_533	= 0x13,
	PCIE_SPEED_2_5GT		= 0x14,
	PCIE_SPEED_5_0GT		= 0x15,
	PCIE_SPEED_8_0GT		= 0x16,
	PCI_SPEED_UNKNOWN		= 0xff,
};

/* linux/slab.h */
#define kzalloc_node(...) (0 /* XXX */)

#define __cpu_to_le32(x) (x)
#define __le32_to_cpu(x) (x)

#define swab32 __builtin_bswap32
#define swab16 __builtin_bswap16

#define cond_resched() { /* XXX */ \
	volatile int i = 100000; \
	/* printk("%s:%d %s cond_resched\n", __FILE__, __LINE__, __FUNCTION__); */ \
	while (i--); \
}

#define iounmap(...) (0 /* XXX */)

typedef uint64_t phys_addr_t;
typedef phys_addr_t resource_size_t;

struct dma_pool *dma_pool_create(const char *name, void *dev,
				 size_t size, size_t align, size_t allocation);

void dma_pool_destroy(struct dma_pool *pool);

void *dma_pool_alloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle);

void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);

#define pci_pool dma_pool
#define pci_pool_create(name, pdev, size, align, allocation) \
		dma_pool_create(name, pdev, size, align, allocation)
#define pci_pool_destroy(pool) dma_pool_destroy(pool)
#define pci_pool_alloc(pool, flags, handle) dma_pool_alloc(pool, flags, handle)
#define pci_pool_free(pool, vaddr, addr) dma_pool_free(pool, vaddr, addr)

#define __user
#define __bitwise

struct vm_area_struct {
};

/* uapi/rdma/ib_user_mad.h */
#define IB_USER_MAD_USER_RMPP 1

#define be16_to_cpup(p) be16_to_cpu(*(uint16_t *)(p))
#define be32_to_cpup(p) be32_to_cpu(*(uint32_t *)(p))

/* access_ok.h */

static inline uint16_t get_unaligned_le16(const void *p)
{
	return le16_to_cpu(*(uint16_t *)p);
}

static inline uint32_t get_unaligned_le32(const void *p)
{
	return le32_to_cpu(*(uint32_t *)p);
}

static inline uint64_t get_unaligned_le64(const void *p)
{
	return le64_to_cpu(*(uint64_t *)p);
}

static inline void put_unaligned_le16(uint16_t val, void *p)
{
	*((uint16_t *)p) = cpu_to_le16(val);
}

static inline void put_unaligned_le32(uint32_t val, void *p)
{
	*((uint32_t *)p) = cpu_to_le32(val);
}

static inline void put_unaligned_le64(uint64_t val, void *p)
{
	*((uint64_t *)p) = cpu_to_le64(val);
}

extern void __bad_unaligned_access_size(void);

#define __get_unaligned_le(ptr) ({ \
	__builtin_choose_expr(sizeof(*(ptr)) == 1, *(ptr), \
	__builtin_choose_expr(sizeof(*(ptr)) == 2, get_unaligned_le16((ptr)), \
	__builtin_choose_expr(sizeof(*(ptr)) == 4, get_unaligned_le32((ptr)), \
	__builtin_choose_expr(sizeof(*(ptr)) == 8, get_unaligned_le64((ptr)), \
	__bad_unaligned_access_size())))); \
})

#define __put_unaligned_le(val, ptr) ({					\
	void *__gu_p = (ptr);						\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		*(uint8_t *)__gu_p = (uint8_t)(val);			\
		break;							\
	case 2:								\
		put_unaligned_le16((uint16_t)(val), __gu_p);		\
		break;							\
	case 4:								\
		put_unaligned_le32((uint32_t)(val), __gu_p);		\
		break;							\
	case 8:								\
		put_unaligned_le64((uint64_t)(val), __gu_p);		\
		break;							\
	default:							\
		__bad_unaligned_access_size();				\
		break;							\
	}								\
	(void)0; })

#define get_unaligned __get_unaligned_le
#define put_unaligned __put_unaligned_le

#define vmalloc_node(...) (0 /* XXX */)
/* needed by icm.c: */
#define kmalloc_node(size, flags, node) kmalloc(size, flags)
#define alloc_pages_node(nid, gfp_mask, order) 0

struct scatterlist {
	unsigned long   page_link;
	unsigned int    offset;
	unsigned int    length;
	dma_addr_t      dma_address;
};

#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->length)

#define sg_is_chain(sg) ((sg)->page_link & 0x01)
#define sg_is_last(sg) ((sg)->page_link & 0x02)
#define sg_chain_ptr(sg) ((struct scatterlist *) ((sg)->page_link & ~0x03))

static inline void sg_assign_page(struct scatterlist *sg, struct page *page)
{
	unsigned long page_link = sg->page_link & 0x3;
	sg->page_link = page_link | (unsigned long) page;
}

/**
 * sg_set_page - Set sg entry to point at given page
 * @sg:          SG entry
 * @page:        The page
 * @len:         Length of data
 * @offset:      Offset into page
 *
 * Description:
 *   Use this function to set an sg entry pointing at a page, never assign
 *   the page directly. We encode sg table information in the lower bits
 *   of the page pointer. See sg_page() for looking up the page belonging
 *   to an sg entry.
 *
 **/
static inline void sg_set_page(struct scatterlist *sg, struct page *page,
			       unsigned int len, unsigned int offset)
{
	sg_assign_page(sg, page);
	sg->offset = offset;
	sg->length = len;
}

static inline struct page *sg_page(struct scatterlist *sg)
{
	return (struct page *)(sg->page_link & ~0x3);
}

static inline void sg_set_buf(struct scatterlist *sg, void *buf,
			      unsigned int buflen)
{
	sg_set_page(sg, kva2page(buf), buflen, PGOFF(buf));
}

#define for_each_sg(sglist, sg, nr, __i) \
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

static inline void sg_mark_end(struct scatterlist *sg)
{
	/*
	 * Set termination bit, clear potential chain bit
	 */
	sg->page_link |= 0x02;
	sg->page_link &= ~0x01;
}

void sg_init_table(struct scatterlist *, unsigned int);
struct scatterlist *sg_next(struct scatterlist *);

static inline int nontranslate_map_sg(void *dev, struct scatterlist *sglist,
				      int nelems, int dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i) {
		sg_dma_address(sg) = page2pa(sg_page(sg)) + sg->offset;
		sg_dma_len(sg) = sg->length;
	}
	return nelems;
}

static inline int dma_map_sg(void *dev, struct scatterlist *sg, int nents,
			     enum dma_data_direction dir)
{
	return nontranslate_map_sg(dev, sg, nents, dir);
}

static inline void dma_unmap_sg(void *dev, struct scatterlist *sg, int nents,
				enum dma_data_direction dir)
{
	/* TODO nop */
}

struct dma_attrs {
};

#define dma_map_sg_attrs(d, s, n, r, a) dma_map_sg(d, s, n, r)
#define dma_unmap_sg_attrs(d, s, n, r, a) dma_unmap_sg(d, s, n, r)

static inline int pci_map_sg(struct pci_device *pdev, struct scatterlist *sg,
			     int nents, int direction)
{
	return dma_map_sg(NULL, sg, nents, (enum dma_data_direction)direction);
}

static inline void pci_unmap_sg(struct pci_device *pdev, struct scatterlist *sg,
				int nents, int direction)
{
	dma_unmap_sg(NULL, sg, nents, (enum dma_data_direction)direction);
}

/* TODO: get this in an arch-dependent manner.  On x86, be careful of adjacent
 * cacheline prefetching. */
#define cache_line_size() 64

static inline void *lowmem_page_address(struct page *page)
{
	/* XXX not sure about this */
	return page2kva(page);
}

#define page_address(page) lowmem_page_address(page)

#define netif_get_num_default_rss_queues() (8 /* FIXME */)

static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	/* XXX */
	memset(buffer, 0, len);
}

/* Hacked up version of Linux's.  Assuming reg's are implemented and
 * read_config never fails. */
static int pcie_capability_read_dword(struct pci_device *dev, int pos,
				      uint32_t *val)
{
	uint32_t pcie_cap;
	*val = 0;
	if (pos & 3)
		return -EINVAL;
	if (pci_find_cap(dev, PCI_CAP_ID_EXP, &pcie_cap))
		return -EINVAL;
	pci_read_config_dword(dev, pcie_cap + pos, val);
	return 0;
}

static inline const char *pci_name(const struct pci_device *pdev)
{
	return pdev->name;
}

#define dev_name(dev) ("ether0" /* XXX */)

#define pci_request_regions(...) (0 /* XXX */)
#define pci_save_state(...) (0 /* XXX */)
#define pci_set_consistent_dma_mask(...) (0 /* XXX */)
#define pci_set_dma_mask(...) (0 /* XXX */)
#define pci_channel_offline(...) (0 /* XXX */)
#define dma_set_max_seg_size(...) (0 /* XXX */)
#define dma_get_cache_alignment(...) (1 /* XXX */)
#define dev_to_node(...) (0 /* XXX */)
#define set_dev_node(...) /* TODO */
#define num_online_cpus() (1U /* XXX */)
#define cpu_to_node(cpu) ((void)(cpu),0)

#define pcie_get_minimum_link(dev, speed, width) ({ \
	*(speed) = PCIE_SPEED_8_0GT; \
	*(width) = PCIE_LNK_X8; \
	0; })

static inline void get_random_bytes(char *buf, int nbytes)
{
	// FIXME this is not random
	for (int i = 0; i < nbytes; i++)
		buf[i] = i;
}

/* linux/moduleparam.h */
#define module_param_array(...)

#define ATOMIC_INIT(i) (i)

/* linux/pci_ids.h */
#define PCI_VENDOR_ID_MELLANOX          0x15b3

struct sysinfo {
	long uptime;             /* Seconds since boot */
	unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
	unsigned long totalram;  /* Total usable main memory size */
	unsigned long freeram;   /* Available memory size */
	unsigned long sharedram; /* Amount of shared memory */
	unsigned long bufferram; /* Memory used by buffers */
	unsigned long totalswap; /* Total swap space size */
	unsigned long freeswap;  /* swap space still available */
	unsigned short procs;    /* Number of current processes */
	unsigned long totalhigh; /* Total high memory size */
	unsigned long freehigh;  /* Available high memory size */
	unsigned int mem_unit;   /* Memory unit size in bytes */
	char _f[20-2*sizeof(long)-sizeof(int)]; /* Padding to 64 bytes */
};

#define pci_pcie_cap(...) (0 /* TODO use pci_device->caps ? */)

#define NET_IP_ALIGN 2
#define ____cacheline_aligned_in_smp

typedef struct cpumask { } cpumask_t;
typedef struct cpumask cpumask_var_t[1];

#define cpumask_set_cpu(...) (0 /* TODO */)
#define zalloc_cpumask_var(...) (1 /* TODO */)
#define cpumask_local_spread(...) (0 /* TODO */)

struct notifier_block {
};

struct hwtstamp_config {
	int flags;
	int tx_type;
	int rx_filter;
};

struct skb_shared_hwtstamps {
};

#define VLAN_N_VID 4096

/* linux/if_vlan.h */
#define VLAN_PRIO_SHIFT 13

/* linux/pci.h */
#define PCI_DMA_BIDIRECTIONAL   0
#define PCI_DMA_TODEVICE        1
#define PCI_DMA_FROMDEVICE      2
#define PCI_DMA_NONE            3

/* uapi/linux/net_tstamp.h */
enum hwtstamp_tx_types {
	HWTSTAMP_TX_OFF,
	HWTSTAMP_TX_ON,
	HWTSTAMP_TX_ONESTEP_SYNC,
};

/* linux/skbuff.h */
enum {
	SKBTX_HW_TSTAMP = 1 << 0,
	SKBTX_SW_TSTAMP = 1 << 1,
	SKBTX_IN_PROGRESS = 1 << 2,
	SKBTX_DEV_ZEROCOPY = 1 << 3,
	SKBTX_WIFI_STATUS = 1 << 4,
	SKBTX_SHARED_FRAG = 1 << 5,
	SKBTX_SCHED_TSTAMP = 1 << 6,
	SKBTX_ACK_TSTAMP = 1 << 7,
};
#define CHECKSUM_NONE           0
#define CHECKSUM_UNNECESSARY    1
#define CHECKSUM_COMPLETE       2
#define CHECKSUM_PARTIAL        3

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_GRE 47
#define IPPROTO_RAW 255

#define IFNAMSIZ 16
#define MAX_ADDR_LEN 32

/* linux/mmzone.h */
#define PAGE_ALLOC_COSTLY_ORDER 3

/* linux/cache.h */
#define L1_CACHE_BYTES (1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES L1_CACHE_BYTES

/* linux/if_vlan.h */
#define VLAN_HLEN 4

#define NETIF_F_RXFCS 0

#define irq_to_desc(irq) ({ /* I_AM_HERE; */ NULL; /* TODO */ })

static inline void eth_broadcast_addr(uint8_t *addr)
{
	memset(addr, 0xff, Eaddrlen);
}

static inline bool netif_carrier_ok(struct ether *dev)
{
	return true; // XXX
}

#define BOND_LINK_FAIL 1
#define BOND_LINK_UP 2
#define BOND_MODE_8023AD 1
#define BOND_MODE_ACTIVEBACKUP 2
#define BOND_MODE_XOR 3
#define BOND_STATE_BACKUP 0
#define eth_validate_addr 0
#define HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ 1
#define HWTSTAMP_FILTER_PTP_V1_L4_EVENT 2
#define HWTSTAMP_FILTER_PTP_V1_L4_SYNC 3
#define HWTSTAMP_FILTER_PTP_V2_DELAY_REQ 4
#define HWTSTAMP_FILTER_PTP_V2_EVENT 5
#define HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ 6
#define HWTSTAMP_FILTER_PTP_V2_L2_EVENT 7
#define HWTSTAMP_FILTER_PTP_V2_L2_SYNC 8
#define HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ 9
#define HWTSTAMP_FILTER_PTP_V2_L4_EVENT 10
#define HWTSTAMP_FILTER_PTP_V2_L4_SYNC 11
#define HWTSTAMP_FILTER_PTP_V2_SYNC 12
#define HWTSTAMP_FILTER_SOME 13
#define HWTSTAMP_FILTER_ALL 14
#define HWTSTAMP_FILTER_NONE 15
#define IFF_ALLMULTI 1
#define IFF_PROMISC 2
#define IFF_UNICAST_FLT 3
#define NETDEV_BONDING_INFO 0
#define NETIF_F_HW_VLAN_CTAG_FILTER 1
#define NETIF_F_NTUPLE 2
#define NETIF_F_RXALL 3
#define NOTIFY_DONE 0
#define SIOCGHWTSTAMP 1
#define SIOCSHWTSTAMP 2
