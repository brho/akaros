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

#if 1
/* sadly, we don't have this yet; too bad!
 */
int eipfmt(void *unused)
{
	return 0;

}
#else
enum
{
	Isprefix= 16,
};

uint8_t prefixvals[256] =
{
[0x00] 0 | Isprefix,
[0x80] 1 | Isprefix,
[0xC0] 2 | Isprefix,
[0xE0] 3 | Isprefix,
[0xF0] 4 | Isprefix,
[0xF8] 5 | Isprefix,
[0xFC] 6 | Isprefix,
[0xFE] 7 | Isprefix,
[0xFF] 8 | Isprefix,
};

int
eipfmt(Fmt *f)
{
	char buf[5*8];
	static char *efmt = "%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux";
	static char *ifmt = "%d.%d.%d.%d";
	uint8_t *p, ip[16];
	uint32_t *lp;
	uint16_t s;
	int i, j, n, eln, eli;

	switch(f->r) {
	case 'E':		/* Ethernet address */
		p = va_arg(f->args, uint8_t *unused_uint8_p_t);
		snprintf(buf, sizeof buf, efmt, p[0], p[1], p[2], p[3], p[4], p[5]);
		return fmtstrncpy(f,  buf, sizeof(f));

	case 'I':		/* Ip address */
		p = va_arg(f->args, uint8_t *unused_uint8_p_t);
common:
		if(memcmp(p, v4prefix, 12) == 0){
			snprintf(buf, sizeof buf, ifmt, p[12], p[13], p[14], p[15]);
			return fmtstrncpy(f,  buf, sizeof(f));
		}

		/* find longest elision */
		eln = eli = -1;
		for(i = 0; i < 16; i += 2){
			for(j = i; j < 16; j += 2)
				if(p[j] != 0 || p[j+1] != 0)
					break;
			if(j > i && j - i > eln){
				eli = i;
				eln = j - i;
			}
		}

		/* print with possible elision */
		n = 0;
		for(i = 0; i < 16; i += 2){
			if(i == eli){
				n += sprint(buf+n, "::");
				i += eln;
				if(i >= 16)
					break;
			} else if(i != 0)
				n += sprint(buf+n, ":");
			s = (p[i]<<8) + p[i+1];
			n += sprint(buf+n, "%ux", s);
		}
		return fmtstrncpy(f,  buf, sizeof(f));

	case 'i':		/* v6 address as 4 longs */
		lp = va_arg(f->args, uint32_t*);
		for(i = 0; i < 4; i++)
			hnputl(ip+4*i, *lp++);
		p = ip;
		goto common;

	case 'V':		/* v4 ip address */
		p = va_arg(f->args, uint8_t *unused_uint8_p_t);
		snprintf(buf, sizeof buf, ifmt, p[0], p[1], p[2], p[3]);
		return fmtstrncpy(f,  buf, sizeof(f));

	case 'M':		/* ip mask */
		p = va_arg(f->args, uint8_t *unused_uint8_p_t);

		/* look for a prefix mask */
		for(i = 0; i < 16; i++)
			if(p[i] != 0xff)
				break;
		if(i < 16){
			if((prefixvals[p[i]] & Isprefix) == 0)
				goto common;
			for(j = i+1; j < 16; j++)
				if(p[j] != 0)
					goto common;
			n = 8*i + (prefixvals[p[i]] & ~Isprefix);
		} else
			n = 8*16;

		/* got one, use /xx format */
		snprintf(buf, sizeof buf, "/%d", n);
		return fmtstrncpy(f,  buf, sizeof(f));
	}
	return fmtstrncpy(f,  "(eipfmt)", sizeof(f));
}
#endif
