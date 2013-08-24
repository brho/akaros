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
errpush(struct errbuf *errstack, int stacksize, int *curindex)
{
	printd("pushe %p %d %dp\n", errstack, stacksize, *curindex);
	if (*curindex == 0)
		*(struct errbuf **)errstack = get_cur_errbuf();

	*curindex = *curindex + 1;
	if (*curindex >= stacksize)
		panic("Error stack overflow");
	set_cur_errbuf(&errstack[*curindex]);
	return 0;
}

void errpop(struct errbuf *errstack, int stacksize, int *curindex)
{
	printd("pope %p %d %d\n", errstack, stacksize, *curindex);
	*curindex = *curindex - 1;
	if (*curindex < 0)
		panic("Error stack underflow");

	if (*curindex == 0)
		set_cur_errbuf(*(struct errbuf **)errstack);
	else
		set_cur_errbuf(&errstack[*curindex]);
}
