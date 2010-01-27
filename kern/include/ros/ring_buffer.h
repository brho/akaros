/******************************************************************************
 * ring.h
 * 
 * Shared producer-consumer ring macros.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Tim Deegan and Andrew Warfield November 2004.
 */

#ifndef ROS_INC_RING_BUFFER_H
#define ROS_INC_RING_BUFFER_H

#include <string.h>
#include <arch/membar.h>

#define xen_mb()  mb()
#define xen_rmb() rmb()
#define xen_wmb() wmb()

typedef unsigned int RING_IDX;

// zra: smp.c is v. slow to build because these RDn() things cause expressions
//      to grow exponentially.

/* Round a 32-bit unsigned constant down to the nearest power of two. */
#define __RD2(_x)  (((_x) & 0x00000002UL) ? 0x2                  : ((_x) & 0x1))
#define __RD4(_x)  (((_x) & 0x0000000cUL) ? __RD2((_x)>>2)<<2    : __RD2(_x))
#define __RD8(_x)  (((_x) & 0x000000f0UL) ? __RD4((_x)>>4)<<4    : __RD4(_x))
#define __RD16(_x) (((_x) & 0x0000ff00UL) ? __RD8((_x)>>8)<<8    : __RD8(_x))
#define __RD32(_x) (((_x) & 0xffff0000UL) ? __RD16((_x)>>16)<<16 : __RD16(_x))

/* Statically assert that two values are in fact equal.
 * It works by enducing a compil error from a duplicate case in a switch 
 * statement if the assertion is false.
 */
#define __ASSERT_EQUAL(x, y) \
	switch ((x) == (y)) case 0: case ((x) == (y)):

/*
 * Calculate size of a shared ring, given the total available space for the
 * ring and indexes (_sz), and the name tag of the request/response structure.
 * A ring contains as many entries as will fit, rounded down to the nearest 
 * power of two (so we can mask with (size-1) to loop around).
 * This tells us how many elements the ring _s can contain, given _sz space.
 */
#define __RING_SIZE(_s, _sz) \
    __RING_SIZE_BYTES(_s, _sz) / sizeof((_s)->ring[0])

/*
 * This is the same as above, except in terms of bytes instead of elements
 */
#define __RING_SIZE_BYTES(_s, _sz) \
    (__RD32((_sz) - (long)(_s)->ring + (long)(_s)))

/*
 * These two are the same as above except that they rely on type information
 * to determine the sizes statically, rather than the runtime instantiation
 * of the ring buffer variable
 */
#define __RING_SIZE_STATIC(__name, _sz) \
    (__RING_SIZE_BYTES_STATIC(_sz) / sizeof(union __name##_sring_entry))

#define __RING_SIZE_BYTES_STATIC(_sz) \
    (__RD32((_sz) - __RING_HEADER_SIZE()))

/*
 * Macros to make the correct C datatypes for a new kind of ring.
 * 
 * To make a new ring datatype, you need to have two message structures,
 * let's say request_t, and response_t already defined.
 *
 * In a header where you want the ring datatype declared, you then do:
 *
 *     DEFINE_RING_TYPES(mytag, request_t, response_t);
 * or
 *     DEFINE_RING_TYPES_WITH_SIZE(mytag, request_t, response_t, size);
 *
 * Both macros expand out to give you a set of types, as you can see below.
 * The most important of these are:
 * 
 *     mytag_sring_t      - The shared ring.
 *     mytag_front_ring_t - The 'front' half of the ring.
 *     mytag_back_ring_t  - The 'back' half of the ring.
 *
 * The first of these macros will only declare a single element array to 
 * represent the ring buffer in the shared ring struct that is ultimately
 * created.  
 *
 * The second macro actually statically declares space of size (size) inside
 * the shared ring struct. This size is rounded down to the nearest power of 2
 * and space is subtracted off to account for holding any necessary ring 
 * buffer headers.
 *
 * To initialize a ring in your code you need to know the location and size
 * of the shared memory area (PAGE_SIZE, for instance). To initialise
 * the front half:
 *
 *     mytag_front_ring_t front_ring;
 *     SHARED_RING_INIT((mytag_sring_t *)shared_page);
 *     FRONT_RING_INIT(&front_ring, (mytag_sring_t *)shared_page, PAGE_SIZE);
 *
 * Initializing the back follows similarly (note that only the front
 * initializes the shared ring):
 *
 *     mytag_back_ring_t back_ring;
 *     BACK_RING_INIT(&back_ring, (mytag_sring_t *)shared_page, PAGE_SIZE);
 *
 * If you use the second of the two macros when first defining your ring 
 * structures, then the size you use when initializing your front and back 
 * rings *should* match the size you passed into this macro (e.g. PAGE_SIZE
 * in this example).
 */

#define __RING_HEADER()                                                 \
    RING_IDX req_prod, req_event;                                       \
    RING_IDX rsp_prod, rsp_event;                                       \
    uint8_t  pad[48];
    
struct rhs_struct {
	__RING_HEADER()
};

#define __RING_HEADER_SIZE() \
    (sizeof(struct rhs_struct))

#define DEFINE_RING_TYPES(__name, __req_t, __rsp_t)                     \
	DEFINE_RING_TYPES_WITH_SIZE(__name, __req_t, __rsp_t,               \
	                            __RING_HEADER_SIZE() + 1)

#define DEFINE_RING_TYPES_WITH_SIZE(__name, __req_t, __rsp_t, __size)   \
                                                                        \
/* Shared ring entry */                                                 \
union __name##_sring_entry {                                            \
    __req_t req;                                                        \
    __rsp_t rsp;                                                        \
} TRUSTED;                                                              \
                                                                        \
/* Shared ring page */                                                  \
struct __name##_sring {                                                 \
	__RING_HEADER()                                                     \
    union __name##_sring_entry ring[__RING_SIZE_STATIC(__name, __size)];\
};                                                                      \
                                                                        \
/* "Front" end's private variables */                                   \
struct __name##_front_ring {                                            \
    RING_IDX req_prod_pvt;                                              \
    RING_IDX rsp_cons;                                                  \
    unsigned int nr_ents;                                               \
    struct __name##_sring *sring;                                       \
};                                                                      \
                                                                        \
/* "Back" end's private variables */                                    \
struct __name##_back_ring {                                             \
    RING_IDX rsp_prod_pvt;                                              \
    RING_IDX req_cons;                                                  \
    unsigned int nr_ents;                                               \
    struct __name##_sring *sring;                                       \
};                                                                      \
                                                                        \
/* Syntactic sugar */                                                   \
typedef struct __name##_sring __name##_sring_t;                         \
typedef struct __name##_front_ring __name##_front_ring_t;               \
typedef struct __name##_back_ring __name##_back_ring_t;                 \
/* This is a dummy function just used to statically assert that         \
 * there are no weird padding issues associated with our sring structs  \
 */                                                                     \
static void __name##_assert_sring_size() __attribute__((used));         \
static void __name##_assert_sring_size() {                              \
	__ASSERT_EQUAL( sizeof(__name##_sring_t),                           \
	                ( __RING_HEADER_SIZE()  +                           \
	                  ( __RING_SIZE_STATIC(__name, __size) *            \
	                    sizeof(union __name##_sring_entry)              \
	                  )                                                 \
	                )                                                   \
	              );                                                    \
}              


/*
 * Macros for manipulating rings.
 * 
 * FRONT_RING_whatever works on the "front end" of a ring: here 
 * requests are pushed on to the ring and responses taken off it.
 * 
 * BACK_RING_whatever works on the "back end" of a ring: here 
 * requests are taken off the ring and responses put on.
 * 
 * N.B. these macros do NO INTERLOCKS OR FLOW CONTROL. 
 * This is OK in 1-for-1 request-response situations where the 
 * requestor (front end) never has more than RING_SIZE()-1
 * outstanding requests.
 */

/* Initialising empty rings */
#define SHARED_RING_INIT(_s) do {                                       \
    (_s)->req_prod  = (_s)->rsp_prod  = 0;                              \
    (_s)->req_event = (_s)->rsp_event = 1;                              \
    (void)memset((_s)->pad, 0, sizeof((_s)->pad));                      \
} while(0)

#define FRONT_RING_INIT(_r, _s, __size) do {                            \
    (_r)->req_prod_pvt = 0;                                             \
    (_r)->rsp_cons = 0;                                                 \
    (_r)->nr_ents = __RING_SIZE(_s, __size);                            \
    (_r)->sring = (_s);                                                 \
} while (0)

#define BACK_RING_INIT(_r, _s, __size) do {                             \
    (_r)->rsp_prod_pvt = 0;                                             \
    (_r)->req_cons = 0;                                                 \
    (_r)->nr_ents = __RING_SIZE(_s, __size);                            \
    (_r)->sring = (_s);                                                 \
} while (0)

/* Initialize to existing shared indexes -- for recovery */
#define FRONT_RING_ATTACH(_r, _s, __size) do {                          \
    (_r)->sring = (_s);                                                 \
    (_r)->req_prod_pvt = (_s)->req_prod;                                \
    (_r)->rsp_cons = (_s)->rsp_prod;                                    \
    (_r)->nr_ents = __RING_SIZE(_s, __size);                            \
} while (0)

#define BACK_RING_ATTACH(_r, _s, __size) do {                           \
    (_r)->sring = (_s);                                                 \
    (_r)->rsp_prod_pvt = (_s)->rsp_prod;                                \
    (_r)->req_cons = (_s)->req_prod;                                    \
    (_r)->nr_ents = __RING_SIZE(_s, __size);                            \
} while (0)

/* How big is this ring? */
#define RING_SIZE(_r)                                                   \
    ((_r)->nr_ents)

/* Number of free requests (for use on front side only). */
#define RING_FREE_REQUESTS(_r)                                          \
    (RING_SIZE(_r) - ((_r)->req_prod_pvt - (_r)->rsp_cons))

/* Test if there is an empty slot available on the front ring.
 * (This is only meaningful from the front. )
 */
#define RING_FULL(_r)                                                   \
    (RING_FREE_REQUESTS(_r) == 0)

/* Test if there are outstanding messages to be processed on a ring. */
#define RING_HAS_UNCONSUMED_RESPONSES(_r)                               \
    ((_r)->sring->rsp_prod - (_r)->rsp_cons)

#ifdef __GNUC__
#define RING_HAS_UNCONSUMED_REQUESTS(_r) ({                             \
    unsigned int req = (_r)->sring->req_prod - (_r)->req_cons;          \
    unsigned int rsp = RING_SIZE(_r) -                                  \
        ((_r)->req_cons - (_r)->rsp_prod_pvt);                          \
    req < rsp ? req : rsp;                                              \
})
#else
/* Same as above, but without the nice GCC ({ ... }) syntax. */
#define RING_HAS_UNCONSUMED_REQUESTS(_r)                                \
    ((((_r)->sring->req_prod - (_r)->req_cons) <                        \
      (RING_SIZE(_r) - ((_r)->req_cons - (_r)->rsp_prod_pvt))) ?        \
     ((_r)->sring->req_prod - (_r)->req_cons) :                         \
     (RING_SIZE(_r) - ((_r)->req_cons - (_r)->rsp_prod_pvt)))
#endif

/* Direct access to individual ring elements, by index. */
#define RING_GET_REQUEST(_r, _idx)                                      \
    (&((_r)->sring->ring[((_idx) & (RING_SIZE(_r) - 1))].req))

#define RING_GET_RESPONSE(_r, _idx)                                     \
    (&((_r)->sring->ring[((_idx) & (RING_SIZE(_r) - 1))].rsp))

/* Loop termination condition: Would the specified index overflow the ring? */
#define RING_REQUEST_CONS_OVERFLOW(_r, _cons)                           \
    (((_cons) - (_r)->rsp_prod_pvt) >= RING_SIZE(_r))

#define RING_PUSH_REQUESTS(_r) do {                                     \
    xen_wmb(); /* back sees requests /before/ updated producer index */ \
    (_r)->sring->req_prod = (_r)->req_prod_pvt;                         \
} while (0)

#define RING_PUSH_RESPONSES(_r) do {                                    \
    xen_wmb(); /* front sees resps /before/ updated producer index */   \
    (_r)->sring->rsp_prod = (_r)->rsp_prod_pvt;                         \
} while (0)

/*
 * Notification hold-off (req_event and rsp_event):
 * 
 * When queueing requests or responses on a shared ring, it may not always be
 * necessary to notify the remote end. For example, if requests are in flight
 * in a backend, the front may be able to queue further requests without
 * notifying the back (if the back checks for new requests when it queues
 * responses).
 * 
 * When enqueuing requests or responses:
 * 
 *  Use RING_PUSH_{REQUESTS,RESPONSES}_AND_CHECK_NOTIFY(). The second argument
 *  is a boolean return value. True indicates that the receiver requires an
 *  asynchronous notification.
 * 
 * After dequeuing requests or responses (before sleeping the connection):
 * 
 *  Use RING_FINAL_CHECK_FOR_REQUESTS() or RING_FINAL_CHECK_FOR_RESPONSES().
 *  The second argument is a boolean return value. True indicates that there
 *  are pending messages on the ring (i.e., the connection should not be put
 *  to sleep).
 * 
 *  These macros will set the req_event/rsp_event field to trigger a
 *  notification on the very next message that is enqueued. If you want to
 *  create batches of work (i.e., only receive a notification after several
 *  messages have been enqueued) then you will need to create a customised
 *  version of the FINAL_CHECK macro in your own code, which sets the event
 *  field appropriately.
 */

#define RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(_r, _notify) do {           \
    RING_IDX __old = (_r)->sring->req_prod;                             \
    RING_IDX __new = (_r)->req_prod_pvt;                                \
    xen_wmb(); /* back sees requests /before/ updated producer index */ \
    (_r)->sring->req_prod = __new;                                      \
    xen_mb(); /* back sees new requests /before/ we check req_event */  \
    (_notify) = ((RING_IDX)(__new - (_r)->sring->req_event) <           \
                 (RING_IDX)(__new - __old));                            \
} while (0)

#define RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(_r, _notify) do {          \
    RING_IDX __old = (_r)->sring->rsp_prod;                             \
    RING_IDX __new = (_r)->rsp_prod_pvt;                                \
    xen_wmb(); /* front sees resps /before/ updated producer index */   \
    (_r)->sring->rsp_prod = __new;                                      \
    xen_mb(); /* front sees new resps /before/ we check rsp_event */    \
    (_notify) = ((RING_IDX)(__new - (_r)->sring->rsp_event) <           \
                 (RING_IDX)(__new - __old));                            \
} while (0)

#define RING_FINAL_CHECK_FOR_REQUESTS(_r, _work_to_do) do {             \
    (_work_to_do) = RING_HAS_UNCONSUMED_REQUESTS(_r);                   \
    if (_work_to_do) break;                                             \
    (_r)->sring->req_event = (_r)->req_cons + 1;                        \
    xen_mb();                                                           \
    (_work_to_do) = RING_HAS_UNCONSUMED_REQUESTS(_r);                   \
} while (0)

#define RING_FINAL_CHECK_FOR_RESPONSES(_r, _work_to_do) do {            \
    (_work_to_do) = RING_HAS_UNCONSUMED_RESPONSES(_r);                  \
    if (_work_to_do) break;                                             \
    (_r)->sring->rsp_event = (_r)->rsp_cons + 1;                        \
    xen_mb();                                                           \
    (_work_to_do) = RING_HAS_UNCONSUMED_RESPONSES(_r);                  \
} while (0)

#endif /* ROS_INC_RING_BUFFER_H */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

