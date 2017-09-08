/* Copyright (c) 2016-2017 Google Inc., All Rights Reserved.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * TODO:
 * - Do per-syscall output formatting, like interpreting arguments
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <parlib/parlib.h>
#include <parlib/bitmask.h>

struct strace_opts {
	FILE						*outfile;
	char						*trace_set;
	char						**cmd_argv;
	int							cmd_argc;
	int							pid;
	bool						follow_children;
	bool						verbose;
	bool						raw_output;
	bool						with_time;
	bool						drop_overflow;
};
static struct strace_opts opts;

static const char *doc = "strace -- trace syscalls of a process";
static const char *args_doc = "-p PID\nPROGRAM [ARGS]\n";

static struct argp_option argp_opts[] = {
	{"output", 'o', "FILE", 0, "Print output to file (default stderr)"},
	{"pid", 'p', "PID", 0, "Process to attach to"},
	{"follow", 'f', 0, 0, "Trace children"},
	{"verbose", 'v', 0, 0, "Print extra info, e.g. syscalls we're tracing"},
	{0, 0, 0, 0, ""},
	{"traceset", 'e', "TRACE_SET", 0,
	 "Comma-separated list of syscalls by name (e.g. openat) and sets to trace.  Use '!' to negate (might need to escape the '!'), and traces are handled in order (e.g. -e path,\\!openat)\n"
	 },
	{"    Available sets:", 0, 0, OPTION_DOC | OPTION_NO_USAGE,
	                "\n"
	                "- path: syscalls that take file paths\n"
	                "- fd: syscalls that take FDs\n"
	                "- file: path and fd sets\n"
	                "- mem: memory related (mmap, shared mem)\n"
	                "- life: process lifetime (create, fork)\n"
	                "- proc: anything process related (yields, pop_ctxs, life)\n"
	                "- sched: requests or yields to the kernel (all resources)\n"
	                "- vmm: syscalls mostly for VMs\n"
	},
	{0, 0, 0, 0, ""},
	{"drop", 'd', 0, 0, "Drop syscalls on overflow"},
	{"raw", 'r', 0, 0, "Raw, untranslated output, with timestamps"},
	{"time", 't', 0, 0, "Print timestamps"},
	{0, 'h', 0, OPTION_HIDDEN, 0},
	{ 0 }
};

struct trace_set {
	char						*name;
	unsigned int				syscs[];
};

/* To add a trace set, create one here, add it to all_trace_sets, and update the
 * help field in argp_opts. */

/* If you change this, update 'file' below. */
static struct trace_set path_trace_set = { "path",
	{SYS_proc_create,
	 SYS_exec,
	 SYS_openat,
	 SYS_stat,
	 SYS_lstat,
	 SYS_access,
	 SYS_link,
	 SYS_unlink,
	 SYS_symlink,
	 SYS_readlink,
	 SYS_chdir,
	 SYS_mkdir,
	 SYS_rmdir,
	 SYS_nbind,
	 SYS_nmount,
	 SYS_nunmount,
	 SYS_wstat,
	 SYS_rename,
	 0}
};

/* If you change this, update 'file' below.
 *
 * Technically tcgetattr/tcsetattr are FDs, but it's mostly noise.  This also
 * tracks openat, since that's the source for all FDs */
static struct trace_set fd_trace_set = { "fd",
	{SYS_openat,
	 SYS_mmap,
	 SYS_read,
	 SYS_write,
	 SYS_openat,
	 SYS_close,
	 SYS_fstat,
	 SYS_fcntl,
	 SYS_llseek,
	 SYS_fchdir,
	 SYS_nmount,
	 SYS_fd2path,
	 SYS_fwstat,
	 SYS_dup_fds_to,
	 SYS_tap_fds,
	 SYS_abort_sysc_fd,
	 0}
};

/* This is the manually-created contents of 'path' and 'fd' */
static struct trace_set file_trace_set = { "file",
	{/* From 'path' */
	 SYS_proc_create,
	 SYS_exec,
	 SYS_openat,
	 SYS_stat,
	 SYS_lstat,
	 SYS_access,
	 SYS_link,
	 SYS_unlink,
	 SYS_symlink,
	 SYS_readlink,
	 SYS_chdir,
	 SYS_mkdir,
	 SYS_rmdir,
	 SYS_nbind,
	 SYS_nmount,
	 SYS_nunmount,
	 SYS_wstat,
	 SYS_rename,
	 SYS_openat,
	 SYS_mmap,
	 SYS_read,
	 SYS_write,

	 /* From 'fd' */
	 SYS_openat,
	 SYS_close,
	 SYS_fstat,
	 SYS_fcntl,
	 SYS_llseek,
	 SYS_fchdir,
	 SYS_nmount,
	 SYS_fd2path,
	 SYS_fwstat,
	 SYS_dup_fds_to,
	 SYS_tap_fds,
	 SYS_abort_sysc_fd,
	 0}
};

static struct trace_set mem_trace_set = { "mem",
	{SYS_mmap,
	 SYS_mprotect,
	 SYS_munmap,
	 SYS_shared_page_alloc,
	 SYS_shared_page_free,
	 SYS_populate_va,
	 0}
};

/* These are all in 'proc'; keep them in sync. */
static struct trace_set life_trace_set = { "life",
	{SYS_proc_create,
	 SYS_proc_run,
	 SYS_proc_destroy,
	 SYS_fork,
	 SYS_exec,
	 SYS_waitpid,
	 SYS_change_to_m,
	 0}
};

/* There's some misc stuff lumped in here.  block/nanosleep are signs of threads
 * sleeping on something, which is usually useful.  aborting syscs are also
 * often signs of control flow. */
static struct trace_set proc_trace_set = { "proc",
	{SYS_block,
	 SYS_nanosleep,
	 SYS_getpcoreid,
	 SYS_getvcoreid,
	 SYS_proc_yield,
	 SYS_change_vcore,
	 SYS_notify,
	 SYS_self_notify,
	 SYS_send_event,
	 SYS_vc_entry,
	 SYS_halt_core,
	 SYS_pop_ctx,
	 SYS_abort_sysc,
	 SYS_abort_sysc_fd,

	 /* From 'life' */
	 SYS_proc_create,
	 SYS_proc_run,
	 SYS_proc_destroy,
	 SYS_fork,
	 SYS_exec,
	 SYS_waitpid,
	 SYS_change_to_m,
	 0}
};

static struct trace_set sched_trace_set = { "sched",
	{SYS_provision,
	 SYS_proc_yield,
	 SYS_poke_ksched,
	 0}
};

static struct trace_set vmm_trace_set = { "vmm",
	{SYS_vmm_add_gpcs,
	 SYS_vmm_poke_guest,
	 SYS_vmm_ctl,
	 SYS_pop_ctx,
	 0}
};

static struct trace_set *all_trace_sets[] = {
	&path_trace_set,
	&fd_trace_set,
	&file_trace_set,
	&mem_trace_set,
	&life_trace_set,
	&proc_trace_set,
	&sched_trace_set,
	&vmm_trace_set,
};

static DECL_BITMASK(traceset_bm, MAX_SYSCALL_NR);


static error_t parse_strace_opt(int key, char *arg, struct argp_state *state)
{
	struct strace_opts *s_opts = state->input;

	switch (key) {
	case 'o':
		s_opts->outfile = fopen(arg, "wb");
		if (!s_opts->outfile) {
			fprintf(stderr, "Unable to open file '%s' for writing: %s\n",
					arg, strerror(errno));
			exit(1);
		}
		break;
	case 'e':
		s_opts->trace_set = arg;
		break;
	case 'p':
		s_opts->pid = atoi(arg);
		if (!s_opts->pid)
			argp_error(state, "Cannot trace pid 0 (won't exist)");
		break;
	case 'f':
		s_opts->follow_children = TRUE;
		break;
	case 'v':
		s_opts->verbose = TRUE;
		break;
	case 'r':
		s_opts->raw_output = TRUE;
		break;
	case 't':
		s_opts->with_time = TRUE;
		break;
	case 'd':
		s_opts->drop_overflow = TRUE;
		break;
	case ARGP_KEY_ARG:
		if (s_opts->pid)
			argp_error(state, "PID already set, can't launch a process too");
		s_opts->cmd_argc = state->argc - state->next + 1;
		s_opts->cmd_argv = malloc(sizeof(char*) * (s_opts->cmd_argc + 1));
		assert(s_opts->cmd_argv);
		s_opts->cmd_argv[0] = arg;
		memcpy(&s_opts->cmd_argv[1], &state->argv[state->next],
		       sizeof(char*) * (s_opts->cmd_argc - 1));
		s_opts->cmd_argv[s_opts->cmd_argc] = NULL;
		state->next = state->argc;
		break;
	case ARGP_KEY_END:
		if (!(s_opts->cmd_argc || s_opts->pid))
			argp_error(state, "Need either -p or a command to run");
		/* Note we never fclose outfile.  It'll flush when we exit.  o/w, we'll
		 * need to be careful whether we're closing stderr or not. */
		if (!s_opts->outfile)
			s_opts->outfile = stderr;
		break;
	case 'h':
		argp_usage(state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static bool handle_trace_set(char *tok, bool clear)
{
	struct trace_set *ts;
	unsigned int sysc_nr;

	for (int i = 0; i < COUNT_OF(all_trace_sets); i++) {
		ts = all_trace_sets[i];
		if (!strcmp(ts->name, tok)) {
			for (int j = 0; j < MAX_SYSCALL_NR; j++) {
				sysc_nr = ts->syscs[j];
				/* 0-terminated list */
				if (!sysc_nr)
					break;
				if (clear)
					CLR_BITMASK_BIT(traceset_bm, sysc_nr);
				else
					SET_BITMASK_BIT(traceset_bm, sysc_nr);
			}
			return TRUE;
		}
	}
	return FALSE;
}

static char *resolve_syscall_alias(char *tok)
{
	if (!strcmp(tok, "open")) {
		tok = "openat";
		return tok;
	}
	return tok;
}

static bool handle_raw_syscall(char *tok, bool clear)
{
	for (int i = 0; i < __syscall_tbl_sz; i++) {
		if (!__syscall_tbl[i])
			continue;
		tok = resolve_syscall_alias(tok);
		if (!strcmp(__syscall_tbl[i], tok)) {
			if (clear)
				CLR_BITMASK_BIT(traceset_bm, i);
			else
				SET_BITMASK_BIT(traceset_bm, i);
			return TRUE;
		}
	}
	return FALSE;
}

static void build_ignore_list(char *trace_set)
{
	char *tok, *tok_save = 0;
	bool clear = FALSE;

	if (!trace_set) {
		if (opts.verbose)
			fprintf(stderr, "# Tracing all syscalls\n");
		return;
	}
	for (tok = strtok_r(trace_set, ",", &tok_save);
	     tok;
		 tok = strtok_r(NULL, ",", &tok_save)) {

		if (tok[0] == '!') {
			clear = TRUE;
			tok++;
		}
		if (handle_trace_set(tok, clear))
			continue;
		if (handle_raw_syscall(tok, clear))
			continue;
		/* You could imaging continuing, but this error would probably be
		 * missed in the output stream and we'd be wondering why we weren't
		 * getting a syscall, due to a typo. */
		fprintf(stderr, "Unknown trace_set argument %s, aborting!\n",
		        tok);
		exit(-1);
	}
	if (opts.verbose) {
		for (int i = 0; i < MAX_SYSCALL_NR; i++) {
			if (GET_BITMASK_BIT(traceset_bm, i))
				fprintf(stderr, "# Tracing syscall %s (%d)\n",
				        __syscall_tbl[i], i);
		}
	}
}

/* Removes the timestamp part of the line.  Use the return string in place of
 * the full line you pass in. */
static char *remove_timestamps(char *full_line)
{
	char *close_brace;

	/* Format: E [  13655.986589401]-[      0.000000000] Syscall.  The seconds
	 * field may vary in size, so we need to find the second ']'. */
	close_brace = strchr(full_line, ']');
	if (!close_brace)
		return full_line;
	close_brace = strchr(close_brace + 1, ']');
	if (!close_brace)
		return full_line;
	/* move starting E or X marker */
	*close_brace = full_line[0];
	return close_brace;
}

static void parse_traces(int fd)
{
	char *line, *_line;
	ssize_t ret;

	line = malloc(SYSTR_BUF_SZ);
	assert(line);

	while ((ret = read(fd, line, SYSTR_BUF_SZ)) > 0) {
		/* make sure each line ends in \n\0. */
		line[ret - 1] = '\n';
		line[MIN(ret, SYSTR_BUF_SZ - 1)] = 0;
		_line = line;
		if (opts.raw_output) {
			fprintf(opts.outfile, "%s", _line);
			continue;
		}
		if (!opts.with_time)
			_line = remove_timestamps(_line);
		fprintf(opts.outfile, "%s", _line);
	}
	/* This is a little hokey.  If the process exited, then the qio hung up and
	 * we got a status message from the kernel.  This was the errstr of the last
	 * failed read.  However, if we're doing a -p and someone kills *us*, we'll
	 * never see this.  And catching the signal doesn't help either.  The
	 * process needs to exit (strace_shutdown).  Either that, or change the
	 * kernel to set_errstr() on close(), and coordinate with a sighandler. */
	if (opts.verbose)
		fprintf(stderr, "%r\n");
	free(line);
}

int main(int argc, char **argv, char **envp)
{
	int fd;
	pid_t pid;
	static char path[2 * MAX_PATH_LEN];
	struct syscall sysc;
	struct argp argp = {argp_opts, parse_strace_opt, args_doc, doc};

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &opts);

	build_ignore_list(opts.trace_set);

	if (opts.cmd_argc) {
		pid = create_child_with_stdfds(opts.cmd_argv[0], opts.cmd_argc,
		                               opts.cmd_argv, envp);
		if (pid < 0) {
			perror("Unable to spawn child");
			exit(-1);
		}
		/* We need to wait on the child asynchronously.  If we hold a ref (as
		 * the parent), the child won't proc_free and that won't hangup/wake us
		 * from a read. */
		syscall_async(&sysc, SYS_waitpid, pid, NULL, 0, 0, 0, 0);
	} else {
		pid = opts.pid;
	}

	snprintf(path, sizeof(path), "/proc/%d/ctl", pid);
	fd = open(path, O_WRITE);
	if (fd < 0) {
		fprintf(stderr, "open %s: %r\n", path);
		exit(1);
	}
	if (opts.follow_children)
		snprintf(path, sizeof(path), "straceall");
	else
		snprintf(path, sizeof(path), "straceme");
	if (write(fd, path, strlen(path)) < strlen(path)) {
		fprintf(stderr, "write to ctl %s: %r\n", path);
		exit(1);
	}
	if (opts.drop_overflow) {
		snprintf(path, sizeof(path), "strace_drop on");
		if (write(fd, path, strlen(path)) < strlen(path)) {
			fprintf(stderr, "write to ctl %s: %r\n", path);
			exit(1);
		}
	}
	close(fd);

	if (opts.trace_set) {
		snprintf(path, sizeof(path), "/proc/%d/strace_traceset", pid);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			fprintf(stderr, "open %s: %r\n", path);
			exit(1);
		}
		if (write(fd, traceset_bm, COUNT_OF(traceset_bm))
		    < COUNT_OF(traceset_bm)) {
			fprintf(stderr, "write to strace_ignore: %r\n");
			exit(1);
		}
		close(fd);
	}

	snprintf(path, sizeof(path), "/proc/%d/strace", pid);
	fd = open(path, O_READ);
	if (!fd) {
		fprintf(stderr, "open %s: %r\n", path);
		exit(1);
	}

	if (opts.cmd_argc) {
		/* now that we've set up the tracing, we can run the process.  isn't it
		 * great that the process doesn't immediately start when you make it? */
		sys_proc_run(pid);
	}

	parse_traces(fd);
	return 0;
}
