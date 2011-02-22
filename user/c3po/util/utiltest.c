/**
 * test prog for util functions
 **/

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "util.h"

#ifndef DEBUG_utiltest_c
#undef debug
#define debug(...)
#undef tdebug
#define tdebug(...)
#endif


void pointer_list_test(void)
{
  pointer_list_t *pl = new_pointer_list("test");
  void *data;

  debug("checking debug: %d %s %d\n", 1, "foo", 3);

  pl_add_tail(pl,NULL);
  data = pl_remove_head(pl);  assert(data == NULL);
  data = pl_remove_head(pl);  assert(data == (void*)-1);

  pl_add_tail(pl,NULL);
  pl_add_tail(pl,NULL);
  data = pl_remove_head(pl);  assert(data == NULL);
  pl_add_tail(pl,NULL);
  data = pl_remove_head(pl);  assert(data == NULL);
  data = pl_remove_head(pl);  assert(data == NULL);
  data = pl_remove_head(pl);  assert(data == (void*)-1);

  pl_add_tail(pl,(void*)1);
  pl_add_tail(pl,(void*)2);
  pl_add_tail(pl,(void*)3);
  data = pl_remove_head(pl);  assert(data == (void*)1);
  data = pl_remove_head(pl);  assert(data == (void*)2);
  data = pl_remove_head(pl);  assert(data == (void*)3);
  data = pl_remove_head(pl);  assert(data == (void*)-1);

  printf("pointer_list_test:   passed\n");
}


void clock_test()
{
  struct timeval stv, etv;
  long long elapsed;
  cpu_tick_t start, end, begin;
  int i;

  printf("ticks_per_second:      %lld\n",ticks_per_second);
  printf("ticks_per_millisecond: %lld\n",ticks_per_millisecond);
  printf("ticks_per_microsecond: %lld\n",ticks_per_microsecond);
  printf("ticks_per_nanosecond:  %lld\n",ticks_per_nanosecond);


  GET_CPU_TICKS( begin );

#define LOOPS 10000
  gettimeofday(&stv, NULL);
  GET_CPU_TICKS( start );
  for(i=0; i<LOOPS; i++) {
    GET_CPU_TICKS( end );
  }
  gettimeofday(&etv, NULL);
  GET_CPU_TICKS( end );

  if( stv.tv_sec == etv.tv_sec )
    elapsed = etv.tv_usec - stv.tv_usec;
  else 
    elapsed = 1e6*(long long)(etv.tv_sec - stv.tv_sec)
      + etv.tv_usec + (1e6 - stv.tv_usec);

  printf("start: %lld   end: %lld\n", start-begin, end-begin);
  printf("elapsed time:   %lld usec   %lld ticks == %lld usec\n",
         elapsed, (end-start), (end-start) / ticks_per_microsecond);

  
  elapsed = end-start;
  GET_CPU_TICKS( start );
  for(i=0; i<LOOPS; i++)
    ;
  GET_CPU_TICKS( end );
  printf("start: %lld   end: %lld\n", start-begin, end-begin);
  printf("cycles per check: %lld\n", (elapsed - (end-start)) / LOOPS);
  
}


int main(int argc, char **argv)
{
  (void) argc;
  (void) argv;

  pointer_list_test();
  clock_test();

  return 0;
}
