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

static void nullbind(struct ipifc *ipifc, int i, char **cpp)
{
	error("cannot bind null device");
}

static void nullunbind(struct ipifc *ipifc)
{
}

static void
nullbwrite(struct ipifc *ipifc, struct block *block, int i, uint8_t * i8)
{
	error("nullbwrite");
}

struct medium nullmedium = {
	.name = "null",
	.bind = nullbind,
	.unbind = nullunbind,
	.bwrite = nullbwrite,
};

void nullmediumlink(void)
{
	addipmedium(&nullmedium);
}
