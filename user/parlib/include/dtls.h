/*
 * Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * This file is part of Parlib.
 * 
 * Parlib is free software: you can redistribute it and/or modify
 * it under the terms of the Lesser GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Parlib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Lesser GNU General Public License for more details.
 * 
 * See COPYING.LESSER for details on the GNU Lesser General Public License.
 * See COPYING for details on the GNU General Public License.
 */

#ifndef PARLIB_DTLS_H
#define PARLIB_DTLS_H

#include <stdint.h>
#include <stdlib.h>

#ifndef __GNUC__
  #error "You need to be using gcc to compile this library..."
#endif 

#ifdef __cplusplus
extern "C" {
#endif

/* Declaration of types needed for dynamically allocatable tls */
typedef struct dtls_key *dtls_key_t;
typedef void (*dtls_dtor_t)(void*);

/* Initialize a dtls_key for dynamically setting/getting uthread local storage
 * on a uthread or vcore. */
dtls_key_t dtls_key_create(dtls_dtor_t dtor);

/* Destroy a dtls key. */
void dtls_key_delete(dtls_key_t key);

/* Set dtls storage for the provided dtls key on the current uthread or vcore. */
void set_dtls(dtls_key_t key, void *dtls);

/* Get dtls storage for the provided dtls key on the current uthread or vcore. */
void *get_dtls(dtls_key_t key);

/* Destroy all dtls storage associated with the current uthread or vcore. */
void destroy_dtls();

#ifdef __cplusplus
}
#endif

#endif /* PARLIB_DTLS_H */

