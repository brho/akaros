/*
 * Copyright (c) 2016 Google Inc
 * Author: Kanoj Sarcar <kanoj@google.com>
 * See LICENSE for details.
 */

#include <err.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <smp.h>
#include <linux/rdma/ib_user_verbs.h>
#include "uverbs.h"
#include <ros/procinfo.h>

static unsigned long pgprot_val(int vmprot)
{
	unsigned long	prot = PTE_P | PTE_U | PTE_A;

	if (vmprot & PROT_WRITE)
		prot |= PTE_W | PTE_D;
	return prot;
}

unsigned long pgprot_noncached(int vmprot)
{
	return pgprot_val(vmprot) | PTE_NOCACHE;
}

unsigned long pgprot_writecombine(int vmprot)
{
	return pgprot_val(vmprot) | PTE_WRITECOMB;
}

/*
 * Our version knocked off from kern/src/mm.c version + uncaching logic from
 * vmap_pmem_nocache(). This routine is expected to be invoked as part of mmap()
 * handler.
 */
int map_upage_at_addr(struct proc *p, physaddr_t paddr, uintptr_t addr, int pteprot, int dolock)
{
	pte_t		pte;
	int		rv = -1;
	struct page	*pp;

	/* __vmr_free_pgs() assumes mapped pte is backed by "struct page" */
	if (paddr > max_paddr) {
		printk("[akaros]: map_upage_at_addr(): paddr=0x%llx "
		    "max_paddr=0x%llx\n", paddr, max_paddr);
		return -1;
	}

	pp = pa2page(paddr);

	/* __vmr_free_pgs() refcnt's pagemap pages differently */
	if (atomic_read(&pp->pg_flags) & PG_PAGEMAP) {
		printk("[akaros]: map_upage_at_addr(): mapPA=0x%llx\n",
		    paddr);
		return -1;
	}

	spin_lock(&p->pte_lock);

	/*
	 * Free any existing page backing uva, drop in this page, and
	 * acquire refcnt on page on behalf of user. Note though that we
	 * do not expect an existing page, since we are invoked in mmap
	 * path (page_insert() does not handle PG_PAGEMAP refcnt's).
	 */
	rv = page_insert(p->env_pgdir, pp, (void *)addr, pteprot);
	spin_unlock(&p->pte_lock);
	return rv;
}

void set_page_dirty_lock(struct page *pagep)
{
	atomic_or(&pagep->pg_flags, PG_DIRTY);
}

void put_page(struct page *pagep)
{
	if (atomic_read(&pagep->pg_flags) & PG_PAGEMAP)
		printk("[akaros]: put_page() on pagemap page!!!\n");
	page_decref(pagep);
}

int get_user_page(struct proc *p, unsigned long uvastart, int write, int force,
    struct page **plist)
{
	pte_t		pte;
	int		ret = -1;
	struct page	*pp;

	spin_lock(&p->pte_lock);

	pte = pgdir_walk(p->env_pgdir, (void*)uvastart, TRUE);

	if (!pte_walk_okay(pte))
		goto err1;

	if (!pte_is_present(pte)) {
		unsigned long prot = PTE_P | PTE_U | PTE_A | PTE_W | PTE_D;
#if 0
		printk("[akaros]: get_user_page() uva=0x%llx pte absent\n",
		    uvastart);
#endif
		/*
		 * TODO: ok to allocate with pte_lock? "prot" needs to be
		 * based on VMR writability, refer to pgprot_noncached().
		 */
		if (upage_alloc(p, &pp, 0))
			goto err1;
		pte_write(pte, page2pa(pp), prot);
	} else {
		pp = pa2page(pte_get_paddr(pte));

		/* __vmr_free_pgs() refcnt's pagemap pages differently */
		if (atomic_read(&pp->pg_flags) & PG_PAGEMAP) {
			printk("[akaros]: get_user_page(): uva=0x%llx\n",
			    uvastart);
			goto err1;
		}
	}

	if (write && (!pte_has_perm_urw(pte))) {
		/* TODO: How is Linux using the "force" parameter */
		printk("[akaros]: get_user_page() uva=0x%llx pte ro\n",
		    uvastart);
		goto err1;
	}

	/* TODO (GUP): change the interface such that devices provide the memory and
	 * the user mmaps it, instead of trying to pin arbitrary user memory. */
	warn_once("Extremely unsafe, unpinned memory mapped!  If your process dies, you might scribble on RAM!");

	plist[0] = pp;
	ret = 1;
err1:
	spin_unlock(&p->pte_lock);
	return ret;
}

int sg_alloc_table(struct sg_table *ptr, unsigned int npages, gfp_t mask)
{
	ptr->sgl = kmalloc((sizeof(struct scatterlist) * npages), mask);
	ptr->nents = ptr->orig_nents = npages;
	sg_init_table(ptr->sgl, npages);
	return 0;
}

void sg_free_table(struct sg_table *ptr)
{
	kfree(ptr->sgl);
}

void idr_remove(struct idr *idp, int id)
{
	BUG_ON((id < 0) || (id >= MAXITEMS));
	idp->values[id] = NULL;
}

void *idr_find(struct idr *idp, int id)
{
	BUG_ON((id < 0) || (id >= MAXITEMS));
	BUG_ON(idp->values[id] == NULL);
	return idp->values[id];
}

int idr_alloc(struct idr *idp, void *ptr, int start, int end, gfp_t gfp_mask)
{
	int	i;

	/* We use values[] == NULL as an indicator that slot is free */
	BUG_ON(ptr == NULL);

	spin_lock_irqsave(&idp->lock);

	for (i = 0; i < MAXITEMS; i++) {
		if (idp->values[i] == NULL) {
			idp->values[i] = ptr;
			goto done;
		}
	}

	i = -1;			/* error return */

done:
	spin_unlock_irqsave(&idp->lock);
	return i;
}

/* START: Linux /sys support for lib/apps */

/* Callers must pass in null terminated strings */
static ssize_t sysfs_read(char __user *buf, size_t ucount, loff_t *pos,
    char *src)
{
	int		slen = strlen(src) + 1;	/* + 1 for terminating null */
	unsigned long	off = *pos, nb = slen - off;

	if (off >= slen)
		return 0;

	if (copy_to_user(buf, (src + off), nb))
		return -EFAULT;

	*pos += nb;
	return nb;
}

static ssize_t ib_api_ver_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char		src[4] = { 0, 0, 0, 0};

	src[0] = '0' + IB_USER_VERBS_ABI_VERSION;

	return sysfs_read(buf, count, pos, src);
}

#if 0 // AKAROS_PORT (9ns hooks)
static const struct file_operations ib_api_ver = {
	.read	= ib_api_ver_read,
	.open	= kfs_open,
	.release= kfs_release,
};
#endif

static ssize_t mlx4_mgm_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
#if CONFIG_MLX4_DEFAULT_MGM_LOG_ENTRY_SIZE == -1
	char		src[4] = { '-', '1', 0, 0 };
#else
	char		src[4] = { '1', '0', 0, 0 };
#endif

	return sysfs_read(buf, count, pos, src);
}

#if 0 // AKAROS_PORT
static const struct file_operations mlx4_mgm = {
	.read	= mlx4_mgm_read,
	.open	= kfs_open,
	.release= kfs_release,
};
#endif

#if 0
static void stradd(char *dest, int val, int num)
{
	int	tval = val, i = 0, fac = 1;

	while (tval) {
		tval /= 10;
		fac *= 10;
		i++;
	}
	fac /= 10;
	tval = val;
	while (tval && num) {
		int dig = tval / fac;
		*dest++ = dig + '0';
		tval -= (dig * fac);
		fac /= 10;
		num--;
	}
}

static ssize_t cpu_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char cpu_info_str[128];
	long freq = __proc_global_info.tsc_freq, idx;

	strncpy(cpu_info_str, "cpu MHz\t\t: ", 16);
	idx = strlen(cpu_info_str);

	stradd(cpu_info_str + idx, freq / 1000000, 4);
	idx += 4;

	strncpy(cpu_info_str + idx, ".", 1);
	idx++;

	stradd(cpu_info_str + idx, freq % 1000000, 3);
	idx += 3;

	cpu_info_str[idx] = 0;

	return sysfs_read(buf, count, pos, cpu_info_str);
}

static const struct file_operations cpuinfo = {
	.read	= cpu_read,
	.open	= kfs_open,
	.release= kfs_release,
};
#endif

void sysfs_init(void)
{
#if 1 // AKAROS_PORT
	warn("mlx4: udrvr stuff requires various files, implement for 9ns!");
#else
	do_mkdir("/dev_vfs/infiniband", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys/class", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys/class/infiniband_verbs", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys/class/infiniband", S_IRWXU | S_IRWXG | S_IRWXO);

	make_device("/sys/class/infiniband_verbs/abi_version",
		    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
		    __S_IFCHR, (struct file_operations *)&ib_api_ver);

	do_mkdir("/sys/module", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys/module/mlx4_core", S_IRWXU | S_IRWXG | S_IRWXO);
	do_mkdir("/sys/module/mlx4_core/parameters", S_IRWXU | S_IRWXG |
	    S_IRWXO);
	make_device("/sys/module/mlx4_core/parameters/log_num_mgm_entry_size",
		    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
		    __S_IFCHR, (struct file_operations *)&mlx4_mgm);

#if 0
	/* Do this thru init scripts */
	do_mkdir("/proc", S_IRWXU | S_IRWXG | S_IRWXO);
	make_device("/proc/cpuinfo", S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR |
	    S_IRGRP | S_IROTH, __S_IFCHR, (struct file_operations *)&cpuinfo);
#endif
#endif
}

static ssize_t dver_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	struct ib_uverbs_device *uvp;
	char		src[4] = { 0, 0, 0, 0};

	uvp = (struct ib_uverbs_device *)get_fs_info(filp);
	src[0] = '0' + uvp->ib_dev->uverbs_abi_ver;

	return sysfs_read(buf, count, pos, src);
}

static ssize_t dname_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	struct ib_uverbs_device *uvp;

	uvp = (struct ib_uverbs_device *)get_fs_info(filp);
	return sysfs_read(buf, count, pos, uvp->ib_dev->name);
}

static ssize_t ntype_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char	src[] = "1";

	return sysfs_read(buf, count, pos, src);
}

static ssize_t ddev_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char	src[] = "0x1003";

	return sysfs_read(buf, count, pos, src);
}

static ssize_t dven_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char	src[] = "0x15b3";

	return sysfs_read(buf, count, pos, src);
}

static ssize_t vsd_read(struct file *filp, char __user *buf,
    size_t count, loff_t *pos)
{
	char	*src = "puma20_A1-10.2.3.0";

	return sysfs_read(buf, count, pos, src);
}

#if 0 // AKAROS_PORT
static const struct file_operations dver_fops = {
	.read	= dver_read,
	.open	= kfs_open,
	.release= kfs_release,
};

static const struct file_operations dname_fops = {
	.read	= dname_read,
	.open	= kfs_open,
	.release= kfs_release,
};

static const struct file_operations ddev_fops = {
	.read	= ddev_read,
	.open	= kfs_open,
	.release= kfs_release,
};

static const struct file_operations dven_fops = {
	.read	= dven_read,
	.open	= kfs_open,
	.release= kfs_release,
};

static const struct file_operations ntype_fops = {
	.read	= ntype_read,
	.open	= kfs_open,
	.release= kfs_release,
};

static const struct file_operations vsd_fops = {
	.read	= vsd_read,
	.open	= kfs_open,
	.release= kfs_release,
};
#endif

void sysfs_create(int devnum, const struct file_operations *verb_fops,
    void *ptr)
{
#if 1 // AKAROS_PORT
	warn("mlx4: udrvr stuff requires various files, implement for 9ns!");
#else
	char		sysname[256] = "/sys/class/infiniband_verbs/uverbs0";
	char		devname[] = "/dev_vfs/infiniband/uverbs0";
	char		drvname[64] = "/sys/class/infiniband/";
	int		sysnameidx = strlen(sysname), drvidx;
	struct file	*fp;
	struct ib_uverbs_device *uvp = (struct ib_uverbs_device *)ptr;

	/* Create correct name */
	if (devnum > 9)
		panic("Too many devs");
	devname[strlen(devname) - 1] = '0' + devnum;
	sysname[sysnameidx - 1] = '0' + devnum;

	/* Foll fops need to come from caller */
	fp = make_device(devname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)verb_fops);
	set_fs_info(fp, ptr);

	/* /sys/class/infiniband/mlx4_0 */
	strncpy((drvname + strlen(drvname)), uvp->ib_dev->name, 12);
	do_mkdir(drvname, S_IRWXU | S_IRWXG | S_IRWXO);
	drvidx = strlen(drvname);

	/* /sys/class/infiniband/mlx4_0/node_type */
	strncpy(drvname + drvidx, "/node_type", 11);
	make_device(drvname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&ntype_fops);

	/* /sys/class/infiniband/mlx4_0/vsd */
	strncpy(drvname + drvidx, "/vsd", 5);
	fp = make_device(drvname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&vsd_fops);
	set_fs_info(fp, ptr);

	/* /sys/class/infiniband_verbs/uverbs0 */
	do_mkdir(sysname, S_IRWXU | S_IRWXG | S_IRWXO);

	/* /sys/class/infiniband_verbs/uverbs0/device */
	strncpy(sysname + sysnameidx, "/device", 16);
	do_mkdir(sysname, S_IRWXU | S_IRWXG | S_IRWXO);

	/* /sys/class/infiniband_verbs/uverbs0/device/device */
	strncpy(sysname + sysnameidx, "/device/device", 16);
	fp = make_device(sysname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&ddev_fops);
	set_fs_info(fp, ptr);

	/* /sys/class/infiniband_verbs/uverbs0/device/vendor */
	strncpy(sysname + sysnameidx, "/device/vendor", 16);
	fp = make_device(sysname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&dven_fops);
	set_fs_info(fp, ptr);

	/* /sys/class/infiniband_verbs/uverbs0/ibdev */
	strncpy(sysname + sysnameidx, "/ibdev", 16);
	fp = make_device(sysname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&dname_fops);
	set_fs_info(fp, ptr);

	/* /sys/class/infiniband_verbs/uverbs0/abi_version */
	strncpy(sysname + sysnameidx, "/abi_version", 16);
	fp = make_device(sysname,
	    S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH,
	    __S_IFCHR, (struct file_operations *)&dver_fops);
	set_fs_info(fp, ptr);
#endif
}

/* END: Linux /sys support for lib/apps */

/* START: Support older version of libibverbs */

/* in_words and provider_in_words are in terms of 4-byte words, not 8-byte */
struct ib_uverbs_ex_cmd_hdr_compat {
	__u16 provider_in_words;
	__u16 provider_out_words;
	__u32 cmd_hdr_reserved;
	__u32 comp_mask;
	/* __u32 dummy; */
	__u64 response;
	__u32 qp_handle;
};

static ssize_t compat_ex(struct ib_uverbs_file *file, size_t count,
    const char __user *buf)
{
	struct ib_uverbs_cmd_hdr hdr;
	struct ib_uverbs_ex_cmd_hdr_compat ex_hdr;
	struct ib_udata ucore;
	struct ib_udata uhw;
	__u32 command;
	int err;
	unsigned long	tmpbuf[16];
	struct ib_uverbs_create_flow *ptr;

	if (copy_from_user(&hdr, buf, sizeof hdr))
		return -EFAULT;

	command = hdr.command & IB_USER_VERBS_CMD_COMMAND_MASK;
	command -= 2;

	if (command == IB_USER_VERBS_EX_CMD_DESTROY_FLOW) {
		INIT_UDATA_BUF_OR_NULL(&ucore, buf + 8, 0, 8, 0);
		err = ib_uverbs_ex_destroy_flow(file, &ucore, &uhw);
		goto next;
	}

	/*
	 * "struct ibv_create_flow" is 56 bytes, "struct ibv_kern_spec" is
	 * 48 bytes, so at a minimum we expect 56 + (n x 48), n >= 1.
	 */
	if (count < 104)
		return -EINVAL;

	if (copy_from_user(&ex_hdr, buf + sizeof(hdr), sizeof(ex_hdr)))
		return -EFAULT;

	if ((hdr.in_words + ex_hdr.provider_in_words) * 4 != count)
		return -EINVAL;

	if (ex_hdr.cmd_hdr_reserved)
		return -EINVAL;

	if (ex_hdr.comp_mask)
		return -EINVAL;

	if (ex_hdr.response) {
		if (!hdr.out_words && !ex_hdr.provider_out_words)
			return -EINVAL;

		if (!access_ok(VERIFY_WRITE,
			       (void __user *) (unsigned long) ex_hdr.response,
			       (hdr.out_words + ex_hdr.provider_out_words) * 4))
			return -EFAULT;
	} else {
		if (hdr.out_words || ex_hdr.provider_out_words)
			return -EINVAL;
	}

	ptr = (struct ib_uverbs_create_flow *)tmpbuf;
	ptr->comp_mask = 0;	/* user input already validated above */
	ptr->qp_handle = ex_hdr.qp_handle;

	if ((count-36) > 120)
		BUG();

	/* Copy 16 bytes worth "struct ibv_kern_flow_attr" */
	copy_from_user(&tmpbuf[1], buf+36, sizeof(struct ib_uverbs_flow_attr));

	ptr->flow_attr.size -= 56;		/* Comes in as 96 = 56 + 40 */

	/* Copy "struct ibv_kern_spec"s */
	copy_from_user(&tmpbuf[3], buf+56, count-56);

	/*
	 * Copy : count-56 "struct ibv_kern_spec"s,
	 * 16 bytes "struct ibv_kern_flow_attr", 16 bytes comp_mask/qp_handle.
	 */
	copy_to_user((char __user *)buf, tmpbuf, count-24);

	INIT_UDATA_BUF_OR_NULL(&ucore, buf,
	    (unsigned long) ex_hdr.response, count - 24,
	    hdr.out_words * 4);

	err = ib_uverbs_ex_create_flow(file, &ucore, &uhw);

next:
	if (err)
		return err;

	return count;
}

static ssize_t compat(struct ib_uverbs_file *file, size_t count,
    const char __user *buf)
{
	unsigned long			tmpbuf[17];
	struct ib_uverbs_cmd_hdr	*p = (struct ib_uverbs_cmd_hdr *)tmpbuf;
	char __user			*dst = (char __user *)buf;
	int				insz, outsz;

	/*
	 * User "struct ibv_qp_dest" is 40 bytes, passes in 136 bytes.
	 * Kernel "struct ib_uverbs_qp_dest" is 32 bytes, expects 120.
	 * Last 8 bytes of user "struct ibv_qp_dest" not used by kernel.
	 * Kernel expects this layout:
	 * 	struct ib_uverbs_cmd_hdr (8)
	 *	struct ib_uverbs_qp_dest (32 <- 40)
	 *	struct ib_uverbs_qp_dest (32 <- 40)
	 *	Rest of qp_mod inputs	 (48)
	 */

	if (count > 136)
		BUG();

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;
	insz = p->in_words * 4;
	outsz = p->out_words * 4;

	copy_to_user(dst, &tmpbuf[1], sizeof(struct ib_uverbs_qp_dest));
	dst += sizeof(struct ib_uverbs_qp_dest);
	copy_to_user(dst, &tmpbuf[6], sizeof(struct ib_uverbs_qp_dest));
	dst += sizeof(struct ib_uverbs_qp_dest);
	copy_to_user(dst, &tmpbuf[11], 48);


	return ib_uverbs_modify_qp(file, buf, insz, outsz);
}

/*
 * Request structure is:
 * ib_uverbs_cmd_hdr :: (almost) ib_uverbs_ex_cmd_hdr_compat.
 * Response structure is:
 * 8B comp_mask :: ib_uverbs_query_device_resp :: 8B timestamp_mask ::
 * 8B hca_core_clock
 */
static ssize_t compat_query(struct ib_uverbs_file *file, size_t count,
    const char __user *buf)
{
	unsigned long			tmpbuf[17], tval = 0;
	struct ib_uverbs_cmd_hdr	*p = (struct ib_uverbs_cmd_hdr *)tmpbuf;
	char __user			*dst = (char __user *)buf;
	int				insz, outsz;

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;
	insz = p->in_words * 4;
	outsz = p->out_words * 4;

	/* Zero out expected comp_mask field in response */
	copy_to_user((void *)tmpbuf[3], &tval, 8);
	/* Kernel writes out after expected comp_mask field */
	tmpbuf[3] += 8;
	/* Move "response" upwards to "buf" */
	copy_to_user(dst, &tmpbuf[3], sizeof(struct ib_uverbs_query_device));

	return ib_uverbs_query_device(file, buf, insz, outsz);
}

/*
 * Compat hack for applications/libraries we care about. Retrofit Linux 3.12
 * style APIs.
 */
ssize_t check_old_abi(struct file *filp, const char __user *buf, size_t count)
{
	struct ib_uverbs_cmd_hdr hdr;
	int			 tmp;
	struct ib_uverbs_file *file = filp->private_data;

	if (copy_from_user(&hdr, buf, sizeof hdr))
		return -EFAULT;

	tmp = hdr.command & IB_USER_VERBS_CMD_COMMAND_MASK;
	if ((tmp >= 52) && (tmp <= 53)) {
		return compat_ex(file, count, buf);
	} else if (tmp == IB_USER_VERBS_CMD_MODIFY_QP) {
		return compat(file, count, buf);
	} else if (tmp == 56) {
		return compat_query(file, count, buf);
	} else if (tmp == IB_USER_VERBS_CMD_QUERY_QP) {
		panic("query_qp API difference not handled\n");
	}

	/* Continue with processing this command */
	return 0;
}

/* END: Support older version of libibverbs */
