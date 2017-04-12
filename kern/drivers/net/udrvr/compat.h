/*
 * Copyright (c) 2016 Google Inc.
 * Author: Kanoj Sarcar <kanoj@google.com>
 * See LICENSE for details.
 *
 * A majority of macros in this file should migrate to compat_todo.h,
 * which should hold source copied from Linux. Some part of macros
 * will also move into linux_compat.h to translate from Linux to
 * Akaros. A tiny portion should remain here, since those are stub
 * or hack definitions whose scope should be restricted only to mlx4u/
 * and udrvr/
 */
#include <linux_compat.h>

#include <arch/uaccess.h>	/* copy_from_user(), copy_to_user() */

#define access_ok(type, addr, size)     1

/*
 * Device file /dev/XXXX has a dentry and inode that is associated
 * with the "struct file" for each user process opening the device file.
 * Thus, we can stash private data into file->f_dentry->d_fs_info or
 * into file->f_dentry->d_inode->i_fs_info.
 */
#define set_fs_info(_p_, _v_)		\
	do { (_p_)->f_dentry->d_fs_info = (_v_); } while(0)
#define	get_fs_info(_p_)	((_p_)->f_dentry->d_fs_info)
#define	private_data			f_privdata

typedef uint8_t u8;
typedef uint8_t __u8;
typedef uint16_t u16;
typedef uint16_t __u16;
typedef uint32_t u32;
typedef uint32_t __u32;
typedef uint64_t u64;
typedef uint64_t __u64;

typedef int32_t	__s32;

typedef off64_t	loff_t;

typedef atomic_t			atomic64_t;

#define	atomic64_set			atomic_set
#define	atomic64_read			atomic_read
#define	atomic_dec_and_test(e)		atomic_sub_and_test(e, 1)
#define	atomic_inc_not_zero(p)		atomic_add_not_zero(p, 1)

#define	mutex_init(a)			qlock_init(a)
#define	mutex_lock(a)			qlock(a)
#define	mutex_unlock(a)			qunlock(a)

extern unsigned long pgprot_noncached(int vmprot);
extern unsigned long pgprot_writecombine(int vmprot);

#define is_vm_hugetlb_page(vma)	0

extern int map_upage_at_addr(struct proc *p, physaddr_t paddr, uintptr_t addr,
                             int pteprot, int dolock);
extern int get_user_page(struct proc *p, unsigned long uvastart, int write,
                         int force, struct page **plist);
extern void put_page(struct page *pagep);
extern void set_page_dirty_lock(struct page *pagep);

#define	io_remap_pfn_range(vma, vmstart, pfn, rangesz, pteprot)	\
	(rangesz == PAGE_SIZE ? map_upage_at_addr(current,	\
	((pfn) << PAGE_SHIFT), vmstart, pteprot, 1) : -1)

#define	get_user_pages(task, mm, uvastart, numpages, write, force,	\
	plist, vlist)							\
		get_user_page(task, uvastart, write, force, plist)

/* The foll is only true for mlx4/ code */
#define	read_lock(p)
#define	read_unlock(p)

#define	GFP_KERNEL			MEM_WAIT
#define	GFP_ATOMIC			0
#define	GFP_NOIO			MEM_WAIT
#define	GFP_NOWAIT			0

#define	__get_free_page(f)		kpage_alloc_addr()

static inline void free_page(unsigned long addr)
{
	if (addr != 0)
		kpages_free((void*)addr, PGSIZE);
}

#define	get_zeroed_page(f)		kpage_zalloc_addr()

#define	kzalloc(SZ, FL)			kzmalloc(SZ, FL)
#define	kcalloc(CNT, SZ, FL)		kzmalloc((CNT) * (SZ), FL)

#define	roundup_pow_of_two(E)		ROUNDUPPWR2(E)
#define	roundup(VAL, UP)		ROUNDUP(VAL, UP)
#define	min(A0, A1)			MIN(A0, A1)
#define	max(A0, A1)			MAX(A0, A1)

#define	LIST_HEAD(l)			LINUX_LIST_HEAD(l)

/*
 * Careful: these will replace "struct mutex" to "struct semaphore" but
 * also replace ptr->mutex to ptr->semaphore aka structure field rename.
 */
#define	mutex		semaphore
#define	rw_semaphore	semaphore

/* From include/linux/netdevice.h */
#define	dev_hold(p)
#define	dev_put(p)

#define	pr_info_once	printk

/* From Linux include/linux/scatterlist.h: move to compat_todo.h */
struct sg_table {
	struct scatterlist *sgl;
	unsigned int nents;
	unsigned int orig_nents;
};

extern int sg_alloc_table(struct sg_table *ptr, unsigned int npages, gfp_t mask);
void sg_free_table(struct sg_table *ptr);


/* From include/linux/compiler.h: move to compat_todo.h */
#define	__acquires(x)
#define	__releases(x)
#define	__acquire(x)			(void)0
#define	__release(x)			(void)0
#define uninitialized_var(x)		x = *(&(x))

/* From include/asm-generic/bug.h: move to compat_todo.h */
#define WARN_ON(condition) ({                                           \
        int __ret_warn_on = !!(condition);                              \
        if (unlikely(__ret_warn_on))                                    \
		printk("BUG: %s:%d/%s()!\n", __FILE__, __LINE__, __func__);\
        unlikely(__ret_warn_on);                                        \
})

#define	BUG_ON(condition)	\
	do {								\
		if (unlikely(condition))				\
			panic("BADBUG");				\
	} while(0)

#define	BUG()		BUG_ON(1)

/* Akaros cpu_to_be32() does not handle constants */
#undef cpu_to_be32
#define	___constant_swab32(x) ((__u32)(					\
	(((__u32)(x) & (__u32)0x000000ffUL) << 24) |			\
	(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |			\
	(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |			\
	(((__u32)(x) & (__u32)0xff000000UL) >> 24)))

#define	cpu_to_be32(x)							\
	(__builtin_constant_p((__u32)(x)) ?				\
	___constant_swab32(x) :						\
	byte_swap32(x))

#define	MAXITEMS	128

struct idr {
	spinlock_t	lock;
	void		*values[MAXITEMS];
};

#define	idr_destroy(p)
#define	idr_preload(f)
#define	idr_preload_end()

#define	DEFINE_IDR(name)			\
		struct idr name = { .lock = SPINLOCK_INITIALIZER }

void idr_remove(struct idr *idp, int id);
void *idr_find(struct idr *idr, int id);
int idr_alloc(struct idr *idp, void *ptr, int start, int end, gfp_t gfp_mask);

struct net_device {
	unsigned char dev_addr[MAX_ADDR_LEN];
};

/* Conflicting definitions in compat_todo.h */
#define	netif_carrier_ok(p)	1
#define	vm_area_struct		vm_region

#define	vm_start		vm_base
#define	vm_pgoff		vm_foff >> PAGE_SHIFT

#undef __init
#undef __exit
#define	__init	__attribute__((used))
#define	__exit	__attribute__((used))

struct cdev {
};

struct kobject {
};

typedef struct  wait_queue_head {
} wait_queue_head_t;

struct lock_class_key {
};

struct attribute {
};

struct ib_ud_header {
};

extern void sysfs_init(void);
extern void sysfs_create(int devnum, const struct file_operations *verb_fops,
                         void *ptr);

extern ssize_t check_old_abi(struct file *filp, const char __user *buf,
                             size_t count);
