/* Copyright (c) 2015-2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <argp.h>
#include <time.h>
#include <parlib/parlib.h>
#include <parlib/timing.h>
#include "xlib.h"
#include "akaros.h"
#include "perfconv.h"
#include "perf_core.h"

/* Helpers */
static void run_process_and_wait(int argc, char *argv[],
								 const struct core_set *cores);

/* For communicating with perf_create_context() */
static struct perf_context_config perf_cfg = {
	.perf_file = "#arch/perf",
	.kpctl_file = "#kprof/kpctl",
	.kpdata_file = "#kprof/kpdata",
};

static struct perfconv_context *cctx;
static struct perf_context *pctx;
extern char **environ;	/* POSIX envp */

struct perf_opts {
	FILE						*outfile;
	const char					*events;
	char						**cmd_argv;
	int							cmd_argc;
	struct core_set				cores;
	bool						got_cores;
	bool						verbose;
	bool						sampling;
	bool						stat_bignum;
	bool						record_quiet;
	unsigned long				record_period;
};
static struct perf_opts opts;

struct perf_cmd {
	char						*name;
	char						*desc;
	char						*opts;
	int (*func)(struct perf_cmd *, int, char **);
};

static int perf_help(struct perf_cmd *cmd, int argc, char *argv[]);
static int perf_list(struct perf_cmd *cmd, int argc, char *argv[]);
static int perf_record(struct perf_cmd *cmd, int argc, char *argv[]);
static int perf_stat(struct perf_cmd *cmd, int argc, char *argv[]);
static int perf_pmu_caps(struct perf_cmd *cmd, int argc, char *argv[]);

static struct perf_cmd perf_cmds[] = {
	{ .name = "help",
	  .desc = "Detailed help for commands",
	  .opts = "COMMAND",
	  .func = perf_help,
	},
	{ .name = "list",
	  .desc = "Lists all available events",
	  .opts = "[REGEX]",
	  .func = perf_list,
	},
	{ .name = "record",
	  .desc = "Samples events during command execution",
	  .opts = 0,
	  .func = perf_record,
	},
	{ .name = "stat",
	  .desc = "Counts events during command execution",
	  .opts = 0,
	  .func = perf_stat,
	},
	{ .name = "pmu_caps",
	  .desc = "Shows PMU capabilities",
	  .opts = "",
	  .func = perf_pmu_caps,
	},
};

/**************************** perf help ****************************/

static int perf_help(struct perf_cmd *cmd, int argc, char *argv[])
{
	char *sub_argv[2];

	if (argc < 2) {
		fprintf(stderr, "perf %s %s\n", cmd->name, cmd->opts);
		return -1;
	}
	for (int i = 0; i < COUNT_OF(perf_cmds); i++) {
		if (!strcmp(perf_cmds[i].name, argv[1])) {
			if (perf_cmds[i].opts) {
				fprintf(stdout, "perf %s %s\n", perf_cmds[i].name,
				        perf_cmds[i].opts);
				fprintf(stdout, "\t%s\n", perf_cmds[i].desc);
			} else {
				/* For argp subcommands, call their help directly. */
				sub_argv[0] = xstrdup(perf_cmds[i].name);
				sub_argv[1] = xstrdup("--help");
				perf_cmds[i].func(&perf_cmds[i], 2, sub_argv);
				free(sub_argv[0]);
				free(sub_argv[1]);
			}
			return 0;
		}
	}
	fprintf(stderr, "Unknown perf command %s\n", argv[1]);
	return -1;
}

/**************************** perf list ****************************/

static int perf_list(struct perf_cmd *cmd, int argc, char *argv[])
{
	char *show_regex = NULL;

	if (argc > 1)
		show_regex = argv[1];
	perf_show_events(show_regex, stdout);
	return 0;
}

/**************************** perf pmu_caps ************************/

static int perf_pmu_caps(struct perf_cmd *cmd, int argc, char *argv[])
{
	const struct perf_arch_info *pai = perf_context_get_arch_info(pctx);

	fprintf(stdout,
			"PERF.version             = %u\n"
			"PERF.proc_arch_events    = %u\n"
			"PERF.bits_x_counter      = %u\n"
			"PERF.counters_x_proc     = %u\n"
			"PERF.bits_x_fix_counter  = %u\n"
			"PERF.fix_counters_x_proc = %u\n",
			pai->perfmon_version, pai->proc_arch_events, pai->bits_x_counter,
			pai->counters_x_proc, pai->bits_x_fix_counter,
			pai->fix_counters_x_proc);
	return 0;
}

/**************************** Common argp ************************/

/* Collection argument parsing.  These options are common to any function that
 * will collect perf events, e.g. perf record and perf stat. */

static struct argp_option collect_opts[] = {
	{"event", 'e', "EVENT", 0, "Event string, e.g. cycles:u:k"},
	{"cores", 'C', "CORE_LIST", 0, "List of cores, e.g. 0.2.4:8-19"},
	{"cpu", 'C', 0, OPTION_ALIAS},
	{"all-cpus", 'a', 0, 0, "Collect events on all cores (on by default)"},
	{"verbose", 'v', 0, 0, 0},
	{ 0 }
};

static const char *collect_args_doc = "COMMAND [ARGS]";

static error_t parse_collect_opt(int key, char *arg, struct argp_state *state)
{
	struct perf_opts *p_opts = state->input;

	/* argp doesn't pass input to the child parser(s) by default... */
	state->child_inputs[0] = state->input;

	switch (key) {
	case 'a':
		/* Our default operation is to track all cores; we don't follow
		 * processes yet. */
		break;
	case 'C':
		parlib_parse_cores(arg, &p_opts->cores);
		p_opts->got_cores = TRUE;
		break;
	case 'e':
		p_opts->events = arg;
		break;
	case 'v':
		p_opts->verbose = TRUE;
		break;
	case ARGP_KEY_ARG:
		p_opts->cmd_argc = state->argc - state->next + 1;
		p_opts->cmd_argv = xmalloc(sizeof(char*) * (p_opts->cmd_argc + 1));
		p_opts->cmd_argv[0] = arg;
		memcpy(&p_opts->cmd_argv[1], &state->argv[state->next],
		       sizeof(char*) * (p_opts->cmd_argc - 1));
		p_opts->cmd_argv[p_opts->cmd_argc] = NULL;
		state->next = state->argc;
		break;
	case ARGP_KEY_END:
		if (!p_opts->cmd_argc)
			argp_usage(state);
		/* By default, we set all cores (different than linux) */
		if (!p_opts->got_cores)
			parlib_get_all_core_set(&p_opts->cores);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

/* Helper, parses args using the collect_opts and the child parser for a given
 * cmd. */
static void collect_argp(struct perf_cmd *cmd, int argc, char *argv[],
                         struct argp_child *children, struct perf_opts *opts)
{
	struct argp collect_opt = {collect_opts, parse_collect_opt,
	                           collect_args_doc, cmd->desc, children};
	char *cmd_name;
	const char *fmt = "perf %s";
	size_t cmd_sz = strlen(cmd->name) + strlen(fmt) + 1;

	/* Rewrite the command name from foo to perf foo for the --help output */
	cmd_name = xmalloc(cmd_sz);
	snprintf(cmd_name, cmd_sz, fmt, cmd->name);
	cmd_name[cmd_sz - 1] = '\0';
	argv[0] = cmd_name;
	argp_parse(&collect_opt, argc, argv, ARGP_IN_ORDER, 0, opts);
	/* It's possible that someone could still be using cmd_name */
}

/* Helper, submits the events in opts to the kernel for monitoring. */
static void submit_events(struct perf_opts *opts)
{
	struct perf_eventsel *sel;
	char *dup_evts, *tok, *tok_save = 0;

	dup_evts = xstrdup(opts->events);
	for (tok = strtok_r(dup_evts, ",", &tok_save);
	     tok;
		 tok = strtok_r(NULL, ",", &tok_save)) {

		sel = perf_parse_event(tok);
		PMEV_SET_INTEN(sel->ev.event, opts->sampling);
		sel->ev.trigger_count = opts->record_period;
		perf_context_event_submit(pctx, &opts->cores, sel);
	}
	free(dup_evts);
}

/**************************** perf record ************************/

static struct argp_option record_opts[] = {
	{"count", 'c', "PERIOD", 0, "Sampling period"},
	{"output", 'o', "FILE", 0, "Output file name (default perf.data)"},
	{"freq", 'F', "FREQUENCY", 0, "Sampling frequency (assumes cycles)"},
	{"call-graph", 'g', 0, 0, "Backtrace recording (always on!)"},
	{"quiet", 'q', 0, 0, "No printing to stdio"},
	{ 0 }
};

/* In lieu of adaptively changing the period to maintain a set freq, we
 * just assume they want cycles and that the TSC is close to that.
 *
 * (cycles/sec) / (samples/sec) = cycles / sample = period.
 *
 * TODO: this also assumes we're running the core at full speed. */
static unsigned long freq_to_period(unsigned long freq)
{
	return get_tsc_freq() / freq;
}

static error_t parse_record_opt(int key, char *arg, struct argp_state *state)
{
	struct perf_opts *p_opts = state->input;

	switch (key) {
	case 'c':
		if (p_opts->record_period)
			argp_error(state, "Period set.  Only use at most one of -c -F");
		p_opts->record_period = atol(arg);
		break;
	case 'F':
		if (p_opts->record_period)
			argp_error(state, "Period set.  Only use at most one of -c -F");
		/* TODO: when we properly support freq, multiple events will have the
		 * same freq but different, dynamic, periods. */
		p_opts->record_period = freq_to_period(atol(arg));
		break;
	case 'g':
		/* Our default operation is to record backtraces. */
		break;
	case 'o':
		p_opts->outfile = xfopen(arg, "wb");
		break;
	case 'q':
		p_opts->record_quiet = TRUE;
		break;
	case ARGP_KEY_END:
		if (!p_opts->events)
			p_opts->events = "cycles";
		if (!p_opts->outfile)
			p_opts->outfile = xfopen("perf.data", "wb");
		if (!p_opts->record_period)
			p_opts->record_period = freq_to_period(1000);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static int perf_record(struct perf_cmd *cmd, int argc, char *argv[])
{
	struct argp argp_record = {record_opts, parse_record_opt};
	struct argp_child children[] = { {&argp_record, 0, 0, 0}, {0} };

	collect_argp(cmd, argc, argv, children, &opts);
	opts.sampling = TRUE;

	/* Once a perf event is submitted, it'll start counting and firing the IRQ.
	 * However, we can control whether or not the samples are collected. */
	submit_events(&opts);
	perf_start_sampling(pctx);
	run_process_and_wait(opts.cmd_argc, opts.cmd_argv,
	                     opts.got_cores ? &opts.cores : NULL);
	perf_stop_sampling(pctx);
	if (opts.verbose)
		perf_context_show_events(pctx, stdout);
	/* The events are still counting and firing IRQs.  Let's be nice and turn
	 * them off to minimize our impact. */
	perf_stop_events(pctx);
	/* Generate the Linux perf file format with the traces which have been
	 * created during this operation. */
	perf_convert_trace_data(cctx, perf_cfg.kpdata_file, opts.outfile);
	fclose(opts.outfile);
	return 0;
}

/**************************** perf stat  ************************/

static struct argp_option stat_opts[] = {
	{"big-num", 'B', 0, 0, "Formatting option"},
	{"output", 'o', "FILE", 0, "Print output to file (default stdout)"},
	{ 0 }
};

static error_t parse_stat_opt(int key, char *arg, struct argp_state *state)
{
	struct perf_opts *p_opts = state->input;

	switch (key) {
	case 'B':
		p_opts->stat_bignum = TRUE;
		break;
	case 'o':
		p_opts->outfile = xfopen(arg, "w");
		break;
	case ARGP_KEY_END:
		if (!p_opts->events)
			p_opts->events = "cache-misses,cache-references,"
			                 "branch-misses,branches,instructions,cycles";
		if (!p_opts->outfile)
			p_opts->outfile = stdout;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

struct stat_val {
	char						*name;
	uint64_t					count;
};

/* Helper, given a name, fetches its value as a float. */
static float get_count_for(char *name, struct stat_val *all_vals,
                           size_t nr_vals)
{
	for (int i = 0; i < nr_vals; i++) {
		if (!strcmp(name, all_vals[i].name))
			return (float)all_vals[i].count;
	}
	return 0.0;
}

/* Helper, gets the seconds count as a float */
static float get_seconds(struct stat_val *all_vals, size_t nr_vals)
{
	float sec = get_count_for("nsec", all_vals, nr_vals) / 1000000000;

	/* We should never have a time of 0, but in case something went wrong, don't
	 * hand back 0 (divide by 0 errors). */
	return sec != 0.0 ? sec : 1.0;
}

/* Prints "X per second", scaling for K, M, or G. */
static void print_default_rate(FILE *out, struct stat_val *val,
                               struct stat_val *all_vals, size_t nr_vals)
{
	float rate = val->count / get_seconds(all_vals, nr_vals);
	char scale = ' ';

	if (rate > 1000000000) {
		rate /= 1000000000;
		scale = 'G';
	} else if (rate > 1000000) {
		rate /= 1000000;
		scale = 'M';
	} else if (rate > 1000) {
		rate /= 1000;
		scale = 'K';
	}
	fprintf(out, "%9.3f %c/sec\n", rate, scale);
}

/* Prints a line for the given stat val.  We pass all the vals since some stats
 * will adjust their output based on *other* known values.  e.g. IPC. */
static void stat_print_val(FILE *out, struct stat_val *val,
                           struct stat_val *all_vals, size_t nr_vals)
{
	/* Everyone gets the same front part of the printout */
	fprintf(out, "%18llu      %-25s #", val->count, val->name);

	/* Based on the particular event and what other events we know, we may print
	 * something different to the summary bit after the #. */
	if (!strcmp(val->name, "instructions")) {
		float cycles = get_count_for("cycles", all_vals, nr_vals);

		if (cycles != 0.0)
			fprintf(out, "%9.3f insns per cycle\n", val->count / cycles);
		else
			print_default_rate(out, val, all_vals, nr_vals);
	} else if (!strcmp(val->name, "cache-misses")) {
		float cache_ref = get_count_for("cache-references", all_vals, nr_vals);

		if (cache_ref != 0.0)
			fprintf(out, "%8.2f%% of all refs\n", val->count * 100 / cache_ref);
		else
			print_default_rate(out, val, all_vals, nr_vals);
	} else if (!strcmp(val->name, "branch-misses")) {
		float branches = get_count_for("branches", all_vals, nr_vals);

		if (branches != 0.0)
			fprintf(out, "%8.2f%% of all branches\n",
			        val->count * 100 / branches);
		else
			print_default_rate(out, val, all_vals, nr_vals);
	} else {
		print_default_rate(out, val, all_vals, nr_vals);
	}
}

static char *cmd_as_str(int argc, char *const argv[])
{
	size_t len = 0;
	char *str;

	for (int i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	str = xzmalloc(len);
	for (int i = 0; i < argc; i++) {
		strlcat(str, argv[i], len);
		if (i != argc - 1)
			strlcat(str, " ", len);
	}
	return str;
}

static struct stat_val *collect_stats(struct perf_context *pctx,
                                      struct timespec *diff)
{
	struct stat_val *stat_vals;

	/* the last stat is time (nsec). */
	stat_vals = xzmalloc(sizeof(struct stat_val) * (pctx->event_count + 1));
	for (int i = 0; i < pctx->event_count; i++) {
		stat_vals[i].count = perf_get_event_count(pctx, i);
		stat_vals[i].name = pctx->events[i].sel.fq_str;
	}
	stat_vals[pctx->event_count].name = "nsec";
	stat_vals[pctx->event_count].count = diff->tv_sec * 1000000000 +
	                                     diff->tv_nsec;
	return stat_vals;
}

static int perf_stat(struct perf_cmd *cmd, int argc, char *argv[])
{
	struct argp argp_stat = {stat_opts, parse_stat_opt};
	struct argp_child children[] = { {&argp_stat, 0, 0, 0}, {0} };
	FILE *out;
	struct timespec start, end, diff;
	struct stat_val *stat_vals;
	char *cmd_string;

	collect_argp(cmd, argc, argv, children, &opts);
	opts.sampling = FALSE;
	out = opts.outfile;

	/* As soon as we submit one event, that event is being tracked, meaning that
	 * the setup/teardown of perf events is also tracked.  Each event (including
	 * the clock measurement) will roughly account for either the start or stop
	 * of every other event. */
	clock_gettime(CLOCK_REALTIME, &start);
	submit_events(&opts);
	run_process_and_wait(opts.cmd_argc, opts.cmd_argv,
	                     opts.got_cores ? &opts.cores : NULL);
	clock_gettime(CLOCK_REALTIME, &end);
	subtract_timespecs(&diff, &end, &start);
	stat_vals = collect_stats(pctx, &diff);
	perf_stop_events(pctx);
	cmd_string = cmd_as_str(opts.cmd_argc, opts.cmd_argv);
	fprintf(out, "\nPerformance counter stats for '%s':\n\n", cmd_string);
	free(cmd_string);
	for (int i = 0; i < pctx->event_count; i++)
		stat_print_val(out, &stat_vals[i], stat_vals, pctx->event_count + 1);
	fprintf(out, "\n%8llu.%09llu seconds time elapsed\n\n", diff.tv_sec,
	        diff.tv_nsec);
	fclose(out);
	free(stat_vals);
	return 0;
}

static void run_process_and_wait(int argc, char *argv[],
								 const struct core_set *cores)
{
	int pid, status;
	size_t max_cores = parlib_nr_total_cores();
	struct core_set pvcores;

	pid = create_child_with_stdfds(argv[0], argc, argv, environ);
	if (pid < 0) {
		perror("Unable to spawn child");
		fflush(stderr);
		exit(1);
	}
	if (cores) {
		parlib_get_ll_core_set(&pvcores);
		parlib_not_core_set(&pvcores);
		parlib_and_core_sets(&pvcores, cores);
		for (size_t i = 0; i < max_cores; i++) {
			if (parlib_get_core(&pvcores, i)) {
				if (sys_provision(pid, RES_CORES, i)) {
					fprintf(stderr,
							"Unable to provision CPU %lu to PID %d: cmd='%s'\n",
							i, pid, argv[0]);
					sys_proc_destroy(pid, -1);
					exit(1);
				}
			}
		}
	}
	sys_proc_run(pid);
	waitpid(pid, &status, 0);
}

static void save_cmdline(int argc, char *argv[])
{
	size_t len = 0;
	char *p;

	for (int i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	cmd_line_save = xmalloc(len);
	p = cmd_line_save;
	for (int i = 0; i < argc; i++) {
		strcpy(p, argv[i]);
		p += strlen(argv[i]);
		if (!(i == argc - 1)) {
			*p = ' ';	/* overwrite \0 with ' ' */
			p++;
		}
	}
}

static void global_usage(void)
{
	fprintf(stderr, "  Usage: perf COMMAND [ARGS]\n");
	fprintf(stderr, "\n  Available commands:\n\n");
	for (int i = 0; i < COUNT_OF(perf_cmds); i++)
		fprintf(stderr, "  \t%s: %s\n", perf_cmds[i].name, perf_cmds[i].desc);
	exit(-1);
}

int main(int argc, char *argv[])
{
	int i, ret = -1;

	save_cmdline(argc, argv);

	/* Common inits.  Some functions don't need these, but it doesn't hurt. */
	perf_initialize();
	pctx = perf_create_context(&perf_cfg);
	cctx = perfconv_create_context(pctx);

	if (argc < 2)
		global_usage();
	for (i = 0; i < COUNT_OF(perf_cmds); i++) {
		if (!strcmp(perf_cmds[i].name, argv[1])) {
			ret = perf_cmds[i].func(&perf_cmds[i], argc - 1, argv + 1);
			break;
		}
	}
	if (i == COUNT_OF(perf_cmds))
		global_usage();
	/* This cleanup is optional - they'll all be dealt with when the program
	 * exits.  This means its safe for us to exit(-1) at any point in the
	 * program. */
	perf_free_context(pctx);
	perfconv_free_context(cctx);
	perf_finalize();
	return ret;
}
