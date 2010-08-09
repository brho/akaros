#ifndef _ROS_INC_STAT_H
#define _ROS_INC_STAT_H

#include <sys/types.h>
#include <timing.h>

#define MAX_FILENAME_SZ 255
/* This will change once we have a decent readdir / getdents syscall, and
 * send the strlen along with the d_name.  The sizes need rechecked too, since
 * they are probably wrong. */
struct kdirent {
	ino_t          				d_ino;       /* inode number */
	off_t          				d_off;       /* offset to the next dirent */
	unsigned short 				d_reclen;    /* length of this record */
	char           				d_name[MAX_FILENAME_SZ + 1]; /* filename */
};

/* These stat sizes should match the types in stat.h and types.h and the sizes
 * in typesizes in glibc (which we modified slightly).  While glibc has it's own
 * stat, we have this here so that the kernel is exporting the interface it
 * expects.  We #def stat for our own internal use at the end. */
struct kstat {
	__dev_t						st_dev;		/* Device.  */
	__ino64_t					st_ino;		/* File serial number.	*/
	__mode_t					st_mode;	/* File mode.  */
	__nlink_t					st_nlink;	/* Link count.  */
	__uid_t						st_uid;		/* User ID of the file's owner.	*/
	__gid_t						st_gid;		/* Group ID of the file's group.*/
	__dev_t						st_rdev;	/* Device number, if device.  */
	__off64_t					st_size;	/* Size of file, in bytes.  */
	__blksize_t					st_blksize;	/* Optimal block size for I/O.  */
	__blkcnt64_t				st_blocks;	/* Number 512-byte blocks allocated. */
	struct timespec				st_atime;	/* Time of last access.  */
	struct timespec				st_mtime;	/* Time of last modification.  */
	struct timespec				st_ctime;	/* Time of last status change.  */
};

#ifdef ROS_KERNEL
#define stat kstat
#define dirent kdirent 
#endif

#endif /* _ROS_INC_STAT_H */
