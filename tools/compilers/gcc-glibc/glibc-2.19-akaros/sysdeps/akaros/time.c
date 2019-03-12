#include <time.h>
#include <stdbool.h>
#include <sys/time.h>
#include <parlib/timing.h>
#include <ros/procinfo.h>

time_t
time(time_t* p)
{
  struct timeval t;
  int ret = gettimeofday(&t,0);
  if(ret == -1)
    return (time_t)-1;

  time_t ti = t.tv_sec;
  if(p)
    *p = ti;
  return ti;
}
libc_hidden_def(time)

/* Adds normal timespecs */
void add_timespecs(struct timespec *sum, const struct timespec *x,
                   const struct timespec *y)
{
	bool plus_one = false;

	sum->tv_nsec = x->tv_nsec + y->tv_nsec;
	/* Overflow detection */
	if (sum->tv_nsec / 1000000000) {
		sum->tv_nsec -= 1000000000;
		plus_one = true;
	}
	sum->tv_sec = x->tv_sec + y->tv_sec + (plus_one ? 1 : 0);
}

/* Subtracts normal timespecs */
void subtract_timespecs(struct timespec *diff, const struct timespec *minuend,
                        const struct timespec *subtrahend)
{
	unsigned long borrow_amt = 0;

	if (minuend->tv_nsec < subtrahend->tv_nsec)
		borrow_amt = 1000000000;
	diff->tv_nsec = borrow_amt + minuend->tv_nsec - subtrahend->tv_nsec;
	diff->tv_sec = minuend->tv_sec - subtrahend->tv_sec
		       - (borrow_amt ? 1 : 0);
}

/* Declared in parlib/timing.h */

uint64_t epoch_nsec_to_tsc(uint64_t epoch_ns)
{
	return nsec2tsc(epoch_ns - __proc_global_info.walltime_ns_last) +
	       __proc_global_info.tsc_cycles_last;
}

uint64_t tsc_to_epoch_nsec(uint64_t tsc)
{
	return tsc2nsec(tsc - __proc_global_info.tsc_cycles_last) +
	       __proc_global_info.walltime_ns_last;
}

uint64_t epoch_nsec(void)
{
	/* in case we get called before the constructor.  it's a little racy,
	 * but this all happens when we're single threaded.  for those curious,
	 * this seems to happen a lot due to the proliferation of gettimeofday
	 * calls. */
	return tsc_to_epoch_nsec(read_tsc());
}
