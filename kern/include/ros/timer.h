#ifndef ROS_INC_TIMER_H
#define ROS_INC_TIMER_H

#include <pool.h>
#include <string.h>

#define TIMER_TAG_SIZE 20
#define MAX_TIMERS 20

/* start_timing()
 * This function simply reads the tsc in a serialized fashion and returns its
 * value.  It is pusposefully annotated with a noinline so that the overheads 
 * assocaited with calling it are as deterministic as possible.
 */
uint64_t start_timing() __attribute__((noinline));

/* stop_timing()
 * This function reads the tsc in a serialized fashion and subtracts the value
 * it reads from the value passed in as a paramter in order to determine the 
 * difference between the two values.  A global timing_overhead value is also 
 * subtracted to compensate for the overhead associated with calling both
 * start and stop timing and returning their values.
 * This function is purposefully annotated with a noinline so that 
 * the overheads assocaited with calling it are as deterministic as possible.
 */
uint64_t stop_timing(uint64_t val) __attribute__((noinline));

/* train_timing()
 * This function is intended to train the timing_overhead variable for use by
 * stop_timing().  It runs through a loop calling start/stop and averaging the 
 * overhead of calling them without doing any useful work in between.
 */
void train_timing();

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

//TODO: ifdef measurement? error check when pool runs low
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

#endif /* !ROS_INC_TIMER_H */

