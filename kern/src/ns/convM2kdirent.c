
#include <vfs.h>
#include <kfs.h>
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
/* akaros does not (yet) pass as much info as plan 9 does,
 * and it still has stuff I'm not happy about like an inode number.
 */
#if 0
struct kdirent {
	__ino64_t d_ino;			/* inod
								   e number */
	__off64_t d_off;			/* offs
								   et to the next dirent */
	unsigned short d_reclen;	/* length of th
								   is record */
	unsigned char d_type;
	char d_name[MAX_FILENAME_SZ + 1];	/* filename */
} __attribute__ ((aligned(8)));

#endif

unsigned int convM2kdirent(uint8_t * buf, unsigned int nbuf, struct kdirent *kd,
						   char *strs)
{
	uint8_t *p, *ebuf;
	char *sv[4];
	int i, ns;
	uint32_t junk;
	printd("%s >>>>>>>>>nbuf %d STATFIXLEN %d\n", __func__, nbuf, STATFIXLEN);
	if (nbuf < STATFIXLEN)
		return 0;

	p = buf;
	ebuf = buf + nbuf;

	p += BIT16SZ;	/* ignore size */
	kd->d_type = GBIT16(p);
	p += BIT16SZ;
	junk = GBIT32(p);
	p += BIT32SZ;
	junk = GBIT8(p);
	p += BIT8SZ;
	junk = GBIT32(p);
	p += BIT32SZ;
	kd->d_ino = GBIT64(p);
	p += BIT64SZ;
	junk /* mode */  = GBIT32(p);
	p += BIT32SZ;
	junk /*d->atime */  = GBIT32(p);
	p += BIT32SZ;
	junk /*d->mtime */  = GBIT32(p);
	p += BIT32SZ;
	junk /*d->length */  = GBIT64(p);
	p += BIT64SZ;

	/* for now, uids in akaros are ints. Does not
	 * matter; kdirents are limited in what they tell you.
	 * get the name, ignore the rest. Maybe we can
	 * fix this later.
	 */
	for (i = 0; i < 4; i++) {
		if (p + BIT16SZ > ebuf)
			return 0;
		ns = GBIT16(p);
		p += BIT16SZ;
		if (p + ns > ebuf)
			return 0;
		if (strs) {
			sv[i] = strs;
			memmove(strs, p, ns);
			strs += ns;
			*strs++ = '\0';
		}
		if (i == 0) {
			kd->d_reclen = ns;
			printd("memmove %p %p %d\n", kd->d_name, p, ns);
			memmove(kd->d_name, p, ns);
			kd->d_name[ns] = 0;
		}
		p += ns;
	}

	printd("%s returns %d %s\n", __func__, p - buf, kd->d_name);
	return p - buf;
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
	uint8_t *p, *ebuf;
	char *sv[4];
	int i, ns;
	uint32_t junk;

	if (nbuf < STATFIXLEN)
		return 0;

	p = buf;
	ebuf = buf + nbuf;

	p += BIT16SZ;	/* ignore size */
	junk /*kd->d_type */  = GBIT16(p);
	p += BIT16SZ;
	ks->st_rdev = ks->st_dev = GBIT32(p);
	p += BIT32SZ;
	junk /*qid.type */  = GBIT8(p);
	p += BIT8SZ;
	junk /*qid.vers */  = GBIT32(p);
	p += BIT32SZ;
	ks->st_ino = GBIT64(p);
	p += BIT64SZ;
	ks->st_mode = mode_9ns_to_posix(GBIT32(p));
	p += BIT32SZ;
	ks->st_atim.tv_sec = GBIT32(p);
	p += BIT32SZ;
	ks->st_mtim.tv_sec = GBIT32(p);
	p += BIT32SZ;
	ks->st_size = GBIT64(p);
	p += BIT64SZ;
	ks->st_blksize = 512;
	ks->st_blocks = ROUNDUP(ks->st_size, ks->st_blksize) / ks->st_blksize;

	ks->st_nlink = 2;	// links make no sense any more.
	ks->st_uid = ks->st_gid = 0;

	return p - buf;
}
