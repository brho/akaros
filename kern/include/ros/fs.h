#ifndef _ROS_INC_STAT_H
#define _ROS_INC_STAT_H

#include <sys/types.h>
#include <timing.h>

/* Keep this 255 to stay in sync with glibc (expects d_name[256]) */
#define MAX_FILENAME_SZ 255
/* This will change once we have a decent readdir / getdents syscall, and
 * send the strlen along with the d_name.  The sizes need rechecked too, since
 * they are probably wrong. */
struct kdirent {
	__ino64_t					d_ino;		/* inode number */
	__off64_t					d_off;		/* offset to the next dirent */
	unsigned short				d_reclen;	/* length of this record */
	unsigned char				d_type;
	char						d_name[MAX_FILENAME_SZ + 1];	/* filename */
} __attribute__((aligned(8)));

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

/* There are a bunch of things that glibc expects, which are part of the kernel
 * interface, but that we don't want to clobber or otherwise conflict with
 * glibc. */
#ifdef ROS_KERNEL
#define stat kstat
#define dirent kdirent 

/* File access modes for open and fcntl. */
#define O_RDONLY		0			/* Open read-only */
#define O_WRONLY		1			/* Open write-only */
#define O_RDWR			2			/* Open read/write */
#define O_ACCMODE		3

/* Bits OR'd into the second argument to open */
#define O_CREAT			00000100	/* not fcntl */
#define O_EXCL			00000200	/* not fcntl */
#define O_NOCTTY		00000400	/* not fcntl */
#define O_TRUNC			00001000	/* not fcntl */
#define O_APPEND		00002000
#define O_NONBLOCK		00004000
#define O_SYNC			00010000
#define O_FSYNC			O_SYNC
#define O_ASYNC			00020000
#define O_DIRECT		00040000	/* Direct disk access. */
#define O_DIRECTORY		00200000	/* Must be a directory. */
#define O_NOFOLLOW		00400000	/* Do not follow links. */
#define O_NOATIME		01000000	/* Do not set atime. */
#define O_CLOEXEC		02000000	/* Set close_on_exec. */
#define O_CREAT_FLAGS (O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC)
#define O_FCNTL_FLAGS (O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK)

/* File creation modes (access controls) */
#define S_IRWXU 00700	/* user (file owner) has read, write and execute perms */
#define S_IRUSR 00400	/* user has read permission */
#define S_IWUSR 00200	/* user has write permission */
#define S_IXUSR 00100	/* user has execute permission */
#define S_IRWXG 00070	/* group has read, write and execute permission */
#define S_IRGRP 00040	/* group has read permission */
#define S_IWGRP 00020	/* group has write permission */
#define S_IXGRP 00010	/* group has execute permission */
#define S_IRWXO 00007	/* others have read, write and execute permission */
#define S_IROTH 00004	/* others have read permission */
#define S_IWOTH 00002	/* others have write permission */
#define S_IXOTH 00001	/* others have execute permission */
#define S_PMASK 00777	/* mask for all perms */

/* fcntl flags that we support, keep in sync with glibc */
#define F_DUPFD		0	/* Duplicate file descriptor */
#define F_GETFD		1	/* Get file descriptor flags */
#define F_SETFD		2	/* Set file descriptor flags */
#define F_GETFL		3	/* Get file status flags */
#define F_SETFL		4	/* Set file status flags */
/* For F_[GET|SET]FD */
#define FD_CLOEXEC	1

/* File type is encoded in the file mode */
#define __S_IFMT	0170000	/* These bits determine file type */
/* File types */
#define __S_IFDIR	0040000	/* Directory */
#define __S_IFCHR	0020000	/* Character device */
#define __S_IFBLK	0060000	/* Block device */
#define __S_IFREG	0100000	/* Regular file */
#define __S_IFIFO	0010000	/* FIFO */
#define __S_IFLNK	0120000	/* Symbolic link */
#define __S_IFSOCK	0140000	/* Socket */
/* Protection bits */
#define __S_ISUID	04000	/* Set user ID on execution */
#define __S_ISGID	02000	/* Set group ID on execution */
#define __S_ISVTX	01000	/* Save swapped text after use (sticky) */
#define __S_IREAD	0400	/* Read by owner */
#define __S_IWRITE	0200	/* Write by owner */
#define __S_IEXEC	0100	/* Execute by owner */
/* Test macros for file types.	*/
#define __S_ISTYPE(mode, mask)	(((mode) & __S_IFMT) == (mask))
#define S_ISDIR(mode)	__S_ISTYPE((mode), __S_IFDIR)
#define S_ISCHR(mode)	__S_ISTYPE((mode), __S_IFCHR)
#define S_ISBLK(mode)	__S_ISTYPE((mode), __S_IFBLK)
#define S_ISREG(mode)	__S_ISTYPE((mode), __S_IFREG)
#define S_ISFIFO(mode)	__S_ISTYPE((mode), __S_IFIFO)
#define S_ISLNK(mode)	__S_ISTYPE((mode), __S_IFLNK)

#endif /* ROS_KERNEL */

#endif /* _ROS_INC_STAT_H */
