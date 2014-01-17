// INFERNO
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
#include <ip.h>


void
hnputv(void *p, int64_t v)
{
	uint8_t *a;

	a = p;
	hnputl(a, v>>32);
	hnputl(a+4, v);
}

void
hnputl(void *p, uint32_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v>>24;
	a[1] = v>>16;
	a[2] = v>>8;
	a[3] = v;
}

void
hnputs(void *p, uint16_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v>>8;
	a[1] = v;
}

int64_t
nhgetv(void *p)
{
	uint8_t *a;

	a = p;
	return ((int64_t)nhgetl(a) << 32) | nhgetl(a+4);
}

uint32_t
nhgetl(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0]<<24)|(a[1]<<16)|(a[2]<<8)|(a[3]<<0);
}

uint16_t
nhgets(void *p)
{
	uint8_t *a;

	a = p;
	return (a[0]<<8)|(a[1]<<0);
}
