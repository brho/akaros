/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Networking helpers for dealing with the plan 9 interface. */

#ifndef _NET_H
#define _NET_H

static inline int snprintf_overflow(int ret, char *buf, size_t buf_len)
{
	return (ret == buf_len) && (buf[buf_len - 1] != 0);
}

int cheap_dial(char *addr, char *local, char *dir, int *cfdp);

#endif /* _NET_H */
