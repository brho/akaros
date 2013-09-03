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

static	short	endian	= 1;
static	uint8_t*aendian	= ( uint8_t *)&endian;
#define	LITTLE	*aendian

uint16_t
ptclbsum(uint8_t *addr, int len)
{
	uint32_t losum, hisum, mdsum, x;
	uint32_t t1, t2;

	losum = 0;
	hisum = 0;
	mdsum = 0;

	x = 0;
	if(((uintptr_t)addr) & 1) {
		if(len) {
			hisum += addr[0];
			len--;
			addr++;
		}
		x = 1;
	}
	while(len >= 16) {
		t1 = *(uint16_t*)(addr+0);
		t2 = *(uint16_t*)(addr+2);	mdsum += t1;
		t1 = *(uint16_t*)(addr+4);	mdsum += t2;
		t2 = *(uint16_t*)(addr+6);	mdsum += t1;
		t1 = *(uint16_t*)(addr+8);	mdsum += t2;
		t2 = *(uint16_t*)(addr+10);	mdsum += t1;
		t1 = *(uint16_t*)(addr+12);	mdsum += t2;
		t2 = *(uint16_t*)(addr+14);	mdsum += t1;
		mdsum += t2;
		len -= 16;
		addr += 16;
	}
	while(len >= 2) {
		mdsum += *(uint16_t*)addr;
		len -= 2;
		addr += 2;
	}
	if(x) {
		if(len)
			losum += addr[0];
		if(LITTLE)
			losum += mdsum;
		else
			hisum += mdsum;
	} else {
		if(len)
			hisum += addr[0];
		if(LITTLE)
			hisum += mdsum;
		else
			losum += mdsum;
	}

	losum += hisum >> 8;
	losum += (hisum & 0xff) << 8;
	while(hisum = (losum>>16))
		losum = hisum + (losum & 0xffff);

	return losum & 0xffff;
}
