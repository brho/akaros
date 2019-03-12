/*
 * Copyright 2013 Google Inc.
 */
//#define DEBUG
#include <setjmp.h>
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
#include <err.h>

/* General idea: if we're at the base for this func (base of ERRSTACK in the
 * scope where ERRSTACK and waserror are used), we need to find and save the
 * previous errbuf, so we know how to pop back.
 *
 * The main goal of this is to track and advertise (via pcpui) the errbuf that
 * should be jumped to next (when we call error()).  Note that the caller of
 * this (waserror()) will fill the jumpbuf shortly with its context.
 *
 * When we enter, curindex points to the slot we should use.  First time, it is
 * 0, and we'll set cur_eb to slot 0.  When we leave, curindex is set to the
 * next free slot. */
struct errbuf *errpush(struct errbuf *errstack, int stacksize, int *curindex,
		       struct errbuf **prev_errbuf)
{
	struct errbuf *cbuf;

	printd("pushe %p %d %dp\n", errstack, stacksize, *curindex);
	if (*curindex == 0)
		*prev_errbuf = get_cur_errbuf();

	if (*curindex >= stacksize)
		panic("Error stack overflow");

	cbuf = &errstack[*curindex];
	set_cur_errbuf(cbuf);
	(*curindex)++;

	return cbuf;
}

/* Undo the work of errpush, and advertise the new errbuf used by error() calls.
 * We only need to be tricky when we reached the beginning of the stack and need
 * to check the prev_errbuf from a previous ERRSTACK/function.
 *
 * When we enter, curindex is the slot of the next *free* errstack (the one we'd
 * push into if we were pushing.  When we leave, it will be decreased by one,
 * and will still point to the next free errstack (the one we are popping).
 */
struct errbuf *errpop(struct errbuf *errstack, int stacksize, int *curindex,
		      struct errbuf *prev_errbuf)
{
	struct errbuf *cbuf;

	printd("pope %p %d %d\n", errstack, stacksize, *curindex);
	/* curindex now points to the slot we are popping*/
	*curindex = *curindex - 1;
	/* We still need to advertise the previous slot, which is one back from
	 * curindex.  If curindex is 0, that means the next free slot is the
	 * first of our errstack.  In this case, we need to advertise the prev.
	 */
	if (*curindex < 0)
		panic("Error stack underflow");

	cbuf = (*curindex == 0) ? prev_errbuf: &errstack[*curindex - 1];
	set_cur_errbuf(cbuf);

	return cbuf;
}
