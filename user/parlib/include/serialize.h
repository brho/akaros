/* Copyright (c) 2015 Google Inc., All Rights Reserved.
 * Kevin Klues <klueska@google.com>
 * See LICENSE for details. */

#ifndef PARLIB_SERIALIZE_H
#define PARLIB_SERIALIZE_H

#include <stddef.h>

struct serialized_data {
	size_t len;
	char buf[];
};
extern struct serialized_data* serialize_argv_envp(char* const* argv,
                                                   char* const* envp);
extern void free_serialized_data(struct serialized_data* sd);

#endif // PARLIB_SERIALIZE_H
