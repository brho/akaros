// Basic string routines.  Not hardware optimized, but not shabby.

#include <stdio.h>
#include <string.h>
#include <ros/memlayout.h>
#include <assert.h>

int strlen(const char *s)
{
	int n;

	for (n = 0; *s != '\0'; s++)
		n++;
	return n;
}

int strnlen(const char *s, size_t size)
{
	int n;

	for (n = 0; size > 0 && *s != '\0'; s++, size--)
		n++;
	return n;
}

/* zra: These aren't being used, and they are dangerous, so I'm rm'ing them
char *
strcpy(char *dst, const char *src)
{
	char *ret;

	ret = dst;
	while ((*dst++ = *src++) != '\0')
		;
	return ret;
}

char *
strcat(char *dst, const char *src)
{
	strcpy(dst+strlen(dst),src);
	return dst;
}
*/

char *strncpy(char *dst, const char *src, size_t size) {
	size_t i;
	char *ret;

	ret = dst;
	for (i = 0; i < size; i++) {
		*dst++ = *src;
		// If strlen(src) < size, null-pad 'dst' out to 'size' chars
		if (*src != '\0')
			src++;
	}
	return ret;
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
	if (size > 0) {
		while (--size > 0 && *src != '\0')
			*dst++ = *src++;
		*dst = '\0';
	}

	return strlen(src);
}

size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t rem;	/* Buffer space remaining after null in dst. */

	/* We must find the terminating NUL byte in dst, but abort the
	 * search if we go past 'size' bytes.  At the end of this loop,
	 * 'dst' will point to either the NUL byte in the original
	 * destination or to one byte beyond the end of the buffer.
	 *
	 * 'rem' will be the amount of 'size' remaining beyond the NUL byte;
	 * potentially zero. This implies that 'size - rem' is equal to the
	 * distance from the beginning of the destination buffer to 'dst'.
	 *
	 * The return value of strlcat is the sum of the length of the
	 * original destination buffer (size - rem) plus the size of the
	 * src string (the return value of strlcpy). */
	rem = size;
	while ((rem > 0) && (*dst != '\0')) {
		rem--;
		dst++;
	}

	return (size - rem) + strlcpy(dst, src, rem);
}

int strcmp(const char *p, const char *q)
{
	while (*p && *p == *q)
		p++, q++;
	return (int) ((unsigned char) *p - (unsigned char) *q);
}

int strncmp(const char *p, const char *q, size_t n)
{
	while (n > 0 && *p && *p == *q)
		n--, p++, q++;
	if (n == 0)
		return 0;
	else
		return (int) ((unsigned char) *p - (unsigned char) *q);
}

// Return a pointer to the first occurrence of 'c' in 's',
// or a null pointer if the string has no 'c'.
char *strchr(const char *s, char c)
{
	for (; *s; s++)
		if (*s == c)
			return (char *) s;
	return 0;
}

// Return a pointer to the last occurrence of 'c' in 's',
// or a null pointer if the string has no 'c'.
char *strrchr(const char *s, char c)
{
	char *lastc = NULL;
	for (; *s; s++)
		if (*s == c){
			lastc = (char*)s;
		}
	return lastc;
}

void *memchr(const void *mem, int chr, int len)
{
	char *s = (char*) mem;

	for (int i = 0; i < len; i++)
		if (s[i] == (char) chr)
			return s + i;
	return NULL;
}

// Return a pointer to the first occurrence of 'c' in 's',
// or a pointer to the string-ending null character if the string has no 'c'.
char *strfind(const char *s, char c)
{
	for (; *s; s++)
		if (*s == c)
			break;
	return (char *) s;
}

// memset aligned words.
static inline void *memsetw(long* _v, long c, size_t n)
{
	long *start, *end, *v;

	start = _v;
	end = _v + n/sizeof(long);
	v = _v;
	c = c & 0xff;
	c = c | c<<8;
	c = c | c<<16;
	#if NUM_ADDR_BITS == 64
	c = c | c<<32;
	#elif NUM_ADDR_BITS != 32
	# error
	#endif

	while(v < end - (8-1))
	{
		v[3] = v[2] = v[1] = v[0] = c;
		v += 4;
		v[3] = v[2] = v[1] = v[0] = c;
		v += 4;
	}

	while(v < end)
	  *v++ = c;

	return start;
}

// copy aligned words.
// unroll 9 ways to get multiple misses in flight
#define memcpyw(type, _dst, _src, n) \
  do { \
	type* restrict src = (type*)(_src); \
	type* restrict dst = (type*)(_dst); \
	type* srcend = src + (n)/sizeof(type); \
	type* dstend = dst + (n)/sizeof(type); \
	while (dst < dstend - (9-1)) { \
		dst[0] = src[0]; \
		dst[1] = src[1]; \
		dst[2] = src[2]; \
		dst[3] = src[3]; \
		dst[4] = src[4]; \
		dst[5] = src[5]; \
		dst[6] = src[6]; \
		dst[7] = src[7]; \
		dst[8] = src[8]; \
		src += 9; \
		dst += 9; \
	} \
	while(dst < dstend) \
	  *dst++ = *src++; \
  } while(0)

void *memset(void *v, int c, size_t _n)
{
	char *p;
	size_t n0;
	size_t n = _n;

	if (n == 0) return NULL; // zra: complain here?

	p = v;

	while (n > 0 && ((uintptr_t)p & (sizeof(long)-1))) {
		*p++ = c;
		n--;
	}

	if (n >= sizeof(long)) {
		n0 = n / sizeof(long) * sizeof(long);
		memsetw((long*)p, c, n0);
		n -= n0;
		p += n0;
	}

	while (n > 0) {
		*p++ = c;
		n--;
	}

	return v;
}

void *memcpy(void* dst, const void* src, size_t _n)
{
	const char* s;
	char* d;
	size_t n0 = 0;
	size_t n = _n;
	int align = sizeof(long)-1;

	s = src;
	d = dst;

	if ((((uintptr_t)s | (uintptr_t)d) & (sizeof(long)-1)) == 0) {
		n0 = n / sizeof(long) * sizeof(long);
		memcpyw(long, d, s, n0);
	} else if ((((uintptr_t)s | (uintptr_t)d) & (sizeof(int)-1)) == 0) {
		n0 = n / sizeof(int) * sizeof(int);
		memcpyw(int, d, s, n0);
	} else if ((((uintptr_t)s | (uintptr_t)d) & (sizeof(short)-1)) == 0) {
		n0 = n / sizeof(short) * sizeof(short);
		memcpyw(short, d, s, n0);
	}

	n -= n0;
	s += n0;
	d += n0;

	while (n-- > 0)
		*d++ = *s++;

	return dst;
}

void *memmove(void *dst, const void *src, size_t _n)
{
#ifdef CONFIG_X86
	bcopy(src, dst, _n);
	return dst;
#else
	const char *s;
	char *d;
	size_t n = _n;

	s = src;
	d = dst;
	if (s < d && s + n > d) {
		s += n;
		d += n;
		while (n-- > 0)
			*--d = *--s;
	} else
		while (n-- > 0)
			*d++ = *s++;

	return dst;
#endif
}

int memcmp(const void *v1, const void *v2, size_t n)
{
	const uint8_t *s1 = (const uint8_t *) v1;
	const uint8_t *s2 = (const uint8_t *) v2;

	while (n-- > 0) {
		if (*s1 != *s2)
			return (int) *s1 - (int) *s2;
		s1++, s2++;
	}

	return 0;
}

void *memfind(const void *_s, int c, size_t n)
{
	const void *ends = (const char *) _s + n;
	const void *s = _s;
	for (; s < ends; s++)
		if (*(const unsigned char *) s == (unsigned char) c)
			break;
	return (void *)s;
}

long strtol(const char *s, char **endptr, int base)
{
	int neg = 0;
	long val = 0;

	// gobble initial whitespace
	while (*s == ' ' || *s == '\t')
		s++;

	// plus/minus sign
	if (*s == '+')
		s++;
	else if (*s == '-')
		s++, neg = 1;

	// hex or octal base prefix
	if ((base == 0 || base == 16) && (s[0] == '0' && s[1] == 'x'))
		s += 2, base = 16;
	else if (base == 0 && s[0] == '0')
		s++, base = 8;
	else if (base == 0)
		base = 10;

	// digits
	while (1) {
		int dig;

		if (*s >= '0' && *s <= '9')
			dig = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			dig = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			dig = *s - 'A' + 10;
		else
			break;
		if (dig >= base)
			break;
		s++, val = (val * base) + dig;
		// we don't properly detect overflow!
	}

	if (endptr)
		*endptr = (char *) s;
	return (neg ? -val : val);
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
	int neg = 0;
	unsigned long val = 0;

	// gobble initial whitespace
	while (*s == ' ' || *s == '\t')
		s++;

	// plus/minus sign
	if (*s == '+')
		s++;
	else if (*s == '-')
		s++, neg = 1;

	// hex or octal base prefix
	if ((base == 0 || base == 16) && (s[0] == '0' && s[1] == 'x'))
		s += 2, base = 16;
	else if (base == 0 && s[0] == '0')
		s++, base = 8;
	else if (base == 0)
		base = 10;

	// digits
	while (1) {
		int dig;

		if (*s >= '0' && *s <= '9')
			dig = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			dig = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			dig = *s - 'A' + 10;
		else
			break;
		if (dig >= base)
			break;
		s++, val = (val * base) + dig;
		// we don't properly detect overflow!
	}

	if (endptr)
		*endptr = (char *) s;
	return (neg ? -val : val);
}

int atoi(const char *s)
{
	if (!s)
		return 0;
	if (s[0] == '0' && s[1] == 'x')
		warn("atoi() used on a hex string!");
	// no overflow detection
	return (int)strtol(s,NULL,10);
}

int sigchecksum(void *address, int length)
{
	uint8_t *p, sum;

	sum = 0;
	for (p = address; length-- > 0; p++)
		sum += *p;

	return sum;
}

void *sigscan(uint8_t *address, int length, char *signature)
{
	uint8_t *e, *p;
	int siglength;

	e = address + length;
	siglength = strlen(signature);
	for (p = address; p + siglength < e; p += 16) {
		if (memcmp(p, signature, siglength))
			continue;
		return p;
	}

	return NULL;
}
