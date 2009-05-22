#ifndef ROS_INC_TIMER_H
#define ROS_INC_TIMER_H
#include <inc/pool.h>
#include <inc/string.h>

#define TIMER_TAG_SIZE 20
#define MAX_TIMERS 20

typedef struct Timer{
	uint64_t curr_run;
	uint64_t aggr_run;
	char label[TIMER_TAG_SIZE];
} timer_t;

// vanilla variety
uint64_t start_timing() __attribute__((noinline));
uint64_t stop_timing(uint64_t val, uint64_t overhead) __attribute__((noinline));
//void print_timerpool(
//allocate if not null
//starttiming
//stoptiming
#define FUNCBEGIN(tag)							\
static timer_t*  _timer_##tag = 0;				\
if (_timer_##tag == NULL){						\
	_timer_##tag = POOL_GET(&timer_pool);		\
	strcpy((_timer_##tag->label), #tag);		\
	_timer_##tag->aggr_run = 0;					\
}												\
_timer_##tag->curr_run = start_timing();


#define FUNCEND(tag)														\
({																			\
_timer_##tag->curr_run = stop_timing(_timer_##tag->curr_run, 0);			\
_timer_##tag->aggr_run += _timer_##tag->curr_run;							\
})




#endif /* !ROS_INC_TIMER_H */

