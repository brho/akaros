#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include "ip.h"

void
hnputv(void *p, uint64_t v)
{
	uint8_t *a;

	a = p;
	a[0] = v>>56;
	a[1] = v>>48;
	a[2] = v>>40;
	a[3] = v>>32;
	a[4] = v>>24;
	a[5] = v>>16;
	a[6] = v>>8;
	a[7] = v;
}

void
hnputl(void *p, unsigned int v)
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

uint64_t
nhgetv(void *p)
{
	uint8_t *a;
	uint64_t v;

	a = p;
	v = (uint64_t)a[0]<<56;
	v |= (uint64_t)a[1]<<48;
	v |= (uint64_t)a[2]<<40;
	v |= (uint64_t)a[3]<<32;
	v |= a[4]<<24;
	v |= a[5]<<16;
	v |= a[6]<<8;
	v |= a[7]<<0;
	return v;
}

unsigned int
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
