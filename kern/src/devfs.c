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

/* These structs are declared again and initialized farther down */
struct file_operations dev_f_op_stdin;
struct file_operations dev_f_op_stdout;

struct file *dev_stdin, *dev_stdout, *dev_stderr;

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
	/* Notice we don't kref_put().  We're storing the refs globally */
	dev_stdin = make_device("/dev/stdin", S_IRUSR | S_IRGRP | S_IROTH,
	                        __S_IFCHR, &dev_f_op_stdin);
	dev_stdout = make_device("/dev/stdout", S_IWUSR | S_IWGRP | S_IWOTH,
	                         __S_IFCHR, &dev_f_op_stdout);
	/* Note stderr uses the same f_op as stdout */
	dev_stderr = make_device("/dev/stderr", S_IWUSR | S_IWGRP | S_IWOTH,
	                         __S_IFCHR, &dev_f_op_stdout);
}

/* Creates a device node at a given location in the FS-tree */
/* TODO: consider making this only deal with the inode */
struct file *make_device(char *path, int mode, int type,
                         struct file_operations *fop)
{
	struct file *f_dev = do_file_open(path, O_CREAT, mode);
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

off_t dev_c_llseek(struct file *file, off_t offset, int whence)
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

ssize_t dev_stdin_read(struct file *file, char *buf, size_t count,
                       off_t *offset)
{
	int read_amt;
	char *kbuf = kmalloc(count, 0);
	memset(kbuf, 0, count);// TODO REMOVE ME
	read_amt = readline(kbuf, count, 0);
	assert(read_amt <= count);
	/* applications (ash) expect a \n instead of a \r */
	if (kbuf[read_amt - 1] == '\r')
		kbuf[read_amt - 1] = '\n';
	/* TODO UMEM */
	if (current)
		memcpy_to_user_errno(current, buf, kbuf, read_amt);
	else
		memcpy(buf, kbuf, read_amt);
	return read_amt;
}

ssize_t dev_stdout_write(struct file *file, const char *buf, size_t count,
                         off_t *offset)
{
	char *t_buf;
	struct proc *p = current;
	if (p)
		t_buf = user_strdup_errno(p, buf, count);
	else
		t_buf = (char*)buf;
	if (!t_buf)
		return -1;
	printk("%s", t_buf);
	return count;
}

/* stdin/stdout/stderr file ops */
struct file_operations dev_f_op_stdin = {
	dev_c_llseek,
	dev_stdin_read,
	0,	/* write - can't write to stdin */
	kfs_readdir,	/* this will fail gracefully */
	dev_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	0,	/* fsync - makes no sense */
	kfs_poll,
	0,	/* readv */
	0,	/* writev */
	kfs_sendpage,
	kfs_check_flags,
};

struct file_operations dev_f_op_stdout = {
	dev_c_llseek,
	0,	/* read - can't read stdout */
	dev_stdout_write,
	kfs_readdir,	/* this will fail gracefully */
	dev_mmap,
	kfs_open,
	kfs_flush,
	kfs_release,
	0,	/* fsync - makes no sense */
	kfs_poll,
	0,	/* readv */
	0,	/* writev */
	kfs_sendpage,
	kfs_check_flags,
};
