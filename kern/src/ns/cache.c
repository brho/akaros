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

/*
 * no cache in hosted mode
 */
void cinit(void)
{
}

void copen(struct chan *c)
{
	c->flag &= ~CCACHE;
}

int cread(struct chan *c, uint8_t * b, int n, int64_t off)
{
	return 0;
}

void cwrite(struct chan *c, uint8_t * buf, int n, int64_t off)
{
}

void cupdate(struct chan *c, uint8_t * buf, int n, int64_t off)
{
}
