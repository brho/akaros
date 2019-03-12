#pragma once

#include <sys/types.h>
#include <ros/limits.h>
#include <ros/time.h>

/* This will change once we have a decent readdir / getdents syscall, and
 * send the strlen along with the d_name.  The sizes need rechecked too, since
 * they are probably wrong. */
struct kdirent {
	__ino64_t		d_ino;		/* inode number */
	__off64_t		d_off;		/* offset to the next dirent */
	unsigned short		d_reclen;	/* length of this record */
	unsigned char		d_type;
	char			d_name[MAX_FILENAME_SZ + 1];	/* filename */
} __attribute__((aligned(8)));

/* These stat sizes should match the types in stat.h and types.h and the sizes
 * in typesizes in glibc (which we modified slightly).  While glibc has it's own
 * stat, we have this here so that the kernel is exporting the interface it
 * expects.  We #def stat for our own internal use at the end. */
struct kstat {
	__dev_t			st_dev;		/* Device.  */
	__ino64_t		st_ino;		/* File serial number.	*/
	__mode_t		st_mode;	/* File mode.  */
	__nlink_t		st_nlink;	/* Link count.  */
	__uid_t			st_uid;		/* User ID of the file owner */
	__gid_t			st_gid;		/* Group ID of the file group */
	__dev_t			st_rdev;	/* Device number, if device.  */
	__off64_t		st_size;	/* Size of file, in bytes.  */
	__blksize_t		st_blksize;	/* Optimal block size for I/O */
	__blkcnt64_t		st_blocks;	/* Nr 512-byte blocks allocd. */
	struct timespec		st_atim;	/* Time of last access.  */
	struct timespec		st_mtim;	/* Time of last modification */
	struct timespec		st_ctim;	/* Time of last status change */
};

/* File access modes for open and fcntl. */
#define O_READ			0x01		/* Open for reading */
#define O_WRITE			0x02		/* Open for writing */
#define O_EXEC			0x04		/* Open for exec */
#define O_RDONLY		O_READ		/* Open read-only */
#define O_WRONLY		O_WRITE		/* Open write-only */
#define O_RDWR			(O_READ | O_WRITE)	/* Open read/write */
#define O_ACCMODE		0x07

/* Bits OR'd into the second argument to open */
#define O_CREAT			00000100	/* not fcntl */
#define O_CREATE		O_CREAT	
#define O_EXCL			00000200	/* not fcntl */
#define O_NOCTTY		00000400	/* not fcntl */
#define O_TRUNC			00001000	/* not fcntl */
#define O_APPEND		00002000
#define O_NONBLOCK		00004000
#define O_SYNC			00010000
#define O_FSYNC			O_SYNC
#define O_ASYNC			00020000
#define O_DIRECT		00040000	/* Direct disk access. */
#define O_PATH			00100000	/* Path only, no I/O */
#define O_DIRECTORY		00200000	/* Must be a directory. */
#define O_NOFOLLOW		00400000	/* Do not follow links. */
#define O_NOATIME		01000000	/* Do not set atime. */
#define O_CLOEXEC		02000000	/* Set close_on_exec. */
#define O_REMCLO		04000000	/* Remove on close. */

/* Keep this value in sync with glibc (io/fcntl.h) */
#define AT_FDCWD		-100

#define F_DUPFD			0	/* Duplicate file descriptor */
#define F_GETFD			1	/* Get file descriptor flags */
#define F_SETFD			2	/* Set file descriptor flags */
#define F_GETFL			3	/* Get file status flags */
#define F_SETFL			4	/* Set file status flags */
#define F_SYNC			101	/* fsync() */
#define F_ADVISE		102	/* posix_fadvise{,64}() */
#define F_CHANCTL_BASE		1000

/* We don't need a GET_FL.  The caller has the chan / FID.  If you have the
 * chan, you already have the flags.  It's not like when you have an FD and
 * don't (yet) have the Unix struct file. */
#define CCTL_SET_FL		(F_CHANCTL_BASE + 0)
#define CCTL_SYNC		(F_CHANCTL_BASE + 1)
#define CCTL_DEBUG		(F_CHANCTL_BASE + 2)

/* For F_[GET|SET]FD */
#define FD_CLOEXEC		1
#define FD_VALID_FLAGS (FD_CLOEXEC)

/* Advise to `posix_fadvise'.  */
#define POSIX_FADV_NORMAL	0	/* No further special treatment */
#define POSIX_FADV_RANDOM	1	/* Expect random page references */
#define POSIX_FADV_SEQUENTIAL	2	/* Expect sequential page references */
#define POSIX_FADV_WILLNEED	3	/* Will need these pages */
#define POSIX_FADV_DONTNEED	4	/* Don't need these pages */
#define POSIX_FADV_NOREUSE	5	/* Data will be accessed once */

/* TODO: have userpsace use our stuff from bits/stats.h */
#ifdef ROS_KERNEL

/* Access mode bits (unistd.h) */
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

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

/* File type is encoded in the file mode */
#define __S_IFMT		000170000	/* These bits determine file type */
/* File types */
#define __S_IFDIR		000040000	/* Directory */
#define __S_IFCHR		000020000	/* Character device */
#define __S_IFBLK		000060000	/* Block device */
#define __S_IFREG		000100000	/* Regular file */
#define __S_IFIFO		000010000	/* FIFO */
#define __S_IFLNK		000120000	/* Symbolic link */
#define __S_IFSOCK		000140000	/* Socket */
/* Protection bits */
#define __S_ISUID		000004000	/* Set user ID on execution */
#define __S_ISGID		000002000	/* Set group ID on execution */
#define __S_ISVTX		000001000	/* Save swapped text after use (sticky) */
#define __S_IREAD		000000400	/* Read by owner */
#define __S_IWRITE		000000200	/* Write by owner */
#define __S_IEXEC		000000100	/* Execute by owner */

/* Test macros for file types */
#define __S_ISTYPE(mode, mask)	(((mode) & __S_IFMT) == (mask))
#define S_ISDIR(mode)	__S_ISTYPE((mode), __S_IFDIR)
#define S_ISCHR(mode)	__S_ISTYPE((mode), __S_IFCHR)
#define S_ISBLK(mode)	__S_ISTYPE((mode), __S_IFBLK)
#define S_ISREG(mode)	__S_ISTYPE((mode), __S_IFREG)
#define S_ISFIFO(mode)	__S_ISTYPE((mode), __S_IFIFO)
#define S_ISLNK(mode)	__S_ISTYPE((mode), __S_IFLNK)

#endif /* ROS_KERNEL */

/* Non-standard bits */
#define __S_NONSTD		077000000	/* Magic Akaros bits */
#define __S_READABLE		001000000	/* File is readable */
#define __S_WRITABLE		002000000	/* File is writable */

/* Test macros for non-standard bits */
#define S_READABLE(mode)	(((mode) & __S_READABLE) != 0)
#define S_WRITABLE(mode)	(((mode) & __S_WRITABLE) != 0)
