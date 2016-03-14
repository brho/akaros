/* Copyright (c) 2014 Google Inc., All Rights Reserved.
 * Kevin Klues <klueska@google.com>
 * See LICENSE for details. */

#include <parlib/serialize.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv, char **envp)
{
	struct serialized_data *sd = serialize_argv_envp(argv, envp);
	size_t *kargc = (size_t*)sd->buf;
	size_t *kenvc = (size_t*)(sd->buf + sizeof(size_t));
	char **kargv = (char**)(sd->buf + 2*sizeof(size_t));
	char **kenvp = (char**)(kargv + *kargc);
	uintptr_t bufbase = (uintptr_t)(kenvp + *kenvc);

	printf("argc: %lu\n", *kargc);
	printf("envc: %lu\n", *kenvc);
	for (int i = 0; i < *kargc; i++)
		printf("argv[%d]: %s\n", i, kargv[i] + bufbase);
	for (int i = 0; i < *kenvc; i++)
		printf("envp[%d]: %s\n", i, kenvp[i] + bufbase);
	free_serialized_data(sd);
	return 0;
}
