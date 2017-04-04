/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <ros/arch/arch.h>
#include <ros/common.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <parlib/parlib.h>
#include "xlib.h"
#include "akaros.h"

static const unsigned int llcores[] = {
	0
};

void ros_get_low_latency_core_set(struct core_set *cores)
{
	for (size_t i = 0; i < COUNT_OF(llcores); i++)
		ros_set_bit(cores, llcores[i]);
}

size_t ros_get_low_latency_core_count(void)
{
	return COUNT_OF(llcores);
}

static int guess_nr_cores(void)
{
	return max_vcores() + ros_get_low_latency_core_count();
}

static int get_vars_nr_cores(void)
{
	int fd, ret;
	char buf[10];

	fd = xopen("#vars/num_cores!dw", O_READ, 0);
	if (fd < 0)
		return -1;
	if (read(fd, buf, sizeof(buf)) <= 0) {
		close(fd);
		return -1;
	}
	ret = atoi(buf);
	return ret;
}

static int nr_cores;

static void set_nr_cores(void *arg)
{
	nr_cores = get_vars_nr_cores();
	if (nr_cores == -1)
		nr_cores = guess_nr_cores();
}

size_t ros_total_cores(void)
{
	static parlib_once_t once = PARLIB_ONCE_INIT;

	parlib_run_once(&once, set_nr_cores, NULL);
	return nr_cores;
}

void ros_parse_cores(const char *str, struct core_set *cores)
{
	unsigned int fcpu, ncpu;
	char *dstr = xstrdup(str);
	char *sptr = NULL;
	char *tok, *sptr2;

	ZERO_DATA(*cores);
	for (tok = strtok_r(dstr, ",", &sptr); tok;
		 tok = strtok_r(NULL, ",", &sptr)) {

		if (strchr(tok, '-')) {
			if (sscanf(tok, "%u-%u", &fcpu, &ncpu) != 2) {
				fprintf(stderr, "Invalid CPU range: %s\n", tok);
				exit(1);
			}
			if (fcpu >= ros_total_cores()) {
				fprintf(stderr, "CPU number out of bound: %u\n", fcpu);
				exit(1);
			}
			if (ncpu >= ros_total_cores()) {
				fprintf(stderr, "CPU number out of bound: %u\n", ncpu);
				exit(1);
			}
			if (fcpu > ncpu) {
				fprintf(stderr, "CPU range is backwards: %u-%u\n", fcpu, ncpu);
				exit(1);
			}
			for (; fcpu <= ncpu; fcpu++)
				ros_set_bit(cores->core_set, fcpu);
		} else {
			fcpu = atoi(tok);
			if (fcpu >= ros_total_cores()) {
				fprintf(stderr, "CPU number out of bound: %u\n",
						fcpu);
				exit(1);
			}
			ros_set_bit(cores->core_set, fcpu);
		}
	}
	free(dstr);
}

void ros_get_all_cores_set(struct core_set *cores)
{
	size_t max_cores = ros_total_cores();

	memset(cores->core_set, 0xff, DIV_ROUND_UP(max_cores, CHAR_BIT));
}

void ros_not_core_set(struct core_set *dcs)
{
	size_t max_cores = ros_total_cores();

	for (size_t i = 0; (max_cores > 0) && (i < sizeof(dcs->core_set)); i++) {
		size_t nb = (max_cores >= CHAR_BIT) ? CHAR_BIT : max_cores;

		dcs->core_set[i] = (~dcs->core_set[i]) & ((1 << nb) - 1);
		max_cores -= nb;
	}
}

void ros_and_core_sets(struct core_set *dcs, const struct core_set *scs)
{
	for (size_t i = 0; i < sizeof(dcs->core_set); i++)
		dcs->core_set[i] &= scs->core_set[i];
}

void ros_or_core_sets(struct core_set *dcs, const struct core_set *scs)
{
	for (size_t i = 0; i < sizeof(dcs->core_set); i++)
		dcs->core_set[i] |= scs->core_set[i];
}
