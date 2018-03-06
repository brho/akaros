#include <cpio.h>
#include <kmalloc.h>
#include <stdio.h>
#include <string.h>

void parse_cpio_entries(void *cpio_b, size_t cpio_sz,
                        int (*cb)(struct cpio_bin_hdr*, void *), void *cb_arg)
{
	struct cpio_newc_header *c_hdr;
	char buf[9] = {0};	/* temp space for strol conversions */
	size_t namesize = 0;
	int offset = 0;		/* offset in the cpio archive */
	struct cpio_bin_hdr *c_bhdr = kzmalloc(sizeof(*c_bhdr), MEM_WAIT);

	/* read all files and paths */
	for (;;) {
		c_hdr = (struct cpio_newc_header*)(cpio_b + offset);
		offset += sizeof(*c_hdr);
		if (offset > cpio_sz) {
			printk("CPIO offset %d beyond size %d, aborting.\n", offset,
			       cpio_sz);
			return;
		}
		if (strncmp(c_hdr->c_magic, "070701", 6)) {
			printk("Invalid magic number in CPIO header, aborting.\n");
			return;
		}
		c_bhdr->c_filename = (char*)c_hdr + sizeof(*c_hdr);
		namesize = cpio_strntol(buf, c_hdr->c_namesize, 8);
		printd("Namesize: %d\n", namesize);
		if (!strcmp(c_bhdr->c_filename, "TRAILER!!!"))
			break;
		c_bhdr->c_ino = cpio_strntol(buf, c_hdr->c_ino, 8);
		c_bhdr->c_mode = (int)cpio_strntol(buf, c_hdr->c_mode, 8);
		c_bhdr->c_uid = cpio_strntol(buf, c_hdr->c_uid, 8);
		c_bhdr->c_gid = cpio_strntol(buf, c_hdr->c_gid, 8);
		c_bhdr->c_nlink = (unsigned int)cpio_strntol(buf, c_hdr->c_nlink, 8);
		c_bhdr->c_mtime = cpio_strntol(buf, c_hdr->c_mtime, 8);
		c_bhdr->c_filesize = cpio_strntol(buf, c_hdr->c_filesize, 8);
		c_bhdr->c_dev_maj = cpio_strntol(buf, c_hdr->c_dev_maj, 8);
		c_bhdr->c_dev_min = cpio_strntol(buf, c_hdr->c_dev_min, 8);
		c_bhdr->c_rdev_maj = cpio_strntol(buf, c_hdr->c_rdev_maj, 8);
		c_bhdr->c_rdev_min = cpio_strntol(buf, c_hdr->c_rdev_min, 8);
		printd("File: %s: %d Bytes\n", c_bhdr->c_filename, c_bhdr->c_filesize);
		offset += namesize;
		/* header + name will be padded out to 4-byte alignment */
		offset = ROUNDUP(offset, 4);
		c_bhdr->c_filestart = cpio_b + offset;
		offset += c_bhdr->c_filesize;
		offset = ROUNDUP(offset, 4);
		if (offset > cpio_sz) {
			printk("CPIO offset %d beyond size %d, aborting.\n", offset,
			       cpio_sz);
			return;
		}
		if (cb(c_bhdr, cb_arg)) {
			printk("Failed to handle CPIO callback, aborting!\n");
			break;
		}
		//printk("offset is %d bytes\n", offset);
	}
	kfree(c_bhdr);
}
