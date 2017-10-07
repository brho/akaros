/* Copyright (c) 2016-7 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Usage: turbo [reset]
 *
 * This will print the turbo ratio since the last reset for each core.
 *
 * x86 only (TODO) */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <parlib/sysinfo.h>
#include <parlib/arch/arch.h>
#include <ros/arch/msr-index.h>
#include <argp.h>

static const char doc[] = "turbo -- control for turbo mode";
static const char args_doc[] = "";

static struct argp_option options[] = {
	{"enable",		'e', 0, 0, "Enable turbo mode"},
	{"disable",		'd', 0, 0, "Disable turbo mode"},
	{"status",		's', 0, 0, "Print status of turbo mode"},
	{0, 0, 0, 0, ""},
	{"ratio",		'r', 0, 0, "Print the experienced turbo ratio"},
	{"zero",		'z', 0, 0, "Zero the turbo ratio"},
	{NULL,			'h', 0, OPTION_HIDDEN, NULL},
	{ 0 }
};

#define PROG_CMD_ENABLE			1
#define PROG_CMD_DISABLE		2
#define PROG_CMD_STATUS			3
#define PROG_CMD_PRINT_RATIO	4
#define PROG_CMD_ZERO_RATIO		5

struct prog_opts {
	int							cmd;
};

static int num_cores;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct prog_opts *p_opts = state->input;

	switch (key) {
	case 'e':
		if (p_opts->cmd) {
			fprintf(stderr, "Too many commands; just one allowed.\n\n");
			argp_usage(state);
		}
		p_opts->cmd = PROG_CMD_ENABLE;
		break;
	case 'd':
		if (p_opts->cmd) {
			fprintf(stderr, "Too many commands; just one allowed.\n\n");
			argp_usage(state);
		}
		p_opts->cmd = PROG_CMD_DISABLE;
		break;
	case 's':
		if (p_opts->cmd) {
			fprintf(stderr, "Too many commands; just one allowed.\n\n");
			argp_usage(state);
		}
		p_opts->cmd = PROG_CMD_STATUS;
		break;
	case 'r':
		if (p_opts->cmd) {
			fprintf(stderr, "Too many commands; just one allowed.\n\n");
			argp_usage(state);
		}
		p_opts->cmd = PROG_CMD_PRINT_RATIO;
		break;
	case 'z':
		if (p_opts->cmd) {
			fprintf(stderr, "Too many commands; just one allowed.\n\n");
			argp_usage(state);
		}
		p_opts->cmd = PROG_CMD_ZERO_RATIO;
		break;
	case ARGP_KEY_ARG:
		fprintf(stderr, "Extra arguments\n");
		argp_usage(state);
		break;
	case ARGP_KEY_END:
		if (!p_opts->cmd)
			argp_usage(state);
		break;
	case 'h':
		argp_state_help(state, stderr, ARGP_HELP_LONG);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	};
	return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

static int get_msr_fd(void)
{
	int fd;

	fd = open("#arch/msr", O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}
	return fd;
}

static int set_turbo_mode(bool enable)
{
	size_t buf_sz;
	ssize_t ret;
	int fd;
	uint64_t *buf;
	uint64_t msr_val;

	fd = get_msr_fd();
	buf_sz = num_cores * sizeof(uint64_t);
	buf = malloc(buf_sz);
	assert(buf);

	ret = pread(fd, buf, buf_sz, MSR_IA32_PERF_CTL);
	if (ret < 0) {
		perror("pread MSR_PERF_CTL");
		exit(-1);
	}
	/* The assumption here is that all cores have the same MSR value.  Changing
	 * this would require changing the wrmsr kernel interface. */
	msr_val = buf[0];
	if (enable)
		msr_val &= ~(1ULL << 32);
	else
		msr_val |= 1ULL << 32;
	ret = pwrite(fd, &msr_val, sizeof(msr_val), MSR_IA32_PERF_CTL);
	if (ret < 0) {
		perror("pwrite MSR_PERF_CTL");
		exit(-1);
	}
	printf("%s turbo mode for all cores\n", enable ? "Enabled" : "Disabled");
	free(buf);
	close(fd);
	return 0;
}

static int print_turbo_status(void)
{
	size_t buf_sz;
	ssize_t ret;
	int fd;
	uint64_t *buf;
	uint64_t msr_val;

	fd = get_msr_fd();
	buf_sz = num_cores * sizeof(uint64_t);
	buf = malloc(buf_sz);
	assert(buf);

	ret = pread(fd, buf, buf_sz, MSR_IA32_PERF_CTL);
	if (ret < 0) {
		perror("pread MSR_PERF_CTL");
		exit(-1);
	}
	/* The assumption here is that all cores have the same MSR value.  Changing
	 * this would require changing the wrmsr kernel interface. */
	msr_val = buf[0];
	printf("Turbo mode is %s for all cores\n", msr_val & (1ULL << 32) ?
	                                           "disabled" : "enabled");
	free(buf);
	close(fd);
	return 0;
}

static void check_for_ratio_msrs(void)
{
	uint32_t ecx;

	parlib_cpuid(0x6, 0, NULL, NULL, &ecx, NULL);
	if (!(ecx & (1 << 0))) {
		fprintf(stderr, "cpuid says no MPERF and APERF\n");
		exit(-1);
	}
}

static int print_turbo_ratio(void)
{
	size_t buf_sz;
	ssize_t ret;
	int fd;
	uint64_t *mperf_buf, *aperf_buf;

	check_for_ratio_msrs();

	fd = get_msr_fd();
	buf_sz = num_cores * sizeof(uint64_t);
	mperf_buf = malloc(buf_sz);
	aperf_buf = malloc(buf_sz);
	assert(mperf_buf && aperf_buf);
	/* ideally these reads happen with no interference/interrupts in between */
	ret = pread(fd, mperf_buf, buf_sz, MSR_IA32_MPERF);
	if (ret < 0) {
		perror("pread MSR_MPERF");
		exit(-1);
	}
	ret = pread(fd, aperf_buf, buf_sz, MSR_IA32_APERF);
	if (ret < 0) {
		perror("pread MSR_APERF");
		exit(-1);
	}
	for (int i = 0; i < num_cores; i++)
		printf("Core %3d: %4f%\n", i, 100.0 * aperf_buf[i] / mperf_buf[i]);
	free(mperf_buf);
	free(aperf_buf);
	close(fd);
	return 0;
}

static int zero_turbo_ratio(void)
{
	uint64_t reset_val = 0;
	ssize_t ret;
	int fd;

	check_for_ratio_msrs();

	fd = get_msr_fd();
	ret = pwrite(fd, &reset_val, sizeof(reset_val), MSR_IA32_MPERF);
	if (ret < 0) {
		perror("pwrite MSR_MPERF");
		exit(-1);
	}
	ret = pwrite(fd, &reset_val, sizeof(reset_val), MSR_IA32_APERF);
	if (ret < 0) {
		perror("pwrite MSR_APERF");
		exit(-1);
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	struct prog_opts popts = {0};

	argp_parse(&argp, argc, argv, 0, 0, &popts);
	/* TODO: could use a core list or something in the future (like perf) */
	num_cores = get_num_pcores();

	switch (popts.cmd) {
	case PROG_CMD_ENABLE:
		return set_turbo_mode(TRUE);
	case PROG_CMD_DISABLE:
		return set_turbo_mode(FALSE);
	case PROG_CMD_STATUS:
		return print_turbo_status();
	case PROG_CMD_PRINT_RATIO:
		return print_turbo_ratio();
	case PROG_CMD_ZERO_RATIO:
		return zero_turbo_ratio();
	default:
		fprintf(stderr, "Unhandled options (argp should catch this)!\n");
		return -1;
	};
}
