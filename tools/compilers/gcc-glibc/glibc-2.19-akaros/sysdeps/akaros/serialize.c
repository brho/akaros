/* Copyright (c) 2015 Google Inc., All Rights Reserved.
 * Kevin Klues <klueska@google.com>
 * See LICENSE for details. */

#include <parlib/serialize.h>
#include <malloc.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

struct serialized_data *serialize_argv_envp(char *const argv[],
                                            char *const envp[])
{
	size_t bufsize = 0;
	size_t argc = 0, envc = 0;
	struct serialized_data *sd;

	/* Count how many args and environment variables we have. */
	if(argv) while(argv[argc]) argc++;
	if(envp) while(envp[envc]) envc++;

	/* Reserve space for argc and envc as well as space for the actual
	 * pointers to our arguments and environment variables in a
	 * to-be-allocated buffer ... */
	bufsize += sizeof(argc) + sizeof(envc);
	bufsize += ((argc + envc) * sizeof(uintptr_t));

	/* Reserve space in the buffer for each of our arguments (the +1 comes
	 * from the '\0' character) */
	int arglens[argc];

	for (int i = 0; i < argc; i++) {
		arglens[i] = strlen(argv[i]) + 1;
		bufsize += arglens[i];
	}

	/* Reserve space in our buffer for each of our environment variables
	 * (the +1 comes from the '\0' character) */
	int envlens[envc];

	for (int i = 0; i < envc; i++) {
		envlens[i] = strlen(envp[i]) + 1;
		bufsize += envlens[i];
	}

	/* Allocate an sd struct with enough room for all of our
	 * arguments, environment variables, and their pointers. */
	sd = calloc(1, sizeof(struct serialized_data) + bufsize);
	if (!sd)
		return sd;

	/* Now fill in the buffer!. */
	size_t *sd_argc = (size_t*)(sd->buf);
	size_t *sd_envc = (size_t*)(sd->buf + sizeof(size_t));
	char **ppos = (char**)(sd->buf + 2*sizeof(size_t));
	char *vpos = (char*)(ppos + argc + envc);
	char *vpos_base = vpos;

	sd->len = bufsize;
	*sd_argc = argc;
	*sd_envc = envc;
	for (int i = 0; i < argc; i++) {
		ppos[i] = (char*)(vpos - vpos_base);
		memcpy(vpos, argv[i], arglens[i]);
		vpos += arglens[i];
	}
	for(int i = 0; i < envc; i++) {
		ppos[argc + i] = (char*)(vpos - vpos_base);
		memcpy(vpos, envp[i], envlens[i]);
		vpos += envlens[i];
	}
	return sd;
}

void free_serialized_data(struct serialized_data *sd)
{
	free(sd);
}

