/* Copyright (c) 2013 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Userspace functions for various measurements.
 *
 * For now, this is built into parlib.  We can pull it out in the future.  Many
 * of the larger functions are in flux. */

#include <ros/common.h>
#include <tsc-compat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <measure.h>

/* Basic stats computation and printing.
 *
 * All of these expect a 2D collection of samples, where the first array is an
 * array of arrays of samples.  The first array's members are something like
 * per-thread samples, where each thread fills in its
 * data[thread_id][sample_number].  The samples should be ordered in
 * chronological order.  Ultimately, the sample needs to produce a uint64_t
 * (e.g. TSC tick). */

static void init_stats(struct sample_stats *stats)
{
	stats->avg_time = 0;
	stats->var_time = 0;
	stats->max_time = 0;
	stats->min_time = UINT64_MAX;
	stats->lat_50 = 0;
	stats->lat_75 = 0;
	stats->lat_90 = 0;
	stats->lat_99 = 0;
	stats->total_samples = 0;
}

/* Could have options for printing for how many rows we want, how much we want
 * to trim the max/min, and how many samples per bin. */
void compute_stats(void **data, int nr_i, int nr_j, struct sample_stats *stats)
{
	uint64_t sample_time, hist_max_time, hist_min_time,
	         lat_max_time, lat_min_time;
	size_t hist_idx, lat_idx, hist_bin_sz, lat_bin_sz;
	float accum_samples = 0.0, coef_var;
	unsigned int *hist_times;
	unsigned int *lat_times;
	unsigned int nr_hist_bins = 75;		/* looks reasonable when printed */
	unsigned int nr_lat_bins = 500;		/* affects granularity of lat perc */
	unsigned int max_dots_per_row = 45;	/* looks good with 80-wide col */
	unsigned int max_hist_bin = 0;
	#define HI_COEF_VAR 1.0

	init_stats(stats);
	/* First pass, figure out the min, max, avg, etc. */
	for (int i = 0; i < nr_i; i++) {
		for (int j = 0; j < nr_j; j++) {
			/* get_sample returns 0 on success.  o/w, skip the sample */
			if (stats->get_sample(data, i, j, &sample_time))
				continue;	/* depending on semantics, we could break */
			stats->total_samples++;
			stats->avg_time += sample_time;
			stats->max_time = sample_time > stats->max_time ? sample_time
			                                                : stats->max_time;
			stats->min_time = sample_time < stats->min_time ? sample_time
			                                                : stats->min_time;
		}
	}
	if (stats->total_samples < 2) {
		printf("Not enough samples (%d) for avg and var\n",
		       stats->total_samples);
		return;
	}
	stats->avg_time /= stats->total_samples;
	/* Second pass, compute the variance.  Want to do this before the
	 * histograms, so we can trim the serious outliers */
	for (int i = 0; i < nr_i; i++) {
		for (int j = 0; j < nr_j; j++) {
			if (stats->get_sample(data, i, j, &sample_time))
				continue;
			/* var: (sum_i=1..n { (x_i - xbar)^2 }) / (n - 1) */
			stats->var_time += (sample_time - stats->avg_time) *
			                   (sample_time - stats->avg_time);
		}
	}
	stats->var_time /= stats->total_samples - 1;
	/* We have two histogram structures.  The main one is for printing, and the
	 * other is for computing latency percentiles.  The only real diff btw the
	 * two is the number of bins.  The latency one has a lot more, for finer
	 * granularity, and the regular one has fewer for better printing.
	 *
	 * Both have the same max and min bin values.  Any excesses get put in the
	 * smallest or biggest bin.  This keeps the granularity reasonable in the
	 * face of very large outliers.  Normally, I trim off anything outside 3
	 * stddev.
	 * 
	 * High variation will throw off our histogram bins, so we adjust.  A
	 * coef_var > 1 is considered high variance.  The numbers I picked are just
	 * heuristics to catch SMM interference and make the output look nice. */
	coef_var = sqrt(stats->var_time) / stats->avg_time;
	if (coef_var > HI_COEF_VAR) {
		hist_max_time = stats->avg_time * 3;
		hist_min_time = stats->avg_time / 3;
	} else {	/* 'normal' data */
		/* trimming the printable hist at 3 stddevs, which for normal data is
		 * 99.7% of the data.  For most any data, it gets 89% (Chebyshev's
		 * inequality) */
		hist_max_time = stats->avg_time + 3 * sqrt(stats->var_time);
		hist_min_time = stats->avg_time - 3 * sqrt(stats->var_time);
		if (hist_min_time > hist_max_time)
			hist_min_time = 0;
	}
	lat_max_time = hist_max_time;
	lat_min_time = hist_min_time;
	hist_bin_sz = (hist_max_time - hist_min_time) / nr_hist_bins + 1;
	lat_bin_sz = (lat_max_time - lat_min_time) / nr_lat_bins + 1;
	hist_times = malloc(sizeof(unsigned int) * nr_hist_bins);
	lat_times = malloc(sizeof(unsigned int) * nr_lat_bins);
	if (!hist_times || !lat_times) {
		perror("compute_stats failed to alloc hist/lat arrays:");
		free(hist_times);
		free(lat_times);
		return;
	}
	memset(hist_times, 0, sizeof(unsigned int) * nr_hist_bins);
	memset(lat_times, 0, sizeof(unsigned int) * nr_lat_bins);
	/* third pass, fill the bins for the histogram and latencies */
	for (int i = 0; i < nr_i; i++) {
		for (int j = 0; j < nr_j; j++) {
			if (stats->get_sample(data, i, j, &sample_time))
				continue;
			/* need to shift, offset by min_time.  anything too small is 0 and
			 * will go into the first bin.  anything too large will go into the
			 * last bin. */
			lat_idx = sample_time < lat_min_time
			          ? 0
			          : (sample_time - lat_min_time) / lat_bin_sz;
			lat_idx = MIN(lat_idx, nr_lat_bins - 1);
			lat_times[lat_idx]++;
			hist_idx = sample_time < hist_min_time
			           ? 0
			           : (sample_time - hist_min_time) / hist_bin_sz;
			hist_idx = MIN(hist_idx, nr_hist_bins - 1);
			hist_times[hist_idx]++;
			/* useful for formatting the ***s */
			max_hist_bin = (hist_times[hist_idx] > max_hist_bin)
			               ? hist_times[hist_idx]
			               : max_hist_bin;
		}
	}
	/* Compute latency percentiles */
	for (int i = 0; i < nr_lat_bins; i++) {
		accum_samples += lat_times[i];
		/* (i + 1), since we've just accumulated one bucket's worth */
		if (!stats->lat_50 && accum_samples / stats->total_samples > 0.50)
			stats->lat_50 = (i + 1) * lat_bin_sz + lat_min_time;
		if (!stats->lat_75 && accum_samples / stats->total_samples > 0.75)
			stats->lat_75 = (i + 1) * lat_bin_sz + lat_min_time;
		if (!stats->lat_90 && accum_samples / stats->total_samples > 0.90)
			stats->lat_90 = (i + 1) * lat_bin_sz + lat_min_time;
		if (!stats->lat_99 && accum_samples / stats->total_samples > 0.99)
			stats->lat_99 = (i + 1) * lat_bin_sz + lat_min_time;
	}
	for (int i = 0; i < nr_hist_bins; i++) {
		uint64_t interval_start = i * hist_bin_sz + hist_min_time;
		uint64_t interval_end = (i + 1) * hist_bin_sz + hist_min_time;
		/* customize the first and last entries */
		if (i == 0)
			interval_start = MIN(interval_start, stats->min_time);
		if (i == nr_hist_bins - 1) {
			interval_end = MAX(interval_end, stats->max_time);
			/* but not at the sake of formatting! (8 spaces) */
			interval_end = MIN(interval_end, 99999999);
		}
		printf("    [%8llu - %8llu] %7d: ", interval_start, interval_end,
		       hist_times[i]);
		/* nr_dots = hist_times[i] * nr_dots_per_sample
		 *         = hist_times[i] * (max_num_dots / max_hist_bin) */
		int nr_dots = hist_times[i] * max_dots_per_row / max_hist_bin;
		for (int j = 0; j < nr_dots; j++)
			printf("*");
		printf("\n");
	}
	printf("\n");
	printf("Samples per dot: %d\n", max_hist_bin / max_dots_per_row);
	printf("Total samples: %llu\n", stats->total_samples);
	printf("Avg time   : %llu\n", stats->avg_time);
	printf("Stdev time : %f\n", sqrt(stats->var_time));
	printf("Coef Var   : %f\n", coef_var);
	if (coef_var > HI_COEF_VAR)
		printf("\tHigh coeff of var with serious outliers, adjusted bins\n");
	/* numbers are overestimates by at most a lat bin */
	printf("50/75/90/99: %d / %d / %d / %d (-<%d)\n", stats->lat_50,
	       stats->lat_75, stats->lat_90, stats->lat_99, lat_bin_sz);
	printf("Min / Max  : %llu / %llu\n", stats->min_time, stats->max_time);
	printf("\n");
	free(hist_times);
	free(lat_times);
}

/* Prints the throughput of certain events over nr_steps of interval time.  Will
 * print the overall throughput of the entire time (total events / steps),
 * and print out each step up to nr_print_steps.
 *
 * Assumes a 2D data structure, where the events in each data[i][] (for a
 * specific i) are in order.  The 'nr_i'es are typically threads or something
 * similar.  nr_j would be how many samples per thread.  The func ptr should
 * return the time of the data[i][j]'th event via *sample and return 0 on
 * success, and any other value for 'no data'.  If start_time is 0, we'll start
 * the clock right before the first event. */
void print_throughput(void **data, unsigned int nr_steps, uint64_t interval,
                      unsigned int nr_print_steps, uint64_t start_time,
                      int nr_i, int nr_j,
                      int (*get_sample)(void **data, int i, int j,
                                        uint64_t *sample))
{
	uint64_t time_now, sample;
	/* next_sample[] tracks each thread's next lock that was acquired */
	unsigned int *next_sample;
	unsigned int *step_events;
	unsigned int most_step_events = 1;
	unsigned int max_dots_per_row = 45;	/* looks good with 80-wide col */
	unsigned int total_events = 0;

	if (!nr_steps)
		return;
	nr_print_steps = MIN(nr_print_steps, nr_steps);
	next_sample = malloc(sizeof(unsigned int) * nr_i);
	step_events = malloc(sizeof(unsigned int) * nr_steps);
	if (!next_sample || !step_events) {
		perror("print_throughput failed alloc:");
		free(next_sample);
		free(step_events);
		return;
	}
	memset(next_sample, 0, sizeof(unsigned int) * nr_i);
	memset(step_events, 0, sizeof(unsigned int) * nr_steps);
	if (start_time) {
		time_now = start_time;
	} else {
		time_now = UINT64_MAX;
		/* Set the replay to start right before the first event */
		for (int i = 0; i < nr_i; i++) {
			if (get_sample(data, i, 0, &sample))
				continue;
			time_now = MIN(time_now, sample);
		}
		if (time_now != 0)
			time_now--;
	}
	for (int k = 0; k < nr_steps; k++) {
		time_now += interval;
		/* for every 'thread', we'll figure out how many events occurred, and
		 * advance next_sample to track the next one to consider */
		for (int i = 0; i < nr_i; i++) {
			/* count nr locks that have happened, advance per thread tracker */
			for ( ; next_sample[i] < nr_j; next_sample[i]++) {
				/* skip this thread if it has no more data */
				if (get_sample(data, i, next_sample[i], &sample))
					continue;
				/* break when we found one that hasn't happened yet */
				if (!(sample <= time_now))
					break;
				step_events[k]++;
			}
		}
		total_events += step_events[k];
		/* for dynamically scaling the *'s */
		most_step_events = MAX(most_step_events, step_events[k]);
	}
	if (nr_print_steps)
		printf("Events per dot: %d\n", most_step_events / max_dots_per_row);
	for (int k = 0; k < nr_print_steps; k++) {
		/* Last step isn't accurate, will only be partially full */
		if (k == nr_steps - 1)
			break;
		printf("%6d: ", k);
		printf("%6d ", step_events[k]);
		/* nr_dots = step_events[k] * nr_dots_per_event
		 *         = step_events[k] * (max_dots_per_row / most_step_events) */
		int nr_dots = step_events[k] * max_dots_per_row / most_step_events;
		for (int i = 0; i < nr_dots; i++)
			printf("*");
		printf("\n");
	}
	printf("Total events: %d, Avg events/step: %d\n", total_events,
	       total_events / nr_steps);
	free(next_sample);
	free(step_events);
}
