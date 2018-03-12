/* Copyright (c) 2013-2018 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Ron Minnich <rminnich@google.com>
 *
 * See LICENSE for details.  */

#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>

/* Special akaros edition. */
unsigned int convM2kdirent(uint8_t * buf, unsigned int nbuf, struct kdirent *kd,
						   char *strs)
{
	struct dir *dir;
	size_t conv_sz, name_sz;

	if (nbuf < STAT_FIX_LEN_9P)
		return 0;
	dir = kmalloc(sizeof(struct dir) + nbuf, MEM_WAIT);
	conv_sz = convM2D(buf, nbuf, dir, (char*)&dir[1]);

	kd->d_ino = dir->qid.path;
	kd->d_off = 0;		/* ignored for 9ns readdir */
	kd->d_type = 0;		/* TODO: might need this; never used this in the VFS */
	name_sz = dir->name ? strlen(dir->name) : 0;
	kd->d_reclen = name_sz;
	/* Our caller should have made sure kd was big enough... */
	memcpy(kd->d_name, dir->name, name_sz);
	kd->d_name[name_sz] = 0;

	kfree(dir);
	return conv_sz;
}

static int mode_9ns_to_posix(int mode_9ns)
{
	int mode_posix = 0;

	if (mode_9ns & DMDIR)
		mode_posix |= __S_IFDIR;
	else if (mode_9ns & DMSYMLINK)
		mode_posix |= __S_IFLNK;
	else
		mode_posix |= __S_IFREG;
	if (mode_9ns & DMREADABLE)
		mode_posix |= __S_READABLE;
	if (mode_9ns & DMWRITABLE)
		mode_posix |= __S_WRITABLE;
	mode_posix |= mode_9ns & 0777;
	return mode_posix;
}

unsigned int convM2kstat(uint8_t * buf, unsigned int nbuf, struct kstat *ks)
{
	struct dir *dir;
	size_t conv_sz, name_sz;

	if (nbuf < STAT_FIX_LEN_9P)
		return 0;
	dir = kmalloc(sizeof(struct dir) + nbuf, MEM_WAIT);
	conv_sz = convM2D(buf, nbuf, dir, (char*)&dir[1]);

	ks->st_dev = dir->type;
	ks->st_ino = dir->qid.path;
	ks->st_mode = mode_9ns_to_posix(dir->mode);
	ks->st_nlink = dir->mode & DMDIR ? 2 : 1;
	ks->st_uid = dir->n_uid;
	ks->st_gid = dir->n_gid;
	ks->st_rdev = dir->dev;
	ks->st_size = dir->length;
	ks->st_blksize = 1;
	ks->st_blocks = dir->length;
	ks->st_atim = dir->atime;
	ks->st_mtim = dir->mtime;
	ks->st_ctim = dir->ctime;

	kfree(dir);
	return conv_sz;
}
