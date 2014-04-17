/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace functions for various measurements.
 *
 * For now, this is built into parlib.  We can pull it out in the future.  Many
 * of the larger functions are in flux (interfaces, options, etc). */

#include <ros/common.h>

/* Basic stats computation and printing.
 *
 * All of these expect a 2D collection of samples, where the first array is an
 * array of arrays of samples.  The first array's members are something like
 * per-thread samples, where each thread fills in its
 * data[thread_id][sample_number].  The samples should be ordered in
 * chronological order.  Ultimately, the sample needs to produce a uint64_t
 * (e.g. TSC tick). */

struct sample_stats {
	int (*get_sample)(void **data, int i, int j, uint64_t *sample);
	uint64_t					avg_time;
	uint64_t					var_time;
	uint64_t					max_time;
	uint64_t					min_time;
	unsigned int				lat_50;
	unsigned int				lat_75;
	unsigned int				lat_90;
	unsigned int				lat_99;
	uint64_t					total_samples;
};

/* Computes basic stats and prints histograms, stats returned via *stats */
void compute_stats(void **data, int nr_i, int nr_j, struct sample_stats *stats);

/* Prints the throughput of events in **data */
void print_throughput(void **data, unsigned int nr_steps, uint64_t interval,
                      unsigned int nr_print_steps, uint64_t start_time,
                      int nr_i, int nr_j,
                      int (*get_sample)(void **data, int i, int j,
                                        uint64_t *sample));
