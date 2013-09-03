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

int
equivip4(uint8_t *a, uint8_t *b)
{
	int i;

	for(i = 0; i < 4; i++)
		if(a[i] != b[i])
			return 0;
	return 1;
}

int
equivip6(uint8_t *a, uint8_t *b)
{
	int i;

	for(i = 0; i < IPaddrlen; i++)
		if(a[i] != b[i])
			return 0;
	return 1;
}
