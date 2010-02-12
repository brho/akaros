/* Barret Rhoden <brho@cs.berkeley.edu>
 *
 * VFS, based on the Linux VFS as described in LKD 2nd Ed (Robert Love), which
 * was probably written by Linus.  A lot of it was changed (reduced) to handle
 * what ROS will need, at least initially.  Hopefully it'll be similar enough
 * to interface with ext2 and other Linux FSs.
 *
 * struct qstr came directly from Linux.
 * Lawyers can sort out the copyrights and whatnot with these interfaces and
 * structures.
 */

#ifndef ROS_KERN_VFS_H
#define ROS_KERN_VFS_H

#include <ros/common.h>
#include <sys/queue.h>
#include <arch/bitmask.h>
#include <atomic.h>
#include <timing.h>

// TODO: temp typedefs, etc.  remove when we support this stuff.
typedef int dev_t;
typedef int kdev_t;
typedef int ino_t;
typedef long off_t; // out there in other .h's, but not in the kernel yet
struct block_device	{int x;};
struct io_writeback	{int x;};
struct event_poll {int x;};
struct poll_table_struct {int x;};
// end temp typedefs

struct super_block;
struct super_operations;
struct dentry;
struct dentry_operations;
struct inode;
struct inode_operations;
struct file;
struct file_operations;
struct file_system_type;
struct vm_area_struct;
struct vfsmount;

/* part of the kernel interface, ripped from man pages, ought to work. */
// TODO: eventually move this to ros/fs.h or something.
#define MAX_FILENAME 255
struct dirent { // or maybe something else to not conflict with userspace
	ino_t          d_ino;       /* inode number */
	off_t          d_off;       /* offset to the next dirent */
	unsigned short d_reclen;    /* length of this record */
	char           d_name[MAX_FILENAME + 1]; /* filename */
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

/* List def's we need */
TAILQ_HEAD(sb_tailq, super_block);
TAILQ_HEAD(dentry_tailq, dentry);
SLIST_HEAD(dentry_slist, dentry);
TAILQ_HEAD(inode_tailq, inode);
SLIST_HEAD(inode_slist, inode);
TAILQ_HEAD(file_tailq, file);
TAILQ_HEAD(io_wb_tailq, io_writeback);
TAILQ_HEAD(event_poll_tailq, event_poll);
TAILQ_HEAD(vfsmount_tailq, vfsmount);

/* Linux's quickstring - saves recomputing the hash and length. */
struct qstr {
    unsigned int hash;
    unsigned int len;
    const unsigned char *name;
};


/* Superblock: Specific instance of a mounted filesystem.  All synchronization
 * is done with the one spinlock. */

extern struct sb_tailq super_blocks;			/* list of all sbs */
extern spinlock_t super_blocks_lock;

struct super_block {
	TAILQ_ENTRY(superblock)		s_list;			/* list of all sbs */
	dev_t						s_dev;			/* id */
	unsigned long				s_blocksize;
	bool						s_dirty;
	unsigned long long			s_maxbytes;		/* max file size */
	struct file_system_type		*s_type;
	struct super_operations		*s_op;
	unsigned long				s_flags;
	unsigned long				s_magic;
	struct vfsmount				*s_mount;		/* vfsmount point */
	spinlock_t					s_lock;			/* used for all sync */
	atomic_t					s_refcnt;
	bool						s_syncing;		/* currently syncing metadata */
	struct inode_tailq			s_dirty_i;		/* dirty inodes */
	struct io_wb_tailq			s_io_wb;		/* writebacks */
	struct dentry_slist			s_anon_d;		/* anonymous dentries */
	struct file_tailq			s_files;		/* assigned files */
	struct block_device			*s_bdev;
	TAILQ_ENTRY(superblock)		s_instances;	/* list of sbs of this fs type*/
	char						s_name[32];
	void						*s_fs_info;
};

struct super_operations {
	struct inode *(*alloc_inode) (struct super_block *sb);
	void (*destroy_inode) (struct inode *);		/* dealloc.  might need more */
	void (*read_inode) (struct inode *);
	void (*dirty_inode) (struct inode *);
	void (*write_inode) (struct inode *, int);
	void (*delete_inode) (struct inode *);		/* deleted from disk */
	void (*put_super) (struct super_block *);	/* releases sb */
	void (*write_super) (struct super_block *);	/* sync with sb on disk */
	int (*sync_fs) (struct super_block *, int);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*umount_begin) (struct super_block *);/* called by NFS */
};

/* this will create/init a SB for a FS */
void alloc_super(void);

/* Inode: represents a specific file */
struct inode {
	SLIST_ENTRY(inode)			i_hash;			/* inclusion in a hash table */
	TAILQ_ENTRY(inode)			i_list;			/* all inodes in the FS */
	struct dentry_tailq			i_dentry;		/* all dentries pointing here*/
	unsigned long				i_ino;
	atomic_t					i_refcnt;
	int							i_mode;
	unsigned int				i_nlink;		/* hard links */
	uid_t						i_uid;
	gid_t						i_gid;
	kdev_t						i_rdev;			/* real device node */
	size_t						i_size;
	struct timespec				i_atime;
	struct timespec				i_mtime;
	struct timespec				i_ctime;
	unsigned long				i_blksize;
	unsigned long				i_blocks;		/* filesize in blocks */
	spinlock_t					i_lock;
	struct inode_operations		*i_ops;
	struct file_operations		*i_fop;
	struct super_block			*i_sb;
	//struct address_space		*i_mapping;		/* linux mapping structs */
	//struct address_space		i_data;			/* rel page caches */
	union {
		struct pipe_inode_info		*i_pipe;
		struct block_device			*i_bdev;
		struct char_device			*i_cdev;
	};
	unsigned long				i_state;
	unsigned long				dirtied_when;	/* in jiffies */
	unsigned int				i_flags;		/* filesystem flags */
	bool						i_socket;
	atomic_t					i_writecount;	/* number of writers */
	void						*i_fs_info;
};

struct inode_operations {
	int (*create) (struct inode *, struct dentry *, int);
	struct dentry *(*lookup) (struct inode *, struct dentry *);
	int (*link) (struct dentry *, struct inode *, struct dentry *);
	int (*unlink) (struct inode *, struct dentry *);
	int (*symlink) (struct inode *, struct dentry *, const char *);
	int (*mkdir) (struct inode *, struct dentry *, int);
	int (*rmdir) (struct inode *, struct dentry *);
	int (*mknod) (struct inode *, struct dentry *, int, dev_t);
	int (*rename) (struct inode *, struct dentry *,
	               struct inode *, struct dentry *);
	int (*readlink) (struct dentry *, char *, int);
	void (*truncate) (struct inode *);			/* set i_size before calling */
	int (*permission) (struct inode *, int);
};

// TODO: might want a static dentry for /, though processes can get to their
// root via their fs_struct or even the default namespace.
// TODO: should have a dentry_htable or something.  we have the structs built
// in to the dentry right now (linux style).  Need some form of locking too

#define DNAME_INLINE_LEN 32
/* Dentry: in memory object, corresponding to an element of a path.  E.g. /,
 * usr, bin, and vim are all dentries.  All have inodes.  Vim happens to be a
 * file instead of a directory.
 * They can be used (valid inode, currently in use), unused (valid, not used),
 * or negative (not a valid inode (deleted or bad path), but kept to resolve
 * requests quickly.  If none of these, dealloc it back to the slab cache.
 * Unused and negatives go in the LRU list. */
struct dentry {
	atomic_t					d_refcnt;		/* don't discard when 0 */
	unsigned long				d_vfs_flags;	/* dentry cache flags */
	spinlock_t					d_lock;
	struct inode				*d_inode;
	TAILQ_ENTRY(dentry)			d_lru;			/* unused list */
	struct dentry_tailq			d_child;
	struct dentry_tailq			d_subdirs;
	unsigned long				d_time;			/* revalidate time (jiffies)*/
	struct dentry_operations	*d_op;
	struct super_block			*d_sb;
	unsigned int				d_flags;
	bool						d_mount_point;
	void						*d_fs_info;
	struct dentry				*d_parent;
	struct qstr					d_name;			/* pts to iname and holds hash*/
	SLIST_ENTRY(dentry)			d_hash;			/* link for the dcache */
	struct dentry_slist			*d_bucket;		/* hash bucket of this dentry */
	unsigned char				d_iname[DNAME_INLINE_LEN];
};

/* not sure yet if we want to call delete when refcnt == 0 (move it to LRU) or
 * when its time to remove it from the dcache. */
struct dentry_operations {
	int (*d_revalidate) (struct dentry *, int);	/* usually a noop */
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	int (*d_delete) (struct dentry *);
	void (*d_iput) (struct dentry *, struct inode *);
};

/* File: represents a file opened by a process. */
struct file {
	TAILQ_ENTRY(file)			f_list;			/* list of all files */
	struct dentry				*f_dentry;
	struct vfsmount				*f_vfsmnt;
	struct file_operations		*f_op;
	atomic_t					f_refcnt;
	unsigned int				f_flags;
	int							f_mode;
	off_t						f_pos;			/* offset / file pointer */
	unsigned int				f_uid;
	unsigned int				f_gid;
	int							f_error;
	struct event_poll_tailq		f_ep_links;
	spinlock_t					f_ep_lock;
	void						*f_fs_info;		/* tty driver hook */
	//struct address_space		*f_mapping;		/* page cache mapping */
};

struct file_operations {
	struct module *owner;
	off_t (*llseek) (struct file *, off_t, int);
	ssize_t (*read) (struct file *, char *, size_t, off_t *);
	ssize_t (*write) (struct file *, const char *, size_t, off_t *);
	int (*readdir) (struct file *, struct dirent *);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long,
	                  off_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long,
	                  off_t *);
	int (*check_flags) (int flags);				/* most FS's ignore this */
};

/* FS structs.  One of these per FS (e.g., ext2) */
struct file_system_type {
	const char					*name;
	int							fs_flags;
	struct superblock			*(*get_sb) (struct file_system_type *, int,
	                                        char *, void *);
	void						(*kill_sb) (struct super_block *);
	struct file_system_type		*next;			/* next FS */
	struct sb_tailq				fs_supers;		/* all of this FS's sbs */
};

/* A mount point: more focused on the mounting, and solely in memory, compared
 * to the SB which is focused on FS definitions (and exists on disc). */
struct vfsmount {
	TAILQ_ENTRY(vfsmount)		mnt_list;		/* might want a hash instead */
	struct vfsmount				*mnt_parent;
	struct dentry				*mnt_mountpoint;/* do we need both of them? */
	struct dentry				*mnt_root;		/* do we need both of them? */
	struct super_block			*mnt_sb;
	struct vfsmount_tailq		mnt_child_mounts;
	TAILQ_ENTRY(vfsmount)		mnt_child_link;
	atomic_t					mnt_refcnt;
	int							mnt_flags;
	char						*mnt_devname;
	struct namespace			*mnt_namespace;
};

/* Per-process structs */
#define NR_OPEN_FILES_DEFAULT 32
#define NR_FILE_DESC_DEFAULT 32
#define NR_FILE_DESC_MAX 1024

/* Bitmask for file descriptors, big for when we exceed the initial small.  We
 * could just use the fd_array to check for openness instead of the bitmask,
 * but eventually we might want to use the bitmasks for other things (like
 * which files are close_on_exec. */
struct fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_MAX)];
};
struct small_fd_set {
    uint8_t fds_bits[BYTES_FOR_BITMASK(NR_FILE_DESC_DEFAULT)];
};

/* All open files for a process */
struct files_struct {
	atomic_t					refcnt;
	spinlock_t					lock;
	int							max_files;		/* max files ptd to by fd */
	int							max_fdset;		/* max of the current fd_set */
	int							next_fd;		/* next number available */
	struct file					**fd;			/* initially pts to fd_array */
	struct fd_set				*open_fds;		/* init, pts to open_fds_init */
	struct small_fd_set			open_fds_init;
	struct file					*fd_array[NR_OPEN_FILES_DEFAULT];
};

/* Process specific filesysten info */
struct fs_struct {
	atomic_t 					refcnt;
	spinlock_t					lock;
	int							umask;
	struct dentry				*root;
	struct dentry				*pwd;
};

/* Each process can have it's own (eventually), but default to the same NS */
struct namespace {
	atomic_t					refcnt;
	spinlock_t					lock;
	struct vfsmount				*root;
	struct vfsmount_tailq		vfsmounts;	/* all vfsmounts in this ns */
};

extern struct namespace default_ns;

#endif /* ROS_KERN_VFS_H */
