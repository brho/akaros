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
#include <console.h>

/* These structs are declared again and initialized farther down */
struct file_operations dev_f_op_stdin;
struct file_operations dev_f_op_stdout;
struct file_operations dev_f_op_null;

struct file *dev_stdin, *dev_stdout, *dev_stderr, *dev_null;

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
	dev_null = make_device("/dev/null", S_IWUSR | S_IWGRP | S_IWOTH,
	                       __S_IFCHR, &dev_f_op_null);
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

/* this is really /dev/console, and will need some tty work.  for now, no matter
 * how much they ask for, we return one character at a time. */
ssize_t dev_stdin_read(struct file *file, char *buf, size_t count,
                       off64_t *offset)
{
	char c;
	extern struct kb_buffer cons_buf;

	if (!count)
		return 0;
	kb_get_from_buf(&cons_buf, &c, 1);
	/* TODO UMEM */
	if (current)
		memcpy_to_user_errno(current, buf, &c, 1);
	else
		memcpy(buf, &c, 1);
	return 1;
}

ssize_t dev_stdout_write(struct file *file, const char *buf, size_t count,
                         off64_t *offset)
{
	char *t_buf;
	struct proc *p = current;
	if (p)
		t_buf = user_memdup_errno(p, buf, count);
	else
		t_buf = (char*)buf;
	if (!t_buf)
		return -1;
	/* TODO: tty hack.  they are sending us an escape sequence, and the keyboard
	 * would try to print it (which it can't do yet).  The hack is even dirtier
	 * in that we only detect it if it is the first char, and we ignore
	 * everything else. */
	if (t_buf[0] != '\033') /* 0x1b */
		cputbuf(t_buf, count);
	if (p)
		user_memdup_free(p, t_buf);
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

ssize_t dev_null_read(struct file *file, char *buf, size_t count,
                      off64_t *offset)
{
	return 0;
}

/* /dev/null: just take whatever was given and pretend it was written */
ssize_t dev_null_write(struct file *file, const char *buf, size_t count,
                       off64_t *offset)
{
	return count;
}

struct file_operations dev_f_op_null = {
	dev_c_llseek,
	dev_null_read,
	dev_null_write,
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
