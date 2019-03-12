#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <ros/syscall.h>

/* potentially a 32 bit seek (off_t is a long) */
off_t __libc_lseek (int fd, off_t offset, int whence)
{
	loff_t retoff = 0;
	off_t hi = 0;
	off_t lo = 0;
	off_t ret;

	if (fd < 0) {
		__set_errno (EBADF);
		return -1;
	}
	switch (whence) {
		case SEEK_SET:
		case SEEK_CUR:
		case SEEK_END:
			break;
		default:
			__set_errno (EINVAL);
			return -1;
	}
	/* get the high and low part, regardless of whether offset was already
	 * 64 bits or not (casting to avoid warnings) */
	hi = (loff_t)offset >> 32;
	lo = offset & 0xffffffff;
	ret = ros_syscall(SYS_llseek, fd, hi, lo, &retoff, whence, 0);
	if (ret) {
		assert(ret == -1);	/* catch odd bugs */
		return ret;
	}
	/* Get the lower 32 or 64 bits, depending on the length of long */
	ret = retoff & (unsigned long)(-1);
	return ret;
}
weak_alias (__libc_lseek, __lseek)
libc_hidden_def (__lseek)
weak_alias (__libc_lseek, lseek)
