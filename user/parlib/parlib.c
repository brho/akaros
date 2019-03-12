/* Copyright (c) 2015 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <parlib/parlib.h>
#include <parlib/core_set.h>
#include <parlib/ros_debug.h>
#include <stdlib.h>

/* Control variables */
bool parlib_wants_to_be_mcp = TRUE;
bool parlib_never_yield = FALSE;
bool parlib_never_vc_request = FALSE;

/* Creates a child process for program @exe, with args and envs.  Will attempt
 * to look in /bin/ if the initial lookup fails, and will invoke sh to handle
 * non-elfs.  Returns the child's PID on success, -1 o/w. */
pid_t create_child(const char *exe, int argc, char *const argv[],
                   char *const envp[])
{
	pid_t kid;
	char *path_exe;
	char **sh_argv;
	const char *sh_path = "/bin/sh";

	kid = sys_proc_create(exe, strlen(exe), argv, envp, 0);
	if (kid > 0)
		return kid;

	/* Here's how we avoid infinite recursion.  We can only have ENOENT the
	 * first time through without bailing out, since all errno paths set exe
	 * to begin with '/'.  That includes calls from ENOEXEC, since sh_path
	 * begins with /.  To avoid repeated calls to ENOEXEC, we just look for
	 * sh_path as the exe, so if we have consecutive ENOEXECs, we'll bail
	 * out. */
	switch (errno) {
	case ENOENT:
		if (exe[0] == '/')
			return -1;
		path_exe = malloc(MAX_PATH_LEN);
		if (!path_exe)
			return -1;
		/* Our 'PATH' is only /bin. */
		snprintf(path_exe, MAX_PATH_LEN, "/bin/%s", exe);
		path_exe[MAX_PATH_LEN - 1] = 0;
		kid = create_child(path_exe, argc, argv, envp);
		free(path_exe);
		break;
	case ENOEXEC:
		/* In case someone replaces /bin/sh with a non-elf. */
		if (!strcmp(sh_path, exe))
			return -1;
		/* We want enough space for the original argv, plus one entry at
		 * the front for sh_path.  When we grab the original argv, we
		 * also need the trailing NULL, which is at argv[argc].  That
		 * means we really want argc + 1 entries from argv. */
		sh_argv = malloc(sizeof(char *) * (argc + 2));
		if (!sh_argv)
			return -1;
		memcpy(&sh_argv[1], argv, sizeof(char *) * (argc + 1));
		sh_argv[0] = (char*)sh_path;
		/* Replace the original argv[0] with the path to exe, which
		 * might have been edited to include /bin/ */
		sh_argv[1] = (char*)exe;
		kid = create_child(sh_path, argc + 1, sh_argv, envp);
		free(sh_argv);
		break;
	default:
		return -1;
	}
	return kid;
}

/* Creates a child process for exe, and shares the parent's standard FDs (stdin,
 * stdout, stderr) with the child.  Returns the child's PID on success, -1 o/w.
 */
pid_t create_child_with_stdfds(const char *exe, int argc, char *const argv[],
                               char *const envp[])
{
	struct childfdmap fd_dups[3] = { {0, 0}, {1, 1}, {2, 2} };
	pid_t kid;
	int ret;

	kid = create_child(exe, argc, argv, envp);
	if (kid < 0)
		return -1;
	ret = syscall(SYS_dup_fds_to, kid, fd_dups, COUNT_OF(fd_dups));
	if (ret != COUNT_OF(fd_dups)) {
		sys_proc_destroy(kid, -1);
		return -1;
	}
	return kid;
}

/* Provisions the CG cores to PID.  Returns -1 if any of them fail. */
int provision_core_set(pid_t pid, const struct core_set *cores)
{
	struct core_set pvcores;
	size_t max_cores = parlib_nr_total_cores();

	parlib_get_ll_core_set(&pvcores);
	parlib_not_core_set(&pvcores);
	parlib_and_core_sets(&pvcores, cores);
	for (size_t i = 0; i < max_cores; i++) {
		if (parlib_get_core(&pvcores, i)) {
			if (sys_provision(pid, RES_CORES, i))
				return -1;
		}
	}
	return 0;
}
