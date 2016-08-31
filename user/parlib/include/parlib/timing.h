#pragma once

#include <stdint.h>
#include <parlib/tsc-compat.h>

__BEGIN_DECLS

uint64_t udelay(uint64_t usec);
uint64_t ndelay(uint64_t nsec);
uint64_t udiff(uint64_t begin, uint64_t end);
uint64_t ndiff(uint64_t begin, uint64_t end);

/* Conversion btw tsc ticks and time units.  From Akaros's kern/src/time.c */
uint64_t tsc2sec(uint64_t tsc_time);
uint64_t tsc2msec(uint64_t tsc_time);
uint64_t tsc2usec(uint64_t tsc_time);
uint64_t tsc2nsec(uint64_t tsc_time);
uint64_t sec2tsc(uint64_t sec);
uint64_t msec2tsc(uint64_t msec);
uint64_t usec2tsc(uint64_t usec);
uint64_t nsec2tsc(uint64_t nsec);
uint64_t nsec(void);

__END_DECLS
