/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Devfs: filesystem interfaces to devices.  For now, we just create the
 * needed/discovered devices in KFS in its /dev/ folder, and only do this for
 * stdin and stdout. */

#include <devfs.h>
#include <kfs.h>
#include <error.h>
#include <syscall.h>
#include <process.h>
#include <smp.h>
#include <umem.h>
#include <kmalloc.h>
#include <ns.h>

void devfs_init(void)
{
	int mode;
	/* Make sure there is a dev directory */
	struct dentry *dentry = lookup_dentry("/dev/", 0);
	if (!dentry) {
		assert(!do_mkdir("/dev/", S_IRWXU | S_IRWXG | S_IRWXO));
	} else {
		kref_put(&dentry->d_kref);
	}
}

/* Creates a device node at a given location in the FS-tree */
/* TODO: consider making this only deal with the inode */
struct file *make_device(char *path, int mode, int type,
                         struct file_operations *fop)
{
	struct file *f_dev = do_file_open(path, O_CREAT | O_RDWR, mode);
	assert(f_dev);
	/* Overwrite the f_op with our own f_ops */
	f_dev->f_dentry->d_inode->i_fop = fop;
	f_dev->f_op = fop;
	SET_FTYPE(f_dev->f_dentry->d_inode->i_mode, type);
	return f_dev;
}

/* We provide a separate set of f_ops for devices (char and block), and the fops
 * is the only thing that differs from the regular KFS.  We need to do some
 * ghetto-overriding of these ops after we create them. */
int dev_c_llseek(struct file *file, off64_t offset, off64_t *ret, int whence)
{
	set_errno(EINVAL);
	return -1;
}

/* we don't allow mmapping of any device file */
int dev_mmap(struct file *file, struct vm_region *vmr)
{
	set_errno(EINVAL);
	return -1;
}
