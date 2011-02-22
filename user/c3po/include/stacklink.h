#ifndef STACKLINK_H
#define STACKLINK_H

// -----------------------------------------------------------------------

// stacklink.h
//
// This module defines the primary interface for stack chunk allocation
// and linking.  It is used by capriccio's thread system to allocate
// initial stack chunks, and it is used by the build system to link and
// unlink stack chunks where appropriate.

// -----------------------------------------------------------------------

// Compile-time settings: comment or uncomment as you please.

// Collect statistics at each call site.
//#define STACK_CALL_STATS

// Collect statistics about stack depth.
//#define STATS_USAGE_STATS

// Check for stack overflow when checking for free space on a chunk.
//#define STACK_PARANOID

// Print statistics and debug info.
//#define STACK_VERBOSE

// -----------------------------------------------------------------------

// Global variables.

// Current stack fingerprint.
extern int stack_fingerprint;

// Counters for tracking how often checks are executed.
extern int stack_extern;
extern int stack_check_link;
extern int stack_check_nolink;
extern int stack_nocheck;

// Counters for tracking stack use.
extern int stack_waste_ext;
extern int stack_waste_int;
extern int stack_allocchunks[];

// The current state of the stack and the free lists for various chunk sizes.
// Indices to the free list array indicate sizes (kb log2 scale).
extern void *stack_bottom;
extern void *stack_freechunks[];

// -----------------------------------------------------------------------

// Function prototypes.  The macros below should only call these functions.

// Ensure that stack_freechunks[bucket] in non-null.
extern void stack_alloc_chunk(int bucket);

// Get a chunk from the specified bucket or return a chunk when finished.
extern void *stack_get_chunk(int bucket);
extern void stack_return_chunk(int bucket, void *chunk);

// Error and stats reporting.
extern void stack_report_call_stats(void);
extern void stack_report_usage_stats(void);
extern void stack_report_link(void *chunk, int used, int node, int succ);
extern void stack_report_unlink(void *chunk);
extern void stack_report_overflow(void);
extern void stack_report_unreachable(int id, char *name);

// -----------------------------------------------------------------------

// Implement settings.

#ifdef STACK_CALL_STATS
# define STATS_CALL_INC(stat) { stat++; }
#else // ndef STACK_CALL_STATS
# define STATS_CALL_INC(stat)
#endif // STACK_CALL_STATS

#ifdef STATS_USAGE_STATS
# define STATS_USAGE_INC(stat, amt) { (stat) += (amt); }
# define STATS_USAGE_DEC(stat, amt) { (stat) -= (amt); }
#else // ndef STATS_USAGE_STATS
# define STATS_USAGE_INC(stat, amt)
# define STATS_USAGE_DEC(stat, amt)
#endif // STATS_USAGE_STATS

#ifdef STACK_PARANOID
# define CHECK_OVERFLOW() \
    { if (savesp < stack_bottom) stack_report_overflow(); }
#else // ndef STACK_PARANOID
# define CHECK_OVERFLOW()
#endif // STACK_PARANOID

#ifdef STACK_VERBOSE
# define REPORT_LINK(chunk, used, node, succ) \
    stack_report_link(chunk, used, node, succ)
# define REPORT_UNLINK(chunk) stack_report_unlink(chunk)
#else // ndef STACK_VERBOSE
# define REPORT_LINK(chunk, used, node, succ)
# define REPORT_UNLINK(chunk)
#endif // STACK_VERBOSE

// -----------------------------------------------------------------------

// Magic macro code follows.  Note that I got rid of the do { ... } while (0)
// construct because we may not be turning on optimizations that would
// eliminate the corresponding conditionals.  We don't need to use these
// macros in any scenarios where do/while would prevent a syntax error.

// -----------------------------------------------------------------------

// Private macros: don't call these directly outside of this file or stack.c.

// Get a chunk from the specified free list (bucket).
#define GET_CHUNK(bucket, chunk) \
{ \
    if (stack_freechunks[bucket] == 0) { \
        stack_alloc_chunk(bucket); \
    } \
    (chunk) = stack_freechunks[bucket]; \
    stack_freechunks[bucket] = *(((void**) (chunk)) - 1); \
    STATS_USAGE_INC(stack_allocchunks[bucket], 1); \
}

// Return a chunk to the free list.
#define RETURN_CHUNK(bucket, chunk) \
{ \
    STATS_USAGE_DEC(stack_allocchunks[bucket], 1); \
    *(((void**) (chunk)) - 1) = stack_freechunks[bucket]; \
    stack_freechunks[bucket] = (chunk); \
}

// Unconditionally link a stack chunk of the specified size.
// Note that stack_size == (1 << (bucket + 10)).  I specify them
// separately to reduce overhead when optimizations are disabled.
#define STACK_LINK_NOSTATS(stack_size, bucket) \
{ \
    GET_CHUNK(bucket, savechunk); \
    savebottom = stack_bottom; \
    stack_bottom = savechunk - (stack_size); \
    asm volatile("movl %%esp,%0" : "=g" (savesp) :); \
    savesp = (void*) ((unsigned long) savechunk - (unsigned long) savesp); \
    asm volatile("addl %0,%%esp" : : "g" (savesp) : "esp"); \
}

// Unconditionally unlink a stack chunk.
#define STACK_UNLINK_NOSTATS(bucket) \
{ \
    asm volatile("subl %0,%%esp" : : "g" (savesp) : "esp"); \
    stack_bottom = savebottom; \
    RETURN_CHUNK(bucket, savechunk); \
}

// Unconditionally link a stack chunk of the specified size, as above.
// Record some stats using the new stack chunk while we're at it.
#define STACK_LINK(stack_size, bucket, node, succ) \
{ \
    STACK_LINK_NOSTATS(stack_size, bucket); \
    STATS_USAGE_INC(stack_waste_int, ((void*) &savebottom) - stack_bottom); \
    REPORT_LINK(savechunk, \
                (((unsigned long) savechunk) - \
                 ((unsigned long) savesp) - \
                 ((unsigned long) savebottom)), \
                (node), (succ)); \
}

// Unconditionally unlink a stack chunk, with stats.
#define STACK_UNLINK(bucket) \
{ \
    REPORT_UNLINK(savechunk); \
    STATS_USAGE_DEC(stack_waste_int, ((void*) &savebottom) - stack_bottom); \
    STACK_UNLINK_NOSTATS(bucket); \
}

// -----------------------------------------------------------------------

// Public macros: this is your interface.

// Unconditionally link a stack chunk, presumably for external function stacks.
#define STACK_EXTERN_LINK(stack_size, bucket, node, succ) \
{ \
    STATS_CALL_INC(stack_extern); \
    STACK_LINK(stack_size, bucket, node, succ); \
}

// Unconditionally unlink an external function stack chunk.
#define STACK_EXTERN_UNLINK(bucket) \
{ \
    STACK_UNLINK(bucket); \
}

// Link a new stack chunk if necessary.
#define STACK_CHECK_LINK(max_path, stack_size, bucket, node, succ) \
{ \
    asm volatile("movl %%esp,%0" : "=g" (savesp) :); \
    CHECK_OVERFLOW(); \
    if ((unsigned long) (savesp - stack_bottom) <= (unsigned long) (max_path)){\
        STATS_CALL_INC(stack_check_link); \
        STACK_LINK(stack_size, bucket, node, succ); \
    } else { \
        STATS_CALL_INC(stack_check_nolink); \
        savesp = 0; \
    } \
}

// Unlink a stack chunk if the above macro linked one.
#define STACK_CHECK_UNLINK(bucket) \
{ \
    if (savesp != 0) { \
        STACK_UNLINK(bucket); \
    } \
}

// Increment counters for call sites with no stack linking.
#define STACK_NOCHECK(node, succ) \
{ \
    STATS_CALL_INC(stack_nocheck); \
}

// Report an error for a function presumed to be unreachable.
#define STACK_UNREACHABLE(id, name) \
{ \
    stack_report_unreachable(id, name); \
}

#endif // STACKLINK_H
