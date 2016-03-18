#define _GNU_SOURCE
#include <dlfcn.h>
#include <parlib/vcore.h>

#include <string.h>

#include <stdio.h>

static void *parlib_memmove(void *dest, const void *src, size_t n)
{
	const char *s;
	char *d;

	s = src;
	d = dest;
	if (s < d && s + n > d) {
		s += n;
		d += n;
		while (n-- > 0)
			*--d = *--s;
	} else
		while (n-- > 0)
			*d++ = *s++;

	return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
	static void *(*glibc_memmove)(void *, const void *, size_t) = 0;

	if (in_vcore_context())
		return parlib_memmove(dest, src, n);
	if (!glibc_memmove)
		glibc_memmove = dlsym(RTLD_NEXT, "memmove");
	return glibc_memmove(dest, src, n);
}
