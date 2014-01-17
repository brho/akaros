/* misc utilities for plan9 */

#include <ns.h>
#include <string.h>

/* Copies n bytes from mem + offset into buf, similar to a read() call. */
int readmem(unsigned long offset, char *buf, unsigned long n,
            void *mem, size_t mem_len)
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
int readnum(unsigned long off, char *buf, unsigned long n, unsigned long val,
            size_t size)
{
	char tmp[64];
	size = MIN(sizeof(tmp), size);
	/* we really need the %* format. */
	size = snprintf(tmp, size, "%lu", val);
	/* size is now strlen, so the rest of this is just like readstr. */
	/* always include the \0 */
	return readmem(off, buf, n, tmp, size + 1);
}

int readstr(unsigned long offset, char *buf, unsigned long n, char *str)
{
	/* always include the \0 */
	return readmem(offset, buf, n, str, strlen(str) + 1);
}
