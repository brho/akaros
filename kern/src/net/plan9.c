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
 *  some hacks for commonality twixt inferno and plan9
 */

char*
commonuser(void)
{
	return current->user;
}

struct chan*
commonfdtochan(int fd, int mode, int a, int b)
{
	return fdtochan(current->fgrp, fd, mode, a, b);
}

char*
commonerror(void)
{
	return current_errstr();
}

int
postnote(struct proc *p, int unused_int, char *note, int val)
{
	panic("postnote");
	return 0;
}
