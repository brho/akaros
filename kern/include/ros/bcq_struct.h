/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Struct for the BCQ.  Needs to be in its own file so glibc doesn't try to
 * include any of the atomics needed for the actual BCQ operations.  */

#ifndef ROS_INC_BCQ_STRUCT_H
#define ROS_INC_BCQ_STRUCT_H

struct bcq_header {
	uint32_t prod_idx;		/* next to be produced in */
	uint32_t cons_pub_idx;	/* last completely consumed */
	uint32_t cons_pvt_idx;	/* last a consumer has dibs on */
};

#define DEFINE_BCQ_TYPES(__name, __elem_t, __num_elems)                        \
                                                                               \
/* Wrapper, per element, with the consumption bool */                          \
struct __name##_bcq_wrap {                                                     \
	__elem_t elem;                                                             \
	bool rdy_for_cons;	/* elem is ready for consumption */                    \
};                                                                             \
                                                                               \
/* The actual BC queue */                                                      \
struct __name##_bcq {                                                          \
	struct bcq_header hdr;                                                     \
	struct __name##_bcq_wrap wraps[__num_elems];                               \
};                                                                             
                                                                               
#endif /* !ROS_INC_BCQ_STRUCT_H */
