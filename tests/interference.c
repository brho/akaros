#include <stdint.h>
#include <stdlib.h>

#ifdef __akaros__

#include <parlib/stdio.h>
#include <parlib/uthread.h>
#include <parlib/timing.h>
#include <parlib/event.h>
#include <benchutil/measure.h>

static void ev_handle_null(struct event_msg *ev_msg, unsigned int ev_type,
                           void *data)
{
}

static void os_init(void)
{
	uthread_mcp_init();
	register_ev_handler(EV_FREE_APPLE_PIE, ev_handle_null, NULL);
}

#else

/* Build on linux from $AKAROS_ROOT:
 * gcc -static --std=gnu99 tests/interference.c -o lin-interference
 */

#include <stdio.h>
#include "../user/parlib/include/parlib/tsc-compat.h"
#include "misc-compat.h"

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

void compute_stats(void **data, int nr_i, int nr_j, struct sample_stats *stats)
{
	/* TODO: could try to link against benchutil. */
}

static void os_init(void)
{
	printf("Linux: If you get a segfault, make sure rdpmc is allowed.\n"
	       "Linux: Set /sys/bus/event_source/devices/cpu/rdpmc = 2 on recent kernels (4.0), or 1 for older kernels.\n");
}

#endif

static uint64_t *delays;

static int get_delay(void **data, int i, int j, uint64_t *sample)
{
	uint64_t **delay_array = (uint64_t**)data;

	*sample = delay_array[i][j];
	return 0;
}

static inline __attribute__((always_inline))
uint64_t pmc_cycles(void)
{
	unsigned int a = 0, d = 0;
	int ecx = (1 << 30) + 1;

	asm volatile("lfence; rdpmc" : "=a"(a), "=d"(d) : "c"(ecx));
	return ((uint64_t)a) | (((uint64_t)d) << 32);
}

int main(int argc, char **argv)
{
	#define THRESHOLD 200
	uint64_t start, diff;
	struct sample_stats stats[1];
	size_t idx = 0;
	size_t nr_below_thresh = 0;
	size_t nr_over_thresh = 0;
	size_t total = 0;
	int pcoreid;
	uint64_t low_samples[THRESHOLD] = {0};
	int nr_samples = 1000;
	uint64_t deadline = sec2tsc(5);	/* assumes TSC and cycles are close */

	/* Normally we'd use a 2D array, but since we're just one thread, we just
	 * need our first thread's array. */
	delays = malloc(sizeof(uint64_t) * nr_samples);
	os_init();
	pcoreid = get_pcoreid();
	udelay(1000);
	deadline += pmc_cycles();

	do {
		if (idx >= nr_samples)
			break;
		total++;
		start = pmc_cycles();
		diff = pmc_cycles() - start;
		if (diff < COUNT_OF(low_samples))
			low_samples[diff]++;
		if (diff < THRESHOLD) {
			nr_below_thresh++;
		} else {
			nr_over_thresh++;
			delays[idx++] = diff;
		}
		if (!start) {
			printf("rdpmc got 0, is perf stat -e cycles running? (aborting)\n");
			break;
		}
	} while (start < deadline);

	printf("\n\nStats for interference\n");
	stats->get_sample = get_delay;
	compute_stats((void**)&delays, 1, idx, stats);

	printf("\n\nStats for low rdtsc times (tsc ticks for two rdtscs)\n");
	for (int i = 0; i < COUNT_OF(low_samples); i++) {
		if (low_samples[i])
			printf("\t[ %2d ] : %lu\n", i, low_samples[i]);
	}

	printf("Pcoreid was %d (and is now %d)\n\n", pcoreid, get_pcoreid());
	printf("Total loops %lu, threshold %u\n", total, THRESHOLD);
	printf("Nr over thresh %lu (%f%% total)\n", nr_over_thresh,
	       nr_over_thresh * 100.0 / total);
	printf("Nr below thresh %lu (%f%% total)\n", nr_below_thresh,
	       nr_below_thresh * 100.0 / total);
	return 0;
}
