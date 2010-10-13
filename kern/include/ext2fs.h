/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * The ROS port for Ext2 FS.  Thanks to Dave Poirier for maintaining this great
 * documentation: http://www.nongnu.org/ext2-doc/ext2.html and the ext group
 * that wrote the FS in the first place.
 *
 * Note all of ext2's disk structures are little-endian. */

#ifndef ROS_KERN_EXT2FS_H
#define ROS_KERN_EXT2FS_H

#include <ros/common.h>
#include <vfs.h>
#include <endian.h>

#define EXT2_SUPER_MAGIC		0xef53

/* State settings */
#define EXT2_VALID_FS			1		/* Cleanly unmounted */
#define EXT2_ERROR_FS			2		/* Currently mounted / not unmounted */

/* s_error options */
#define EXT2_ERRORS_CONTINUE	1		/* continue on error */
#define EXT2_ERRORS_RO			2		/* remount read-only */
#define EXT2_ERRORS_PANIC		3		/* panic on error */

/* Creator OS options */
#define EXT2_OS_LINUX			0
#define EXT2_OS_HURD			1
#define EXT2_OS_MASIX			2
#define EXT2_OS_FREEBSD			3
#define EXT2_OS_LITES			4
#define EXT2_OS_ROS				666		/* got dibs on the mark of the beast */

/* Revision Levels */
#define EXT2_GOOD_OLD_REV		0		/* Revision 0 */
#define EXT2_DYNAMIC_REV		1		/* Revision 1, extra crazies, etc */

/* FS Compatibile Features.  We can support them or now, without risk of
 * damaging meta-data. */
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC	0x0001	/* block prealloc */
#define EXT2_FEATURE_COMPAT_MAGIC_INODES	0x0002
#define EXT2_FEATURE_COMPAT_HAS_JOURNAL		0x0004	/* ext3/4 journal */
#define EXT2_FEATURE_COMPAT_EXT_ATTR		0x0008	/* extended inode attr */
#define EXT2_FEATURE_COMPAT_RESIZE_INO		0x0010	/* non-standard ino size */
#define EXT2_FEATURE_COMPAT_DIR_INDEX		0x0020	/* h-tree dir indexing */

/* FS Incompatibile Features.  We should refuse to mount if we don't support
 * any of these. */
#define EXT2_FEATURE_INCOMPAT_COMPRESSION	0x0001	/* disk compression */
#define EXT2_FEATURE_INCOMPAT_FILETYPE		0x0002
#define EXT2_FEATURE_INCOMPAT_RECOVER		0x0004
#define EXT2_FEATURE_INCOMPAT_JOURNAL_DEV	0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG		0x0010

/* FS read-only features: We should mount read-only if we don't support these */
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER	0x0001	/* sparse superblock */
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE	0x0002	/* 64-bit filesize */
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR	0x0004	/* binary tree sorted dir */

/* Compression types (s_algo_bitmap) */
#define EXT2_LZV1_ALG			0x0001
#define EXT2_LZRW3A_ALG			0x0002
#define EXT2_GZIP_ALG			0x0004
#define EXT2_BZIP2_ALG			0x0008
#define EXT2_LZO_ALG			0x0010

/* Defined Reserved inodes */
#define EXT2_BAD_INO			1		/* bad blocks inode */
#define EXT2_ROOT_INO			2		/* root directory inode */
#define EXT2_ACL_IDX_INO		3		/* ACL index (deprecated?) */
#define EXT2_ACL_DATA_INO		4		/* ACL data (deprecated?) */
#define EXT2_BOOT_LOADER_INO	5		/* boot loader inode */
#define EXT2_UNDEL_DIR_INO		6		/* undelete directory inode */

/* Inode/File access mode and type (i_mode).  Note how they use hex here, but
the crap we keep around for glibc/posix is (probably still) in octal. */
#define EXT2_S_IFSOCK			0xC000	/* socket */
#define EXT2_S_IFLNK			0xA000	/* symbolic link */
#define EXT2_S_IFREG			0x8000	/* regular file */
#define EXT2_S_IFBLK			0x6000	/* block device */
#define EXT2_S_IFDIR			0x4000	/* directory */
#define EXT2_S_IFCHR			0x2000	/* character device */
#define EXT2_S_IFIFO			0x1000	/* fifo */
#define EXT2_S_ISUID			0x0800	/* set process user id */
#define EXT2_S_ISGID			0x0400	/* set process group id */
#define EXT2_S_ISVTX			0x0200	/* sticky bit */
#define EXT2_S_IRUSR			0x0100	/* user read */
#define EXT2_S_IWUSR			0x0080	/* user write */
#define EXT2_S_IXUSR			0x0040	/* user execute */
#define EXT2_S_IRGRP			0x0020	/* group read */
#define EXT2_S_IWGRP			0x0010	/* group write */
#define EXT2_S_IXGRP			0x0008	/* group execute */
#define EXT2_S_IROTH			0x0004	/* others read */
#define EXT2_S_IWOTH			0x0002	/* others write */
#define EXT2_S_IXOTH			0x0001	/* others execute */

/* Inode flags, for how to access data for an inode/file/object */
#define EXT2_SECRM_FL			0x00000001	/* secure deletion */
#define EXT2_UNRM_FL			0x00000002	/* record for undelete */
#define EXT2_COMPR_FL			0x00000004	/* compressed file */
#define EXT2_SYNC_FL			0x00000008	/* synchronous updates */
#define EXT2_IMMUTABLE_FL		0x00000010	/* immutable file */
#define EXT2_APPEND_FL			0x00000020	/* append only */
#define EXT2_NODUMP_FL			0x00000040	/* do not dump/delete file */
#define EXT2_NOATIME_FL			0x00000080	/* do not update i_atime */
/* Compression Flags */
#define EXT2_DIRTY_FL			0x00000100	/* dirty (modified) */
#define EXT2_COMPRBLK_FL		0x00000200	/* compressed blocks */
#define EXT2_NOCOMPR_FL			0x00000400	/* access raw compressed data */
#define EXT2_ECOMPR_FL			0x00000800	/* compression error */
/* End of compression flags */
#define EXT2_BTREE_FL			0x00010000	/* b-tree format directory */
#define EXT2_INDEX_FL			0x00010000	/* hash indexed directory */
#define EXT2_IMAGIC_FL			0x00020000	/* AFS directory */
#define EXT3_JOURNAL_DATA_FL	0x00040000	/* journal file data */
#define EXT2_RESERVED_FL		0x80000000	/* reserved for ext2 library */

/* Directory entry file types */
#define EXT2_FT_UNKNOWN			0	/* unknown file type */
#define EXT2_FT_REG_FILE		1	/* regular file */
#define EXT2_FT_DIR				2	/* directory */
#define EXT2_FT_CHRDEV			3	/* character device */
#define EXT2_FT_BLKDEV			4	/* block device */
#define EXT2_FT_FIFO			5	/* FIFO / buffer file */
#define EXT2_FT_SOCK			6	/* socket */
#define EXT2_FT_SYMLINK			7	/* symbolic link */

struct ext2_sb {
	uint32_t					s_inodes_cnt;		/* total, both used/free */
	uint32_t					s_blocks_cnt;		/* used/free/reserved */
	uint32_t					s_rblocks_cnt;		/* reserved for su/brho */
	uint32_t					s_free_blocks_cnt;	/* free, incl reserved */
	uint32_t					s_free_inodes_cnt;
	uint32_t					s_first_data_block;	/* id of block holding sb */
	uint32_t					s_log_block_size;
	uint32_t					s_log_frag_size;	/* no real frag support */
	uint32_t					s_blocks_per_group;
	uint32_t					s_frags_per_group;
	uint32_t					s_inodes_per_group;
	uint32_t					s_mtime;			/* last mount time */
	uint32_t					s_wtime;			/* last write to the FS */
	uint16_t					s_mnt_cnt;			/* mounts since fsck */
	uint16_t					s_max_mnt_cnt;		/* mounts between fscks */
	uint16_t					s_magic;
	uint16_t					s_state;			/* mount state */
	uint16_t					s_errors;			/* what to do on error */
	uint16_t					s_minor_rev_level;
	uint32_t					s_lastcheck;		/* last fsck */
	uint32_t					s_checkinterval;	/* max time btw fscks */
	uint32_t					s_creator_os;
	uint32_t					s_rev_level;
	uint16_t					s_def_resuid;		/* uid for reserved blocks*/
	uint16_t					s_def_resgid;		/* gid for reserved blocks*/
/* Next chunk, EXT2_DYNAMIC_REV specific */
	uint32_t					s_first_ino;		/* first usable for F_REG */
	uint16_t					s_inode_size;
	uint16_t					s_block_group_nr;	/* BG holding *this* SB */
	uint32_t					s_feature_compat;
	uint32_t					s_feature_incompat;
	uint32_t					s_feature_ro_compat;
	uint64_t					s_uuid[2];			/* volume id */
	char						s_volume_name[16];	/* null terminated */
	char						s_last_mounted[64];	/* dir path of mount */
	uint32_t					s_algo_bitmap;		/* compression type */
/* Next chunk, Performance Hints */
	uint8_t						s_prealloc_blocks;	/* when making F_REG */
	uint8_t						s_prealloc_dir_blocks;	/* when making F_DIR */
	uint16_t					s_padding1;
/* Next chunk, Journaling Support */
	uint8_t						s_journal_uuid[16];
	uint32_t					s_journal_inum;		/* ino of the journal file*/
	uint32_t					s_journal_dev;		/* device num of journal */
	uint32_t					s_last_orphan;		/* first in list to delete*/
/* Next chunk, Directory Indexing Support */
	uint32_t					s_hash_seed[4];		/* for directory indexing */
	uint8_t						s_def_hash_version;	/* default */
	uint8_t						s_padding2;
	uint16_t					s_padding3;
/* Next chunk, Other options */
	uint32_t					s_default_mount_opts;
	uint32_t					s_first_meta_bg;	/* BG id of first meta */
	uint8_t						s_reserved[760];
};

/* All block ids are absolute (not relative to the BG). */
struct ext2_block_group {
	uint32_t					bg_block_bitmap;	/* block id of bitmap */
	uint32_t					bg_inode_bitmap;	/* block id of bitmap */
	uint32_t					bg_inode_table;		/* id of first block */
	uint16_t					bg_free_blocks_cnt;
	uint16_t					bg_free_inodes_cnt;
	uint16_t					bg_used_dirs_cnt;	/* inodes alloc to dirs */
	uint16_t					bg_padding;
	uint8_t						bg_reserved[12];
};

/* Random note: expect some number of the initial inodes to be reserved (11 in
 * rev 0), and later versions just start from s_first_ino.
 *
 * Only regular files use the full 32bits of size.  Rev 0 uses a signed i_size.
 *
 * For the i_blocks, the first 12 are block numbers for direct blocks.  The
 * 13th entry ([12]) is the block number for the first indirect block.  The
 * 14th entry is the number for a doubly-indirect block, and the 15th is a
 * triply indirect block. Having a value of 0 in the array terminates it, with
 * no further blocks. (not clear how that works with holes)
 *
 * For the osd2[12], these should be unused for ext2, and they should be used
 * differently for ext4.
 *
 * Giant warning: the inode numbers start at 1 in the system, not 0! */
struct ext2_inode {
	uint16_t					i_mode;				/* file type and mode */
	uint16_t					i_uid;
	uint32_t					i_size;				/* lower 32bits of size */
	uint32_t					i_atime;
	uint32_t					i_ctime;
	uint32_t					i_mtime;
	uint32_t					i_dtime;			/* delete time */
	uint16_t					i_gid;
	uint16_t					i_links_cnt;		/* fs_ino->i_nlink */
	uint32_t					i_blocks;			/* num blocks reserved */
	uint32_t					i_flags;			/* how to access data */
	uint32_t					i_osd1;				/* OS dependent */
	uint32_t					i_block[15];		/* list of blocks reserved*/
	uint32_t					i_generation;		/* used by NFS */
	uint32_t					i_file_acl;			/* block num hold ext attr*/
	uint32_t					i_dir_acl;			/* upper 32bits of size */
	uint32_t					i_faddr;			/* fragment, obsolete. */
	uint8_t						i_osd2[12];			/* OS dependent */
};

/* a dir_inode of 0 means an unused entry.  reclen will go to the end of the
 * data block when it is the last entry.  These are 4-byte aligned on disk. */
struct ext2_dirent {
	uint32_t					dir_inode;			/* inode */
	uint16_t					dir_reclen;			/* len, including padding */
	uint8_t						dir_namelen;		/* len of dir_name w/o \0 */
	uint8_t						dir_filetype;
	uint8_t						dir_name[256];		/* might be < 255 on disc */
};

/* Every FS must extern it's type, and be included in vfs_init() */
extern struct fs_type ext2_fs_type;

/* This hangs off the VFS's SB, and tracks in-memory copies of the disc SB and
 * the block group descriptor table.  For now, s_dirty (VFS) will track the
 * dirtiness of all things hanging off the sb.  Both of the objects contained
 * are kmalloc()d, as is this struct. */
struct ext2_sb_info {
	struct ext2_sb				*e2sb;
	struct ext2_block_group		*e2bg;
	unsigned int				nr_bgs;
};

/* Inode in-memory data.  This stuff is in cpu-native endianness.  If we start
 * using the data in the actual inode and in the buffer cache, change
 * ext2_my_bh() and its two callers.  Assume this data is dirty. */
struct ext2_i_info {
	uint32_t					i_block[15];		/* list of blocks reserved*/
};
#endif /* ROS_KERN_EXT2FS_H */
