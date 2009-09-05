#ifdef __IVY__

#include <stdio.h>
#include <string.h>
#include <lib.h>

// Use inline even when not optimizing for speed, since it prevents
// warnings that would occur due to unused static functions.
#ifdef DEPUTY_ALWAYS_STOP_ON_ERROR
  #define INLINE inline __attribute__((always_inline))
#else
  #define INLINE inline
#endif

#define __LOCATION__        __FILE__, __LINE__, __FUNCTION__
#define __LOCATION__FORMALS const char* file, int line, const char* func
#define __LOCATION__ACTUALS file, line, func

#ifndef asmlinkage
#define asmlinkage __attribute__((regparm(0)))
#endif

#ifndef noreturn
#define noreturn __attribute__((noreturn))
#endif

#define IN_DEPUTY_LIBRARY

#include "deputy/checks.h"

// If Deputy fails, it checks to see if you have specified special handling
// for failed checks.
enum handlerKind {
    HANDLE_DEFAULT, // no handler specified
    HANDLE_IGNORE,  // ignore
    HANDLE_STOP,    // warn and stop
    HANDLE_WARN,    // warn but do not stop
    HANDLE_SLEEP,   // sleep and wait for the the debugger
};

static enum handlerKind deputyErrorHandler = HANDLE_DEFAULT;

static int deputyInited = 0;
static void deputyInitOnFirstFailure(void) {
    // Cache the result of environment lookup
    //char *onerror = getenv("DEPUTY_ONERROR");
    deputyErrorHandler = HANDLE_DEFAULT;
/*
    if (onerror) {
        if (!strcasecmp(onerror, "stop")) {
            deputyErrorHandler = HANDLE_STOP;
        } else if (!strcasecmp(onerror, "ignore")) {
            deputyErrorHandler = HANDLE_IGNORE;
        } else if (!strcasecmp(onerror, "warn")) {
            deputyErrorHandler = HANDLE_WARN;
        } else if (!strcasecmp(onerror, "sleep")) {
            deputyErrorHandler = HANDLE_SLEEP;
        } else {
            fprintf(stderr, "Unexpected value for env var DEPUTY_ONERROR\n");
        }
    }
*/
}

// This function is called directly from the checks unless the code was
// compiled with --deputyAlwaysStopOnError . It is also called from
// deputy_fail_noreturn to do the real work.
asmlinkage
void deputy_fail_mayreturn(const char *check, const char *text,
                           __LOCATION__FORMALS) {
    if (!deputyInited) {
        deputyInitOnFirstFailure(); deputyInited = 1;
    }
    if (deputyErrorHandler != HANDLE_IGNORE) {
        cprintf("%s:%d: %s: Assertion failed in %s:\n  %s\n",
                __LOCATION__ACTUALS, check, text);
    }
    // Now look whether we should return or not
    switch (deputyErrorHandler) {
        case HANDLE_DEFAULT:
        case HANDLE_STOP:
            cprintf("Execution aborted.\n");
            exit(/*1*/);
#if 0
        case HANDLE_SLEEP:
#if defined(_MSVC)
            // On Windows, this will fire the just-in-time debugger.
            _asm { int 3 }
#else
            // Pause for debugging multithreaded code.
            // (Trick due to Ben Liblit, via Dan Wilkerson.)
            {
                volatile int stopped = 1;

                fprintf(stderr, "Execution paused for debugging (pid = %d).\n",
                        getpid());
                fflush(stderr);

                while (stopped) {
                    // 1. Execution will stop here.
                    sleep(1);
                }

                // 2. Set breakpoint here, and set stopped to 0.
                // (This statement exists so we have a place to put
                // that breakpoint.)
                stopped ++;
            }
#endif
            break;
#endif /* 0 */
        default:
            break;
    }
}

// This function must NOT return. This is the function that is called
// by the checks if we compiled with --alwaysStopOnError
asmlinkage noreturn
void deputy_fail_noreturn(const char *check, const char *text,
                          __LOCATION__FORMALS) {
    deputy_fail_mayreturn(check, text, __LOCATION__ACTUALS);
    cprintf("Deputy check failed; execution aborted.\n");
    exit(/*1*/);
}

// This function must NOT return. This is the function that is called
// by the checks if we compiled with --alwaysStopOnError and --deputyFastChecks
asmlinkage noreturn
void deputy_fail_noreturn_fast(void) {
    cprintf("Deputy check failed; execution aborted.\n");
    cprintf("Detailed error report suppressed (--deputyFastChecks).\n");
    exit(/*1*/);
}

/* Search for a NULL starting at e and return its index */
asmlinkage
int deputy_findnull(const void *e, unsigned int bytes) {
#define NULLCHECK(type) \
    do { \
        type *p = (type*) e; \
        while (*p != 0) { \
            p++; \
        } \
        length = (p - (type*) e); \
    } while (0)

    int length = 0;

    switch (bytes) {
        case 1:
            NULLCHECK(char);
            break;
        case 2:
            NULLCHECK(short);
            break;
        case 4:
            NULLCHECK(long);
            break;
        default:
           cprintf("Invalid byte size for nullcheck.\n");
           exit(/*1*/);
    }

    return length;
#undef NULLCHECK
}

asmlinkage
void *__deputy_memset(void *s, int c, unsigned int n) {
  return memset(s, c, n);
}
#endif // __IVY__
