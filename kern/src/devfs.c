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

/* These structs are declared again and initialized farther down */
struct file_operations dev_f_op_stdin;
struct file_operations dev_f_op_stdout;

struct file *dev_stdin, *dev_stdout, *dev_stderr;

/* Helper to build stdin, stdout, and stderr */
static struct file *get_stdinout(char *name, int mode,
                                 struct file_operations *fop)
{
	struct file *f_char_dev = do_file_open(name, O_CREAT, mode);
	assert(f_char_dev);
	/* Overwrite the f_op with our own f_ops */
	f_char_dev->f_dentry->d_inode->i_fop = fop;
	f_char_dev->f_op = fop;
	SET_FTYPE(f_char_dev->f_dentry->d_inode->i_mode, __S_IFCHR);
	return f_char_dev;
}

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
	dev_stdin = get_stdinout("/dev/stdin", S_IRUSR | S_IRGRP | S_IROTH,
	                         &dev_f_op_stdin);
	dev_stdout = get_stdinout("/dev/stdout", S_IWUSR | S_IWGRP | S_IWOTH,
	                          &dev_f_op_stdout);
	/* Note stderr uses the same f_op as stdout */
	dev_stderr = get_stdinout("/dev/stderr", S_IWUSR | S_IWGRP | S_IWOTH,
	                          &dev_f_op_stdout);
}

/* We provide a separate set of f_ops and pm_ops for devices (char for now), and
 * this is the only thing that differs from the regular KFS.  We need to do some
 * ghetto-overriding of these ops after we create them. */

off_t dev_c_llseek(struct file *file, off_t offset, int whence)
{
	set_errno(EINVAL);
	return -1;
}

ssize_t dev_stdin_read(struct file *file, char *buf, size_t count,
                       off_t *offset)
{
	printk("[kernel] Tried to read %d bytes from stdin\n", count);
	return -1;
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

/* we don't allow mmapping of any device file */
int dev_mmap(struct file *file, struct vm_region *vmr)
{
	set_errno(EINVAL);
	return -1;
}

/* Character device file ops */
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
