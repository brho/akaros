/* Copyright (c) 2012 The Regents of the University of California
 * Andrew Waterman <waterman@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * glibc syscall functions / tools for working with Akaros */

#ifndef _GLIBC_ROS_VCORE_TLS_H
#define _GLIBC_ROS_VCORE_TLS_H

#ifdef __cplusplus
extern "C" {
#endif

void set_tls_desc(void* addr, int vcoreid);
void *get_tls_desc(int vcoreid);

void *allocate_tls(void);
void free_tls(void *tcb);
void *reinit_tls(void *tcb);

#ifdef __cplusplus
}
#endif

#endif /* _GLIBC_ROS_SYSCALL_H */
