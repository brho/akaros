/* Copyright (c) 2012 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 *
 * See LICENSE for details. */

#pragma once

#include <stdint.h>
#include <stdlib.h>

__BEGIN_DECLS

#ifndef __GNUC__
#error "You need to be using gcc to compile this library..."
#endif

/* Declaration of types needed for dynamically allocatable tls */
typedef struct dtls_key *dtls_key_t;
typedef void (*dtls_dtor_t)(void *);

/* Initialize a dtls_key for dynamically setting/getting uthread local storage
 * on a uthread or vcore. */
dtls_key_t dtls_key_create(dtls_dtor_t dtor);

/* Destroy a dtls key. */
void dtls_key_delete(dtls_key_t key);

/* Set dtls storage for the provided dtls key on the current uthread or vcore.
 */
void set_dtls(dtls_key_t key, const void *dtls);

/* Get dtls storage for the provided dtls key on the current uthread or vcore.
 */
void *get_dtls(dtls_key_t key);

/* Destroy all dtls storage associated with the current uthread or vcore. */
void destroy_dtls(void);

__END_DECLS
