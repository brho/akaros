/*
 * Copyright 2013 Google Inc.
 */
//#define DEBUG
#include <setjmp.h>
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
#include <fcall.h>

int
pusherror(struct errbuf *errstack, int stacksize,
	  int *curindex, struct errbuf  **perrbuf)
{
printd("pushe %p %d %d perr %p *per %p\n", errstack, stacksize, *curindex,
perrbuf,*perrbuf);
    if (*curindex == 0)
	*(struct errbuf **)errstack = *perrbuf;

    *curindex = *curindex + 1;
    if (*curindex >= stacksize)
	panic("Error stack overflow");
    *perrbuf = &errstack[*curindex];
    return 0;
}

struct errbuf *
poperror(struct errbuf *errstack, int stacksize,
	  int *curindex, struct errbuf  **perrbuf)
{
printd("pope %p %d %d\n", errstack, stacksize, *curindex);
    *curindex = *curindex - 1;
    if (*curindex < 0)
	panic("Error stack underflow");

    if (*curindex == 0)
	*perrbuf = *(struct errbuf**)errstack;
    else
	*perrbuf = &errstack[*curindex];
    
    return *perrbuf;
}
