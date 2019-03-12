#pragma once

#include <ros/common.h>
#include <ros/time.h>
#include <arch/time.h>
#include <ros/procinfo.h>

/* Conversion factors */
#define NSEC_PER_SEC	1000000000L
#define NSEC_PER_MSEC	1000000L
#define NSEC_PER_USEC	1000L

void time_init(void);
void udelay(uint64_t usec);	/* done in arch-specific files */

uint64_t read_persistent_clock(void);	/* arch-specific */

uint64_t tsc2nsec(uint64_t tsc_time);

static inline uint64_t tsc2usec(uint64_t tsc_time)
{
	return tsc2nsec(tsc_time) / NSEC_PER_USEC;
}

static inline uint64_t tsc2msec(uint64_t tsc_time)
{
	return tsc2nsec(tsc_time) / NSEC_PER_MSEC;
}

static inline uint64_t tsc2sec(uint64_t tsc_time)
{
	return tsc2nsec(tsc_time) / NSEC_PER_SEC;
}

uint64_t nsec2tsc(uint64_t nsec);

static inline uint64_t usec2tsc(uint64_t usec)
{
	return nsec2tsc(usec * NSEC_PER_USEC);
}

static inline uint64_t msec2tsc(uint64_t msec)
{
	return nsec2tsc(msec * NSEC_PER_MSEC);
}

static inline uint64_t sec2tsc(uint64_t sec)
{
	return nsec2tsc(sec * NSEC_PER_SEC);
}

uint64_t epoch_nsec(void);

static inline struct timespec nsec2timespec(uint64_t ns)
{
	return (struct timespec) {
		.tv_sec = ns / NSEC_PER_SEC,
		.tv_nsec = ns % NSEC_PER_SEC
	};
}

static inline struct timeval nsec2timeval(uint64_t ns)
{
	return (struct timeval) {
		.tv_sec = ns / NSEC_PER_SEC,
		.tv_usec = (ns % NSEC_PER_SEC) / NSEC_PER_USEC
	};
}

static inline struct timespec tsc2timespec(uint64_t tsc_time)
{
	return nsec2timespec(tsc2nsec(tsc_time));
}

/* Just takes a time measurement.  Meant to be paired with stop_timing.  Use
 * this if you don't want to muck with overheads or subtraction. */
static inline __attribute__((always_inline))
uint64_t start_timing(void)
{
    return read_tsc_serialized();
}

/* Takes a time measurement and subtracts the start time and timing overhead,
 * to return the detected elapsed time.  Use this if you don't want to muck
 * with overheads or subtraction. */
static inline __attribute__((always_inline))
uint64_t stop_timing(uint64_t start_time)
{
    uint64_t diff = read_tsc_serialized();
    diff -= start_time;
    diff -= __proc_global_info.tsc_overhead;
	if ((int64_t) diff < 0) {
		return 1;
	}
	return diff;
}

static inline __attribute__((always_inline))
uint64_t nsec(void)
{
	return tsc2nsec(read_tsc());
}


/* Ancient measurement crap below.  TODO: use or lose it */

#if 0
#include <pool.h>
#include <string.h>

#define TIMER_TAG_SIZE 20
#define MAX_TIMERS 20
/* timer_t
 * This struct is used to keep track of counter values as they are spread
 * throughput code and timing measurements are made calling TAGGED_TIMING_BEGIN
 * and TAGGED_TIMING_END
 */
typedef struct Timer{
	uint64_t curr_run;
	uint64_t aggr_run;
	char label[TIMER_TAG_SIZE];
} timer_t;

#define TAGGED_TIMING_BEGIN(tag)                    \
	static timer_t* _timer_##tag = NULL;            \
	if (_timer_##tag == NULL) {                     \
		_timer_##tag = POOL_GET(&timer_pool);       \
		strcpy((_timer_##tag->label), #tag);        \
		_timer_##tag->aggr_run = 0;                 \
	}                                               \
	_timer_##tag->curr_run = start_timing();
#define TAGGED_TIMING_END(tag)                                              \
({                                                                          \
	_timer_##tag->curr_run = stop_timing(_timer_##tag->curr_run);       \
	_timer_##tag->aggr_run += _timer_##tag->curr_run;                   \
})

#endif
