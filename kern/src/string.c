// Basic string routines.  Not hardware optimized, but not shabby.

#ifdef __SHARC__
#pragma nosharc
#endif

#include <string.h>
#include <ros/memlayout.h>
#include <assert.h>

int
strlen(const char *s)
{
	int n;

	for (n = 0; *s != '\0'; s++)
		n++;
	return n;
}

int
strnlen(const char *s, size_t size)
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

char *
strncpy(char *dst, const char *src, size_t size) {
	size_t i;
	char *ret;

	ret = dst;
	for (i = 0; i < size; i++) {
		// TODO: ivy bitches about this
		*dst++ = *src;
		// If strlen(src) < size, null-pad 'dst' out to 'size' chars
		if (*src != '\0')
			src++;
	}
	return ret;
}

size_t
strlcpy(char *dst, const char *src, size_t size)
{
	char *dst_in;

	dst_in = dst;
	if (size > 0) {
		while (--size > 0 && *src != '\0')
			*dst++ = *src++;
		*dst = '\0';
	}
	return dst - dst_in;
}

int
strcmp(const char *p, const char *q)
{
	while (*p && *p == *q)
		p++, q++;
	return (int) ((unsigned char) *p - (unsigned char) *q);
}

int
strncmp(const char *p, const char *q, size_t n)
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
char *
strchr(const char *s, char c)
{
	for (; *s; s++)
		if (*s == c)
			return (char *) s;
	return 0;
}

void *
memchr(void* mem, int chr, int len)
{
	char* s = (char*)mem;
	for(int i = 0; i < len; i++)
		if(s[i] == (char)chr)
			return s+i;
	return NULL;
}

// Return a pointer to the first occurrence of 'c' in 's',
// or a pointer to the string-ending null character if the string has no 'c'.
char *
strfind(const char *s, char c)
{
	for (; *s; s++)
		if (*s == c)
			break;
	return (char *) s;
}

// n must be a multiple of 16 and v must be uint32_t-aligned
static inline void *
memset16(uint32_t *COUNT(n/sizeof(uint32_t)) _v, uint32_t c, size_t n)
{
	uint32_t *start, *end;
	uint32_t *BND(_v, end) v;

	start = _v;
	end = _v + n/sizeof(uint32_t);
	v = _v;
	c = c | c<<8 | c<<16 | c<<24;

	if(n >= 64 && ((uintptr_t)v) % 8 == 0)
	{
		uint64_t* v64 = (uint64_t*)v;
		uint64_t c64 = c | ((uint64_t)c)<<32;
		while(v64 < (uint64_t*)end-7)
		{
			v64[3] = v64[2] = v64[1] = v64[0] = c64;
			v64[7] = v64[6] = v64[5] = v64[4] = c64;
			v64 += 8;
		}
		v = (uint32_t*)v64;
	}

	while(v < end)
	{
		v[3] = v[2] = v[1] = v[0] = c;
		v += 4;
	}

	return start;
}

// n must be a multiple of 16 and v must be 4-byte aligned.
// as allowed by ISO, behavior undefined if dst/src overlap
static inline void *
memcpy16(uint32_t *COUNT(n/sizeof(uint32_t)) _dst,
         const uint32_t *COUNT(n/sizeof(uint32_t)) _src, size_t n)
{
	uint32_t *dststart, *SNT dstend, *SNT srcend;
	uint32_t *BND(_dst,dstend) dst;
	const uint32_t *BND(_src,srcend) src;

	dststart = _dst;
	dstend = (uint32_t *SNT)(_dst + n/sizeof(uint32_t));
	srcend = (uint32_t *SNT)(_src + n/sizeof(uint32_t));
	dst = _dst;
	src = _src;

	while(dst < dstend && src < srcend)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];

		src += 4;
		dst += 4;
	}

	return dststart;
}

void *
pagecopy(void* d, void* s)
{
	static_assert(PGSIZE % 64 == 0);
	for(int i = 0; i < PGSIZE; i += 64)
	{
		*((uint64_t*)(d+i+0)) = *((uint64_t*)(s+i+0));
		*((uint64_t*)(d+i+8)) = *((uint64_t*)(s+i+8));
		*((uint64_t*)(d+i+16)) = *((uint64_t*)(s+i+16));
		*((uint64_t*)(d+i+24)) = *((uint64_t*)(s+i+24));
		*((uint64_t*)(d+i+32)) = *((uint64_t*)(s+i+32));
		*((uint64_t*)(d+i+40)) = *((uint64_t*)(s+i+40));
		*((uint64_t*)(d+i+48)) = *((uint64_t*)(s+i+48));
		*((uint64_t*)(d+i+56)) = *((uint64_t*)(s+i+56));
	}
	return d;
}

void *
memset(void *COUNT(_n) v, int c, size_t _n)
{
	char *BND(v,v+_n) p;
	size_t n0;
	size_t n = _n;

	if (n == 0) return NULL; // zra: complain here?

	p = v;

    while (n > 0 && ((uintptr_t)p & 7))
	{
		*p++ = c;
		n--;
	}

	if(n >= 16 && ((uintptr_t)p & 3) == 0)
	{
		n0 = (n/16)*16;
		memset16((uint32_t*COUNT(n0/sizeof(uint32_t)))p,c,n0);
		n -= n0;
		p += n0;
	}

	while (n > 0)
	{
		*p++ = c;
		n--;
	}

	return v;
}

void *
(DMEMCPY(1,2,3) memcpy)(void *COUNT(_n) dst, const void *COUNT(_n) src, size_t _n)
{
	const char *BND(src,src+_n) s;
	char *BND(dst,dst+_n) d;
	size_t n0;
	size_t n = _n;

	s = src;
	d = dst;

	if(n >= 16 && ((uintptr_t)src  & 3) == 0 && ((uintptr_t)dst & 3) == 0)
	{
		n0 = (n/16)*16;
		memcpy16((uint32_t*COUNT(n0/sizeof(uint32_t)))dst,
                 (const uint32_t*COUNT(n0/sizeof(uint32_t)))src,n0);
		n -= n0;
		s += n0;
		d += n0;
	}

	while (n-- > 0)
		*d++ = *s++;

	return dst;
}

void *
memmove(void *COUNT(_n) dst, const void *COUNT(_n) src, size_t _n)
{
	const char *BND(src,src+_n) s;
	char *BND(dst,dst+_n) d;
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
}

int
memcmp(const void *COUNT(n) v1, const void *COUNT(n) v2, size_t n)
{
	const uint8_t *BND(v1,v1+n) s1 = (const uint8_t *) v1;
	const uint8_t *BND(v2,v2+n) s2 = (const uint8_t *) v2;

	while (n-- > 0) {
		if (*s1 != *s2)
			return (int) *s1 - (int) *s2;
		s1++, s2++;
	}

	return 0;
}

void *
memfind(const void *COUNT(n) _s, int c, size_t n)
{
	const void *SNT ends = (const char *) _s + n;
	const void *BND(_s,_s + n) s = _s;
	for (; s < ends; s++)
		if (*(const unsigned char *) s == (unsigned char) c)
			break;
	return (void *BND(_s,_s+n)) s;
}

long
strtol(const char *s, char **endptr, int base)
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

int
atoi(const char* s)
{
	// no overflow detection
	return (int)strtol(s,NULL,10);
}
