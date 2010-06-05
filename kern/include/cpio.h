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

void print_cpio_entries(void *cpio_b);

#endif	/* ROS_KERN_CPIO_H */
