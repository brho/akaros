/* See COPYRIGHT for copyright information. */
/* Kevin Klues <klueska@cs.berkeley.edu>	*/

#ifndef ROS_POOL_H
#define ROS_POOL_H   

#define POOL_DECLARE(_t, p, sz)                                                \
struct {                                                                       \
	uint32_t size;                                                         \
	uint32_t free;                                                         \
	uint32_t index;                                                        \
	_t* queue[(sz)];                                                       \
	_t pool[(sz)];                                                         \
} p = {(sz), (sz), 0, {[0 ... ((sz)-1)] 0}, {[0 ... ((sz)-1)] 0}};

#define POOL_INIT(p)                                                           \
({                                                                             \
	for(int i=0; i<(p)->size; i++) {                                       \
		(p)->queue[i] = &((p)->pool[i]);                               \
	}                                                                      \
})

#define POOL_GET(p)                                                            \
({                                                                             \
	if((p)->free) {                                                        \
		void* rval = (p)->queue[(p)->index];                           \
		(p)->queue[(p)->index] = NULL;                                 \
		(p)->free--;                                                   \
		(p)->index++;                                                  \
		if((p)->index == (p)->size) {                                  \
    		(p)->index = 0;                                                \
      	}                                                                      \
		rval;                                                          \
	}                                                                      \
	NULL;                                                                  \
})

#define POOL_PUT(p, val)                                                       \
({                                                                             \
	if((p)->free >= (p)->size) {                                           \
		-1;                                                            \
	}                                                                      \
	else {                                                                 \
		int emptyIndex = ((p)->index + (p)->free);                     \
		if (emptyIndex >= (p)->size) {                                 \
			emptyIndex -= (p)->size;                               \
		}                                                              \
		(p)->queue[emptyIndex] = val;                                  \
		(p)->free++;                                                   \
		1;                                                             \
	}                                                                      \
})

#define POOL_EMPTY(p) ((p)->free == 0)
#define POOL_SIZE(p) ((p)->free))
#define POOL_MAX_SIZE(p) ((p)->size))

#endif //ROS_POOL_H
