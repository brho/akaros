/* Copyright (c) 2015 Google Inc
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
#include <parlib/parlib.h>
#include "xlib.h"
#include "akaros.h"
#include "perfconv.h"
#include "perf_core.h"

static struct perf_context_config perf_cfg = {
	.perf_file = "#arch/perf",
	.kpctl_file = "#kprof/kpctl",
};

static void usage(const char *prg)
{
	fprintf(stderr,
			"Use: %s {list,cpucaps,record} [-mkecxoKh] -- CMD [ARGS ...]\n"
			"\tlist            Lists all the available events and their meaning.\n"
			"\tcpucaps         Shows the system CPU capabilities in term of "
			"performance counters.\n"
			"\trecord           Setups the configured counters, runs CMD, and "
			"shows the values of the counters.\n"
			"Options:\n"
			"\t-m PATH          Sets the path of the PERF file ('%s').\n"
			"\t-k PATH          Sets the path of the KPROF control file "
			"('%s').\n"
			"\t-e EVENT_SPEC    Adds an event to be tracked.\n"
			"\t-c CPUS_STR      Selects the CPU set on which the counters "
			"should be active.\n"
			"\t                 The following format is supported for the CPU "
			"set:\n"
			"\t                   !      = Negates the following set\n"
			"\t                   all    = Enable all CPUs\n"
			"\t                   llall  = Enable all low latency CPUs\n"
			"\t                   I.J.K  = Enable CPUs I, J, and K\n"
			"\t                   N-M    = Enable CPUs from N to M, included\n"
			"\t                 Examples: all:!3.4.7  0-15:!3.5.7\n"
			"\t-x EVENT_RX      Sets the event name regular expression for "
			"list.\n"
			"\t-o PATH          Sets the perf output file path ('perf.data').\n"
			"\t-K PATH          Sets the kprof data file path ('#kprof/kpdata').\n"
			"\t-h               Displays this help screen.\n", prg,
			perf_cfg.perf_file, perf_cfg.kpctl_file);
	exit(1);
}

static void show_perf_arch_info(const struct perf_arch_info *pai, FILE *file)
{
	fprintf(file,
			"PERF.version             = %u\n"
			"PERF.proc_arch_events    = %u\n"
			"PERF.bits_x_counter      = %u\n"
			"PERF.counters_x_proc     = %u\n"
			"PERF.bits_x_fix_counter  = %u\n"
			"PERF.fix_counters_x_proc = %u\n",
			pai->perfmon_version, pai->proc_arch_events, pai->bits_x_counter,
			pai->counters_x_proc, pai->bits_x_fix_counter,
			pai->fix_counters_x_proc);
}

static void run_process_and_wait(int argc, char *argv[],
								 const struct core_set *cores)
{
	int pid, status;
	size_t max_cores = ros_total_cores();
	struct core_set pvcores;

	pid = sys_proc_create(argv[0], strlen(argv[0]), argv, NULL, 0);
	if (pid < 0) {
		perror(argv[0]);
		exit(1);
	}

	ros_get_low_latency_core_set(&pvcores);
	ros_not_core_set(&pvcores);
	ros_and_core_sets(&pvcores, cores);
	for (size_t i = 0; i < max_cores; i++) {
		if (ros_get_bit(&pvcores, i)) {
			if (sys_provision(pid, RES_CORES, i)) {
				fprintf(stderr,
						"Unable to provision CPU %lu to PID %d: cmd='%s'\n",
						i, pid, argv[0]);
				exit(1);
			}
		}
	}

	sys_proc_run(pid);
	waitpid(pid, &status, 0);
}

int main(int argc, char *argv[])
{
	int i, icmd = -1, num_events = 0;
	const char *cmd = argv[1], *show_rx = NULL;
	const char *kpdata_file = "#kprof/kpdata", *outfile = "perf.data";
	struct perfconv_context *cctx;
	struct perf_context *pctx;
	struct core_set cores;
	const char *events[MAX_CPU_EVENTS];

	ros_get_all_cores_set(&cores);
	cctx = perfconv_create_context();

	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-m")) {
			if (++i < argc)
				perf_cfg.perf_file = argv[i];
		} else if (!strcmp(argv[i], "-k")) {
			if (++i < argc)
				perf_cfg.kpctl_file = argv[i];
		} else if (!strcmp(argv[i], "-e")) {
			if (++i < argc) {
				if (num_events >= MAX_CPU_EVENTS) {
					fprintf(stderr, "Too many events: %d\n", num_events);
					return 1;
				}
				events[num_events++] = argv[i];
			}
		} else if (!strcmp(argv[i], "-x")) {
			if (++i < argc)
				show_rx = argv[i];
		} else if (!strcmp(argv[i], "-c")) {
			if (++i < argc)
				ros_parse_cores(argv[i], &cores);
		} else if (!strcmp(argv[i], "-o")) {
			if (++i < argc)
				outfile = argv[i];
		} else if (!strcmp(argv[i], "-K")) {
			if (++i < argc)
				kpdata_file = argv[i];
		} else if (!strcmp(argv[i], "--")) {
			icmd = i + 1;
			break;
		} else {
			usage(argv[0]);
		}
	}
	if (!cmd)
		usage(argv[0]);

	perf_initialize(argc, argv);
	pctx = perf_create_context(&perf_cfg);

	if (!strcmp(cmd, "list")) {
		perf_show_events(show_rx, stdout);
	} else if (!strcmp(cmd, "cpucaps")) {
		show_perf_arch_info(perf_context_get_arch_info(pctx), stdout);
	} else if (!strcmp(cmd, "record")) {
		if (icmd < 0)
			usage(argv[0]);

		for (i = 0; i < num_events; i++) {
			struct perf_eventsel sel;

			perf_parse_event(events[i], &sel);
			perf_context_event_submit(pctx, &cores, &sel);
		}

		if (!strcmp(argv[icmd], "sleep") && (icmd + 1) < argc)
			sleep(atoi(argv[icmd + 1]));
		else
			run_process_and_wait(argc - icmd, argv + icmd, &cores);

		perf_context_show_values(pctx, stdout);

		/* Flush the profiler per-CPU trace data into the main queue, so that
		 * it will be available for read.
		 */
		perf_flush_context_traces(pctx);

		/* Generate the Linux perf file format with the traces which have been
		 * created during this operation.
		 */
		perf_convert_trace_data(cctx, kpdata_file, outfile);
	} else {
		usage(argv[0]);
	}
	perf_free_context(pctx);
	perfconv_free_context(cctx);
	perf_finalize();

	return 0;
}
