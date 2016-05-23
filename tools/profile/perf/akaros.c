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

size_t ros_total_cores(void)
{
	return max_vcores() + ros_get_low_latency_core_count();
}

void ros_parse_cores(const char *str, struct core_set *cores)
{
	unsigned int fcpu, ncpu;
	char *dstr = xstrdup(str);
	char *sptr = NULL;
	char *tok, *sptr2;

	ZERO_DATA(*cores);
	for (tok = strtok_r(dstr, ":", &sptr); tok;
		 tok = strtok_r(NULL, ":", &sptr)) {
		bool neg_core = FALSE;

		if (*tok == '!') {
			neg_core = TRUE;
			tok++;
		}
		if (!strcmp(tok, "all")) {
			size_t max_cores = ros_total_cores();

			if (max_cores > MAX_NUM_CORES) {
				fprintf(stderr, "The number of system CPU exceeds the "
						"structure limits: num_cores=%u limits=%u\n", max_cores,
						CHAR_BIT * CORE_SET_SIZE);
				exit(1);
			}
			if (neg_core)
				memset(cores->core_set, 0,
					   DIV_ROUND_UP(max_cores, CHAR_BIT));
			else
				memset(cores->core_set, 0xff,
					   DIV_ROUND_UP(max_cores, CHAR_BIT));
		} else if (!strcmp(tok, "llall")) {
			ros_get_low_latency_core_set(cores);
		} else if (strchr(tok, '-')) {
			if (sscanf(tok, "%u-%u", &fcpu, &ncpu) != 2) {
				fprintf(stderr, "Invalid CPU range: %s\n", tok);
				exit(1);
			}
			if ((fcpu >= MAX_NUM_CORES) ||
				(ncpu >= MAX_NUM_CORES) || (fcpu > ncpu)) {
				fprintf(stderr, "CPU number out of bound: %u\n",
						fcpu);
				exit(1);
			}
			for (; fcpu <= ncpu; fcpu++) {
				if (neg_core)
					ros_clear_bit(cores->core_set, fcpu);
				else
					ros_set_bit(cores->core_set, fcpu);
			}
		} else {
			for (tok = strtok_r(tok, ".", &sptr2); tok;
				 tok = strtok_r(NULL, ".", &sptr2)) {
				fcpu = atoi(tok);
				if (fcpu >= MAX_NUM_CORES) {
					fprintf(stderr, "CPU number out of bound: %u\n",
							fcpu);
					exit(1);
				}
				if (neg_core)
					ros_clear_bit(cores->core_set, fcpu);
				else
					ros_set_bit(cores->core_set, fcpu);
			}
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
