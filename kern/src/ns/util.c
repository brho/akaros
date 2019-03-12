/* misc utilities for plan9 */

#include <ns.h>
#include <string.h>
#include <err.h>
#include <syscall.h>
#include <smp.h>

/* Copies n bytes from mem + offset into buf, similar to a read() call. */
int readmem(unsigned long offset, char *buf, unsigned long n,
			const void *mem, size_t mem_len)
{
	if (offset >= mem_len)
		return 0;
	if (offset + n > mem_len)
		n = mem_len - offset;
	memmove(buf, mem + offset, n);
	return n;
}

/* Read a num/string to user mode, accounting for offset.  Not a huge fan of the
 * 'size' parameter (the old plan9 users just picked NUMSIZE (12), though they
 * seem to want to limit it).  */
static int __readnum(unsigned long off, char *buf, unsigned long n,
                     unsigned long val, size_t size, const char *fmt)
{
	char tmp[64];

	size = MIN(sizeof(tmp), size);
	/* we really need the %* format. */
	size = snprintf(tmp, size, fmt, val);
	/* size is now strlen, so the rest of this is just like readstr. */
	/* always include the \0 */
	return readmem(off, buf, n, tmp, size + 1);
}

int readnum(unsigned long off, char *buf, unsigned long n, unsigned long val,
            size_t size)
{
	return __readnum(off, buf, n, val, size, "%lu");
}

int readnum_hex(unsigned long off, char *buf, unsigned long n,
                unsigned long val, size_t size)
{
	return __readnum(off, buf, n, val, size, "0x%lx");
}

int readstr(unsigned long offset, char *buf, unsigned long n, const char *str)
{
	return readmem(offset, buf, n, str, strlen(str));
}

/* Helper: extracts a long from a user buffer (in text). */
unsigned long strtoul_from_ubuf(void *ubuf, size_t count, int base)
{
	char num64[NUMSIZE64];

	/* want to give strtoul a null-terminated buf (can't handle random
	 * user strings) */
	if (count > sizeof(num64)) {
		set_errno(EINVAL);
		error(EFAIL, "attempted to write %d chars, max %d", count,
			  sizeof(num64));
	}
	memcpy(num64, ubuf, count);
	num64[count] = 0;	/* enforce trailing 0 */
	return strtoul(num64, 0, base);
}

/* Converts open mode flags, e.g. O_RDWR, to a rwx------ value, e.g. S_IRUSR */
int omode_to_rwx(int open_flags)
{
	static int rwx_opts[] = { [O_RDWR | O_EXEC] = 0700,
	                          [O_RDWR] = 0600,
	                          [O_READ | O_EXEC] = 0500,
	                          [O_READ] = 0400,
	                          [O_WRITE | O_EXEC] = 0300,
	                          [O_WRITE] = 0200,
	                          [O_EXEC] = 0100 };

	return rwx_opts[open_flags & O_ACCMODE];
}

/* Converts open mode flags related to permissions, e.g. O_RDWR, to 9p.  It's a
 * bit ugly, since 9p (according to http://man.cat-v.org/plan_9/5/open) seems to
 * require that O_EXEC is mutually exclusive with the others.  If someone on
 * Akaros wants EXEC, we'll just substitute READ. */
int omode_to_9p_accmode(int open_flags)
{
	static int acc_opts[] = { [O_RDWR | O_EXEC] = 2,
	                          [O_WRITE | O_EXEC] = 2,
	                          [O_READ | O_EXEC] = 0,
	                          [O_EXEC] = 0,
	                          [O_RDWR] = 2,
	                          [O_WRITE] = 1,
	                          [O_READ] = 0,
	                          [0] = 0 /* we can't express no permissions */
	                          };

	return acc_opts[open_flags & O_ACCMODE];
}

int access_bits_to_omode(int access_bits)
{
	int omode = 0;

	if (R_OK)
		omode |= O_READ;
	if (W_OK)
		omode |= O_WRITE;
	if (X_OK)
		omode |= O_EXEC;
	return omode;
}

/* TODO: This assumes UID isn't concurrently changed */
bool caller_is_username(char *uid)
{
	struct username *current_user = current ? &current->user : &eve;

	return strcmp(current_user->name, uid) == 0;
}

/* Checks if current->user has permissions for omode access on something with
 * {owner_fileuid, perm} */
bool caller_has_perms(char *fileuid, uint32_t perm, int omode)
{
	int rwx;

	perm &= S_PMASK;	/* technically unnecessary; good for clarity */
	/* select user, group, or other from the traditional rwxrwxrwx, shifting
	 * into the upper-most position */
	if (caller_is_username(fileuid))
		perm <<= 0;
	else if (iseve())
		perm <<= 3;
	else
		perm <<= 6;
	/* translate omode into things like S_IRUSR (just one set of rwx------).
	 * Plan 9 originally only returned 0400 0200 0600 and 0100 here; it
	 * didn't seem to handle O_EXEC being mixed readable or writable. */
	rwx = omode_to_rwx(omode);
	return (rwx & perm) == rwx;
}

bool caller_has_dir_perms(struct dir *dir, int omode)
{
	return caller_has_perms(dir->uid, READ_ONCE(dir->mode), omode);
}

void dir_perm_check(struct dir *dir, int omode)
{
	devpermcheck(dir->uid, READ_ONCE(dir->mode) & S_PMASK, omode);
}
