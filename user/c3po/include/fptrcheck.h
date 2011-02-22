#ifndef FPTR_CHECK_H
#define FPTR_CHECK_H

// -----------------------------------------------------------------------

// fptrcheck.h
//
// This module implements a sanity check for the graph analysis used by
// the stack module.  It provides instrumentation for function pointer
// call sites and function entry points in order to determine whether
// our graph was missing edges.
//
// These checks are enabled or disabled by the analyzegraph program.

// -----------------------------------------------------------------------

extern int fptr_caller;

#define FPTR_CALL(cur) \
    do { \
        if (fptr_caller != -1) { \
            fptr_report_unvalidated_overwrite(cur); \
        } \
        fptr_caller = (cur); \
    } while (0)

#define FPTR_CHECK(cur, a) \
    do { \
        if (fptr_caller != -1) { \
            int *p = (a); \
            while (*p != 0) { \
                if (*p == fptr_caller) { \
                    fptr_caller = -1; \
                    break; \
                } \
                p++; \
            } \
            if (fptr_caller != -1) { \
                fptr_report_unexpected_call(cur); \
                fptr_caller = -1; \
            } \
        } \
    } while (0)

#define FPTR_DONE(cur) \
    do { \
        if (fptr_caller != -1) { \
            fptr_report_unvalidated_return(cur); \
            fptr_caller = -1; \
        } \
    } while (0)

#endif // FPTR_CHECK_H
