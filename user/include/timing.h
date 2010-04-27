#ifndef __PARLIB_TIMING_H__
#define __PARLIB_TIMING_H__

void udelay(uint64_t usec);
uint64_t udiff(uint64_t begin, uint64_t end);
uint64_t ndiff(uint64_t begin, uint64_t end);

#endif /* __PARLIB_TIMING_H__ */
