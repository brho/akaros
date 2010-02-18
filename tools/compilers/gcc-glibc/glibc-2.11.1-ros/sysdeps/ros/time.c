#include <time.h>
#include <sys/time.h>

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
