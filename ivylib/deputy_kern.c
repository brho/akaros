#include <inc/stdio.h>
#include <inc/assert.h>
#include <inc/string.h>

#define IN_DEPUTY_LIBRARY

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

asmlinkage
void deputy_fail_mayreturn(const char *check, const char *text,
                           __LOCATION__FORMALS) {
    cprintf("%s:%d: %s: Assertion failed in %s: %s\n",
            __LOCATION__ACTUALS, check, text);
/*
    dump_stack();
*/
}

asmlinkage noreturn
void deputy_fail_noreturn_fast(void) {
    panic("Deputy assertion failure\n");
}

int deputy_strlen(const char *str) {
    return strlen(str);
}

char *deputy_strcpy(char *dest, const char *src) {
    char *tmp = dest;
    while ((*dest++ = *src++) != '\0') {
        // do nothing
    }
    return tmp;
}

char *deputy_strncpy(char *dest, const char *src, size_t count) {
    char *tmp = dest;
    int c = count;
    while (c >= 0) {
        if ((*tmp = *src) != 0) src++;
        tmp++;
        c--;
    }
    return dest;
}

/* Search for a NULL starting at e and return its index */
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
            break;
    }

    return length;
#undef NULLCHECK
}

void *__deputy_memset(void *s, int c, unsigned int n) {
  return memset(s, c, n);
}
