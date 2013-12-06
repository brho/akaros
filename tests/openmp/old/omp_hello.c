#include <sys/sysinfo.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <vcore.h>

int main (int argc, char *argv[]) 
{
  int nthreads, tid;
  printf("get_nprocs: %d\n", get_nprocs());
  printf("SC_NPROCESSORS_ONLN: %d\n", sysconf (_SC_NPROCESSORS_ONLN));
  printf("max num vcores: %d\n", max_vcores());

/* Fork a team of threads giving them their own copies of variables */
#pragma omp parallel private(nthreads, tid)
  {

  /* Obtain thread number */
  tid = omp_get_thread_num();
  printf("Hello World from thread = %d\n", tid);

  /* Only master thread does this */
  if (tid == 0) 
    {
    nthreads = omp_get_num_threads();
    printf("Number of threads = %d\n", nthreads);
    }

  }  /* All threads join master thread and disband */

}
