/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Common printf format extensions.  For now, %r is installed by default
 * (in early init code), and the others need to be requested.
 *
 * To register, for example %i for ipaddr, call:
 * 	register_printf_specifier('i', printf_ipaddr, printf_ipaddr_info);
 */

#pragma once

#include <parlib/common.h>
#include <printf.h>

__BEGIN_DECLS

/* Commonly used as %i, will print out a 16-byte plan9 IP address */
int printf_ipaddr(FILE *stream, const struct printf_info *info,
                  const void *const *args);
int printf_ipaddr_info(const struct printf_info* info, size_t n, int *argtypes,
                       int *size);

/* Commonly used as %M, will print out a plan9 IPv6 mask, preferably as /xx */
int printf_ipmask(FILE *stream, const struct printf_info *info,
                  const void *const *args);
int printf_ipmask_info(const struct printf_info* info, size_t n, int *argtypes,
                       int *size);

/* Commonly used as %E, will print out an ethernet address */
int printf_ethaddr(FILE *stream, const struct printf_info *info,
                   const void *const *args);
int printf_ethaddr_info(const struct printf_info* info, size_t n, int *argtypes,
                        int *size);

/* Commonly used as %H, will print out a pointer as hex.  Pass it a precision,
 * as in ("%.5H", ptr) (5 bytes) or ("%.*H", x, ptr) (x bytes). */
int printf_hexdump(FILE *stream, const struct printf_info *info,
                   const void *const *args);
int printf_hexdump_info(const struct printf_info* info, size_t n, int *argtypes,
                        int *size);

/* Installed by default, will print the errstr for %r */
int printf_errstr(FILE *stream, const struct printf_info *info,
                  const void *const *args);
int printf_errstr_info(const struct printf_info *info, size_t n, int *argtypes,
                       int *size);

__END_DECLS
