/* Barret Rhoden <brho@cs.berkeley.edu>
 *
 * VFS, based on the Linux VFS as described in LKD 2nd Ed (Robert Love) and in
 * UTLK (Bovet/Cesati) , which was probably written by Linus.  A lot of it was
 * changed (reduced) to handle what ROS will need, at least initially.
 * Hopefully it'll be similar enough to interface with ext2 and other Linux
 * FSs.
 *
 * struct qstr came directly from Linux.
 * Lawyers can sort out the copyrights and whatnot with these interfaces and
 * structures. */

#ifndef ROS_KERN_VFS_H
#define ROS_KERN_VFS_H

#include <ros/common.h>
#include <sys/queue.h>
#include <bitmask.h>
#include <kref.h>
#include <timing.h>
#include <radix.h>
#include <hashtable.h>
#include <pagemap.h>
#include <blockdev.h>

/* ghetto preprocessor hacks (since proc includes vfs) */
struct page;
struct vm_region;

// TODO: temp typedefs, etc.  remove when we support this stuff.
typedef int dev_t;
typedef int kdev_t;
typedef int ino_t;
typedef long off_t; // out there in other .h's, but not in the kernel yet
struct io_writeback	{int x;};
struct event_poll {int x;};
struct poll_table_struct {int x;};
// end temp typedefs.  note ino and off_t are needed in the next include

#include <ros/fs.h>

struct super_block;
struct super_operations;
struct dentry;
struct dentry_operations;
struct inode;
struct inode_operations;
struct file;
struct file_operations;
struct fs_type;
struct vfsmount;

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
TAILQ_HEAD(fs_type_tailq, fs_type); 

/* Linux's quickstring - saves recomputing the hash and length.  Note the length
 * is the non-null-terminated length, as you'd get from strlen(). (for now) */
struct qstr {
    unsigned int hash;
    unsigned int len;
    char *name;
};

/* Helpful structure to pass around during lookup operations.  At each point,
 * it tracks the the answer, the name of the previous, how deep the symlink
 * following has gone, and the symlink pathnames.  *dentry and *mnt up the
 * refcnt of those objects too, so whoever 'receives; this will need to decref.
 * This is meant to be pinning only the 'answer' to a path_lookup, and not the
 * intermediate steps.  The intermediates get pinned due to the existence of
 * their children in memory.  Internally, the VFS will refcnt any item whenever
 * it is in this struct.  The last_sym is needed to pin the dentry (and thus the
 * inode and char* storage for the symname) for the duration of a lookup.  When
 * you resolve a pathname, you need to keep its string in memory. */
#define MAX_SYMLINK_DEPTH 6 // arbitrary.
struct nameidata {
	struct dentry				*dentry;		/* dentry of the obj */
	struct vfsmount				*mnt;			/* its mount pt */
	struct qstr					last;			/* last component in search */
	int							flags;			/* lookup flags */
	int							last_type;		/* type of last component */
	unsigned int				depth;			/* search's symlink depth */
	int							intent;			/* access type for the file */
	struct dentry				*last_sym;		/* pins the symname */
};

/* nameidata lookup flags and access type fields */
#define LOOKUP_FOLLOW 		0x01	/* if the last is a symlink, follow */
#define LOOKUP_DIRECTORY 	0x02	/* last component must be a directory */
#define LOOKUP_CONTINUE 	0x04	/* still filenames to go */
#define LOOKUP_PARENT 		0x08	/* lookup the dir that includes the item */
/* These are the nd's intent */
#define LOOKUP_OPEN 		0x10	/* intent is to open a file */
#define LOOKUP_CREATE 		0x11	/* create a file if it doesn't exist */
#define LOOKUP_ACCESS 		0x12	/* access / check user permissions */

/* Superblock: Specific instance of a mounted filesystem.  All synchronization
 * is done with the one spinlock. */

struct super_block {
	TAILQ_ENTRY(super_block)	s_list;			/* list of all sbs */
	dev_t						s_dev;			/* id */
	unsigned long				s_blocksize;
	bool						s_dirty;
	unsigned long long			s_maxbytes;		/* max file size */
	struct fs_type				*s_type;
	struct super_operations		*s_op;
	unsigned long				s_flags;
	unsigned long				s_magic;
	struct vfsmount				*s_mount;		/* vfsmount point */
	spinlock_t					s_lock;			/* used for all sync */
	struct kref					s_kref;
	bool						s_syncing;		/* currently syncing metadata */
	struct inode_tailq			s_inodes;		/* all inodes */
	struct inode_tailq			s_dirty_i;		/* dirty inodes */
	struct io_wb_tailq			s_io_wb;		/* writebacks */
	struct file_tailq			s_files;		/* assigned files */
	struct dentry_tailq			s_lru_d;		/* unused dentries (in dcache)*/
	spinlock_t					s_lru_lock;
	struct hashtable			*s_dcache;		/* dentry cache */
	spinlock_t					s_dcache_lock;
	struct hashtable			*s_icache;		/* inode cache */
	spinlock_t					s_icache_lock;
	struct block_device			*s_bdev;
	TAILQ_ENTRY(super_block)	s_instances;	/* list of sbs of this fs type*/
	char						s_name[32];
	void						*s_fs_info;
};

struct super_operations {
	struct inode *(*alloc_inode) (struct super_block *sb);
	void (*dealloc_inode) (struct inode *);
	void (*read_inode) (struct inode *);
	void (*dirty_inode) (struct inode *);
	void (*write_inode) (struct inode *, bool);
	void (*put_inode) (struct inode *);			/* when decreffed */
	void (*drop_inode) (struct inode *);		/* when about to destroy */
	void (*delete_inode) (struct inode *);		/* deleted from disk */
	void (*put_super) (struct super_block *);	/* releases sb */
	void (*write_super) (struct super_block *);	/* sync with sb on disk */
	int (*sync_fs) (struct super_block *, bool);
	int (*remount_fs) (struct super_block *, int, char *);
	void (*umount_begin) (struct super_block *);/* called by NFS */
};

/* Sets the type of file, IAW the bits in ros/fs.h */
#define SET_FTYPE(mode, type) ((mode) = ((mode) & ~__S_IFMT) | (type))

/* Will need a bunch of states/flags for an inode.  TBD */
#define I_STATE_DIRTY			0x001

/* Inode: represents a specific file */
struct inode {
	SLIST_ENTRY(inode)			i_hash;			/* inclusion in a hash table */
	TAILQ_ENTRY(inode)			i_sb_list;		/* all inodes in the FS */
	TAILQ_ENTRY(inode)			i_list;			/* describes state (dirty) */
	struct dentry_tailq			i_dentry;		/* all dentries pointing here*/
	unsigned long				i_ino;
	struct kref					i_kref;
	int							i_mode;			/* access mode and file type */
	unsigned int				i_nlink;		/* hard links */
	uid_t						i_uid;
	gid_t						i_gid;
	kdev_t						i_rdev;			/* real device node */
	size_t						i_size;
	unsigned long				i_blksize;
	unsigned long				i_blocks;		/* filesize in blocks */
	struct timespec				i_atime;
	struct timespec				i_mtime;
	struct timespec				i_ctime;
	spinlock_t					i_lock;
	struct inode_operations		*i_op;
	struct file_operations		*i_fop;
	struct super_block			*i_sb;
	struct page_map				*i_mapping;		/* usually points to i_pm */
	struct page_map				i_pm;			/* this inode's page cache */
	union {
		struct pipe_inode_info		*i_pipe;
		struct block_device			*i_bdev;
		struct char_device			*i_cdev;
	};
	unsigned long				i_state;
	unsigned long				dirtied_when;	/* in jiffies */
	unsigned int				i_flags;		/* filesystem mount flags */
	bool						i_socket;
	atomic_t					i_writecount;	/* number of writers */
	void						*i_fs_info;
};

struct inode_operations {
	int (*create) (struct inode *, struct dentry *, int, struct nameidata *);
	struct dentry *(*lookup) (struct inode *, struct dentry *,
	                          struct nameidata *);
	int (*link) (struct dentry *, struct inode *, struct dentry *);
	int (*unlink) (struct inode *, struct dentry *);
	int (*symlink) (struct inode *, struct dentry *, const char *);
	int (*mkdir) (struct inode *, struct dentry *, int);
	int (*rmdir) (struct inode *, struct dentry *);
	int (*mknod) (struct inode *, struct dentry *, int, dev_t);
	int (*rename) (struct inode *, struct dentry *,
	               struct inode *, struct dentry *);
	char *(*readlink) (struct dentry *);
	void (*truncate) (struct inode *);			/* set i_size before calling */
	int (*permission) (struct inode *, int, struct nameidata *);
};

#define DNAME_INLINE_LEN 32

/* Dentry flags.  All negatives are also unused. */
#define DENTRY_USED			0x01 	/* has a kref > 0 */
#define DENTRY_NEGATIVE		0x02	/* cache of a failed lookup */
#define DENTRY_DYING		0x04	/* should be freed on release */

/* Dentry: in memory object, corresponding to an element of a path.  E.g. /,
 * usr, bin, and vim are all dentries.  All have inodes.  Vim happens to be a
 * file instead of a directory.
 * They can be used (valid inode, currently in use), unused (valid, not used),
 * or negative (not a valid inode (deleted or bad path), but kept to resolve
 * requests quickly.  If none of these, dealloc it back to the slab cache.
 * Unused and negatives go in the LRU list. */
struct dentry {
	struct kref					d_kref;			/* don't discard when 0 */
	unsigned long				d_flags;		/* dentry cache flags */
	spinlock_t					d_lock;
	struct inode				*d_inode;
	TAILQ_ENTRY(dentry)			d_lru;			/* unused list */
	TAILQ_ENTRY(dentry)			d_alias;		/* linkage for i_dentry */
	struct dentry_tailq			d_subdirs;
	TAILQ_ENTRY(dentry)			d_subdirs_link;
	unsigned long				d_time;			/* revalidate time (jiffies)*/
	struct dentry_operations	*d_op;
	struct super_block			*d_sb;
	bool						d_mount_point;	/* is an FS mounted over here */
	struct vfsmount				*d_mounted_fs;	/* fs mounted here */
	struct dentry				*d_parent;
	struct qstr					d_name;			/* pts to iname and holds hash*/
	char						d_iname[DNAME_INLINE_LEN];
	void						*d_fs_info;
};

/* not sure yet if we want to call delete when refcnt == 0 (move it to LRU) or
 * when its time to remove it from the dcache. */
struct dentry_operations {
	int (*d_revalidate) (struct dentry *, struct nameidata *);
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	int (*d_delete) (struct dentry *);
	int (*d_release) (struct dentry *);
	void (*d_iput) (struct dentry *, struct inode *);
};

/* Yanked from glibc-2.11.1/posix/unistd.h */
#define SEEK_SET   0   /* Seek from beginning of file.  */
#define SEEK_CUR   1   /* Seek from current position.  */
#define SEEK_END   2   /* Seek from end of file.  */

/* File: represents a file opened by a process. */
struct file {
	TAILQ_ENTRY(file)			f_list;			/* list of all files */
	struct dentry				*f_dentry;		/* definitely not inode.  =( */
	struct vfsmount				*f_vfsmnt;
	struct file_operations		*f_op;
	struct kref					f_kref;
	unsigned int				f_flags;		/* O_APPEND, etc */
	int							f_mode;			/* O_RDONLY, etc */
	off_t						f_pos;			/* offset / file pointer */
	unsigned int				f_uid;
	unsigned int				f_gid;
	int							f_error;
	struct event_poll_tailq		f_ep_links;
	spinlock_t					f_ep_lock;
	void						*f_fs_info;		/* tty driver hook */
	struct page_map				*f_mapping;		/* page cache mapping */

	/* Ghetto appserver support */
	int fd; // all it contains is an appserver fd (for pid 0, aka kernel)
	int refcnt;
	spinlock_t lock;
};

struct file_operations {
	off_t (*llseek) (struct file *, off_t, int);
	ssize_t (*read) (struct file *, char *, size_t, off_t *);
	ssize_t (*write) (struct file *, const char *, size_t, off_t *);
	int (*readdir) (struct file *, struct dirent *);
	int (*mmap) (struct file *, struct vm_region *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *, int);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	ssize_t (*readv) (struct file *, const struct iovec *, unsigned long,
	                  off_t *);
	ssize_t (*writev) (struct file *, const struct iovec *, unsigned long,
	                  off_t *);
	ssize_t (*sendpage) (struct file *, struct page *, int, size_t, off_t, int);
	int (*check_flags) (int flags);				/* most FS's ignore this */
};

/* FS structs.  One of these per FS (e.g., ext2) */
struct fs_type {
	const char					*name;
	int							fs_flags;
	struct super_block			*(*get_sb) (struct fs_type *, int,
	                                        char *, struct vfsmount *);
	void						(*kill_sb) (struct super_block *);
	TAILQ_ENTRY(fs_type)		list;
	struct sb_tailq				fs_supers;		/* all of this FS's sbs */
};

/* A mount point: more focused on the mounting, and solely in memory, compared
 * to the SB which is focused on FS definitions (and exists on disc). */
struct vfsmount {
	TAILQ_ENTRY(vfsmount)		mnt_list;
	struct vfsmount				*mnt_parent;
	struct dentry				*mnt_mountpoint;/* parent dentry where mnted */
	struct dentry				*mnt_root;		/* dentry of root of this fs */
	struct super_block			*mnt_sb;
	struct vfsmount_tailq		mnt_child_mounts;
	TAILQ_ENTRY(vfsmount)		mnt_child_link;
	struct kref					mnt_kref;
	int							mnt_flags;
	char						*mnt_devname;
	struct namespace			*mnt_namespace;
};

/* Per-process structs */
#define NR_OPEN_FILES_DEFAULT 32
#define NR_FILE_DESC_DEFAULT 32
/* keep this in sync with glibc's fd_setsize */
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

/* Describes an open file.  We need this, since the FD flags are supposed to be
 * per file descriptor, not per file (like the file status flags). */
struct file_desc {
	struct file					*fd_file;
	unsigned int				fd_flags;
};

/* All open files for a process */
struct files_struct {
	spinlock_t					lock;
	int							max_files;		/* max files ptd to by fd */
	int							max_fdset;		/* max of the current fd_set */
	int							next_fd;		/* next number available */
	struct file_desc			*fd;			/* initially pts to fd_array */
	struct fd_set				*open_fds;		/* init, pts to open_fds_init */
	struct small_fd_set			open_fds_init;
	struct file_desc			fd_array[NR_OPEN_FILES_DEFAULT];
};

/* Process specific filesysten info */
struct fs_struct {
	spinlock_t					lock;
	int							umask;
	struct dentry				*root;
	struct dentry				*pwd;
};

/* Each process can have its own (eventually), but default to the same NS */
struct namespace {
	struct kref					kref;
	spinlock_t					lock;
	struct vfsmount				*root;
	struct vfsmount_tailq		vfsmounts;	/* all vfsmounts in this ns */
};

/* Global Structs */
extern struct sb_tailq super_blocks;			/* list of all sbs */
extern spinlock_t super_blocks_lock;
extern struct fs_type_tailq file_systems;		/* lock this if it's dynamic */
extern struct namespace default_ns;

/* Slab caches for common objects */
extern struct kmem_cache *dentry_kcache;
extern struct kmem_cache *inode_kcache;
extern struct kmem_cache *file_kcache;

/* Misc VFS functions */
void vfs_init(void);
void qstr_builder(struct dentry *dentry, char *l_name);
char *file_name(struct file *file);
int path_lookup(char *path, int flags, struct nameidata *nd);
void path_release(struct nameidata *nd);
int mount_fs(struct fs_type *fs, char *dev_name, char *path, int flags);

/* Superblock functions */
struct super_block *get_sb(void);
void init_sb(struct super_block *sb, struct vfsmount *vmnt,
             struct dentry_operations *d_op, unsigned long root_ino,
             void *d_fs_info);

/* Dentry Functions */
struct dentry *get_dentry(struct super_block *sb, struct dentry *parent,
                          char *name);
void dentry_release(struct kref *kref);
void __dentry_free(struct dentry *dentry);
struct dentry *lookup_dentry(char *path, int flags);
struct dentry *dcache_get(struct super_block *sb, struct dentry *what_i_want);
void dcache_put(struct super_block *sb, struct dentry *key_val);
struct dentry *dcache_remove(struct super_block *sb, struct dentry *key);
void dcache_prune(struct super_block *sb, bool negative_only);

/* Inode Functions */
struct inode *get_inode(struct dentry *dentry);
void load_inode(struct dentry *dentry, unsigned int ino);
int create_file(struct inode *dir, struct dentry *dentry, int mode);
int create_dir(struct inode *dir, struct dentry *dentry, int mode);
int create_symlink(struct inode *dir, struct dentry *dentry,
                   const char *symname, int mode);
int check_perms(struct inode *inode, int access_mode);
void inode_release(struct kref *kref);
void stat_inode(struct inode *inode, struct kstat *kstat);
struct inode *icache_get(struct super_block *sb, unsigned int ino);
void icache_put(struct super_block *sb, struct inode *inode);
struct inode *icache_remove(struct super_block *sb, unsigned int ino);

/* File-ish functions */
ssize_t generic_file_read(struct file *file, char *buf, size_t count,
                          off_t *offset);
ssize_t generic_file_write(struct file *file, const char *buf, size_t count,
                           off_t *offset);
ssize_t generic_dir_read(struct file *file, char *u_buf, size_t count,
                         off_t *offset);
struct file *do_file_open(char *path, int flags, int mode);
int do_symlink(char *path, const char *symname, int mode);
int do_link(char *old_path, char *new_path);
int do_unlink(char *path);
int do_access(char *path, int mode);
int do_chmod(char *path, int mode);
int do_mkdir(char *path, int mode);
int do_rmdir(char *path);
struct file *dentry_open(struct dentry *dentry, int flags);
void file_release(struct kref *kref);

/* Process-related File management functions */
struct file *get_file_from_fd(struct files_struct *open_files, int fd);
struct file *put_file_from_fd(struct files_struct *open_files, int file_desc);
int insert_file(struct files_struct *open_files, struct file *file, int low_fd);
void close_all_files(struct files_struct *open_files, bool cloexec);
void clone_files(struct files_struct *src, struct files_struct *dst);
int do_chdir(struct fs_struct *fs_env, char *path);
char *do_getcwd(struct fs_struct *fs_env, char **kfree_this, size_t cwd_l);

/* Debugging */
int ls_dash_r(char *path);

#endif /* ROS_KERN_VFS_H */
