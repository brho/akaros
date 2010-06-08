#ifndef ROS_KERN_CPIO_H
#define ROS_KERN_CPIO_H

#define CPIO_NEW_ASCII 070701
#define CPIO_CRC_ASCII 070702

struct cpio_newc_header
{
	char c_magic[6];
	char c_ino[8];
	char c_mode[8];
	char c_uid[8];
	char c_gid[8];
	char c_nlink[8];
	char c_mtime[8];
	char c_filesize[8];	/* must be 0 for FIFOs and directories */
	char c_dev_maj[8];
	char c_dev_min[8];
	char c_rdev_maj[8];	/* only valid for chr and blk special files */
	char c_rdev_min[8];	/* only valid for chr and blk special files */
	char c_namesize[8];	/* count includes terminating NUL in pathname */
	char c_chksum[8];	/* for CRC format the sum of all the bytes in the file*/
};

/* Header passed around when initing a FS based on a CPIO archive */
struct cpio_bin_hdr
{
	unsigned long	c_ino;	/* FYI: we ignore this */
	int				c_mode;
	uid_t			c_uid;
	gid_t			c_gid;
	unsigned int	c_nlink; /* not sure how this makes CPIO-sense, ignoring */
	unsigned long	c_mtime;
	size_t			c_filesize;
	unsigned long	c_dev_maj;
	unsigned long	c_dev_min;
	unsigned long	c_rdev_maj;
	unsigned long	c_rdev_min;
	char			*c_filename;
	void			*c_filestart;
};

void parse_cpio_entries(struct super_block *sb, void *cpio_b);

/* Helper function: converts src non-null-term string's n bytes from base 16 to
 * a long, using buf as space.  Make sure buf is n + 1. */
static inline long cpio_strntol(char *buf, char *src, size_t n)
{
	memcpy(buf, src, n);
	buf[n] = '\0';
	return strtol(buf, 0, 16);
}

#endif	/* ROS_KERN_CPIO_H */
