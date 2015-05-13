/* Copyright (c) 2014 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#ifndef PTHREAD_PROFALARM_H
#define PTHREAD_PROFALARM_H

__BEGIN_DECLS

void enable_profalarm(int hz);
void disable_profalarm();

__END_DECLS

#endif	/* PTHREAD_PROFALARM_H */
