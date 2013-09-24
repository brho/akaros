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

/* General idea: if we're at the base for this func (base of ERRSTACK in the
 * scope where ERRSTACK and waserror are used), we need to find and save the
 * previous errbuf, so we know how to pop back.
 *
 * The main goal of this is to track and advertise (via pcpui) the errbuf that
 * should be jumped to next (when we call error()).  Note that the caller of
 * this (waserror()) will fill the jumpbuf shortly with its context. */
int errpush(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf **prev_errbuf)
{
	printd("pushe %p %d %dp\n", errstack, stacksize, *curindex);
	if (*curindex == 0)
		*prev_errbuf = get_cur_errbuf();

	if (*curindex >= stacksize)
		panic("Error stack overflow");
	set_cur_errbuf(&errstack[*curindex]);
	*curindex = *curindex + 1;
	return 0;
}

/* Undo the work of errpush, and advertise the new errbuf used by error() calls.
 * We only need to be tricky when we reached the beginning of the stack and need
 * to check the prev_errbuf from a previous ERRSTACK/function. */
void errpop(struct errbuf *errstack, int stacksize, int *curindex,
            struct errbuf *prev_errbuf)
{
	printd("pope %p %d %d\n", errstack, stacksize, *curindex);
	*curindex = *curindex - 1;
	if (*curindex < 0)
		panic("Error stack underflow");

	if (*curindex == 0)
		set_cur_errbuf(prev_errbuf);
	else
		set_cur_errbuf(&errstack[*curindex]);
}
