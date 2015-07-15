#ifndef ROS_KERN_TIME_H
#define ROS_KERN_TIME_H

#include <ros/common.h>
#include <ros/time.h>
#include <arch/time.h>

void train_timing();
void udelay(uint64_t usec);	/* done in arch-specific files */
uint64_t tsc2sec(uint64_t tsc_time);
uint64_t tsc2msec(uint64_t tsc_time);
uint64_t tsc2usec(uint64_t tsc_time);
uint64_t tsc2nsec(uint64_t tsc_time);
uint64_t sec2tsc(uint64_t sec);
uint64_t msec2tsc(uint64_t msec);
uint64_t usec2tsc(uint64_t usec);
uint64_t nsec2tsc(uint64_t nsec);
uint64_t epoch_tsc(void);
uint64_t epoch_sec(void);
uint64_t epoch_msec(void);
uint64_t epoch_usec(void);
uint64_t epoch_nsec(void);
void tsc2timespec(uint64_t tsc_time, struct timespec *ts);

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
    diff -= system_timing.timing_overhead;
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
	_timer_##tag->curr_run = stop_timing(_timer_##tag->curr_run);           \
	_timer_##tag->aggr_run += _timer_##tag->curr_run;                       \
})

#endif
#endif /* ROS_KERN_TIME_H */
