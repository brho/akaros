/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifndef ROS_INC_POOL_H
#define ROS_INC_POOL_H

#include <string.h>

#define POOL_TYPE_DEFINE(_type, p, sz)                                                \
typedef struct struct_##p {                                                             \
	uint32_t size;                                                         \
	uint32_t free;                                                         \
	uint32_t index;                                                        \
	_type*COUNT(1) queue[(sz)];                                                       \
	_type pool[(sz)];                                                         \
} p##_t;

#define POOL_INIT(p, sz)                                                       \
({                                                                             \
	(p)->size = (sz);                                                          \
	(p)->free = (sz);                                                          \
	(p)->index = 0;                                                            \
	memset((p)->pool, 0, (sz) * sizeof((p)->pool[0]));                         \
	for(int i=0; i<(p)->size; i++) {                                           \
		(p)->queue[i] = &((p)->pool[i]);                                       \
	}                                                                          \
})
// removed unnecessary (p)->queue[(p)->index] = NULL;
#define POOL_GET(p)                                            \
({                                                             \
	void* rval = NULL;                                         \
	if((p)->free) {                                            \
		rval = (p)->queue[(p)->index];                         \
		(p)->free--;                                           \
		(p)->index++;                                          \
		if((p)->index == (p)->size) {                          \
    		(p)->index = 0;                                    \
      	}                                                      \
	}                                                          \
	rval;                                                      \
})

// emptyIndex is also the first element that has been allocated, iterate thru to index-1

#define POOL_FOR_EACH(p, func)									\
({																\
	int emptyIndex = ((p)->index + (p)->free);          	    \
	if (emptyIndex >= (p)->size) {               				\
		emptyIndex -= (p)->size;                           		\
	}		                                                    \
	for(int _i = emptyIndex;  _i < (p)->index; _i++){			\
		func((p)->queue[_i]);								\
	}															\
})																\

#define POOL_PUT(p, val)                                                       \
({                                                                             \
	int rval = -1;                                                            \
	if((p)->free < (p)->size) {                                           \
		int emptyIndex = ((p)->index + (p)->free);                     \
		if (emptyIndex >= (p)->size) {                                 \
			emptyIndex -= (p)->size;                               \
		}                                                              \
		(p)->queue[emptyIndex] = val;                                  \
		(p)->free++;                                                   \
		rval = 1;                                                             \
	}                                                                      \
	rval;                                                                 \
})

#define POOL_EMPTY(p) ((p)->free == 0)
#define POOL_SIZE(p) ((p)->free)
#define POOL_MAX_SIZE(p) ((p)->size)

#endif //ROS_INC_POOL_H
