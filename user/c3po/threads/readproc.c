
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <ros/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "util.h"
#include "readproc.h"




/**
 * read data from a file in /proc, caching the FD if that seems to work.
 **/
int read_proc_file(int *fdp, char *name, char *buf, int len)
{
  register int ret;
  register int fd = *fdp;

  // open the proc file
  if( fd < 0 ) {
    fd = syscall(SYS_open,name,O_RDONLY); 
    if(fd < 0) {
      warning("error opening %s: %s\n",name,strerror(errno));
      *fdp = -2;
      return -1;
    }
    *fdp = fd;
  }

  // read
  // FIXME: pread should be better, but it always returns 0 bytes on my home system.  Why?
#define USE_PREAD 0
#if USE_PREAD
  ret = syscall(SYS_pread, fd, buf, len, 0);
#else
  /*  if( syscall(SYS_lseek, fd, 0, SEEK_SET) < 0 ) {
    warning("error w/ lseek: %s\n", strerror(errno));
    close(fd);
    *fdp = -2; 
    return -1;
    }  */
  ret = syscall(SYS_read, fd, buf, len);
  // FIXME: fd caching seems to flake out someitmes - why??
  syscall(SYS_close, fd);
  *fdp = -1;
#endif

  if( ret < 50 ) {
    warning("too little data in %s (%d bytes)\n",name,ret);
    if( fd >= 0 ) syscall(SYS_close, fd);
    *fdp = -1;
    return -1;
  }

  return ret;
}


/**
 * Read stats from /proc/self/stat.
 *
 * FIXME: the FD caching may not work correctly for cloned children -
 * check on this later.
 **/
void refresh_process_stats(proc_self_stat_t *stat)
{
  static int fd = -1;
  char buf[256];
  int ret;
  //int len;

  ret = read_proc_file(&fd, "/proc/self/stat", buf, sizeof(buf));
  if( ret < 0 ) return;
  if( ret == sizeof(buf) ) {
    warning("too much data in /proc/self/stat - can't get valid stats\n"); 
    if( fd >= 0) close(fd);
    fd = -2;
    return;
  }
  
  // parse.  We could do this faster ourselves, by not keeping all of
  // this data.  The speed shouldn't really matter, though, since this
  // routine should only be called a few times / second.
  ret = sscanf(buf, "%d (%[^)]) %c %d %d %d %d %d %lu %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu",

               &stat->pid, 
               &stat->comm[0],
               &stat->state,
               &stat->ppid,
               &stat->pgrp,
               &stat->session,
               &stat->tty_nr,
               &stat->tty_pgrp,

               &stat->flags,
               &stat->min_flt,
               &stat->cmin_flt,
               &stat->maj_flt,
               &stat->cmaj_flt,
               &stat->tms_utime,
               &stat->tms_stime,
               &stat->tms_cutime,
               &stat->tms_cstime,

               &stat->priority,
               &stat->nice,
               &stat->removed, 
               &stat->it_real_value,
               &stat->start_time, // just unsigned long in 2.4
  
               &stat->vsize,
               &stat->rss,
               &stat->rss_rlim_cur,
               &stat->start_code,
               &stat->end_code,
               &stat->start_stack,
               &stat->esp,
               &stat->eip,

               &stat->pending_sig, // obsolete - use /proc/self/status
               &stat->blocked_sig, // obsolete - use /proc/self/status
               &stat->sigign,      // obsolete - use /proc/self/status
               &stat->sigcatch,    // obsolete - use /proc/self/status
               &stat->wchan,

               &stat->nswap,
               &stat->cnswap,
               &stat->exit_signal,
               &stat->cpu_num,
               &stat->rt_priority, // new in 2.5
               &stat->policy      // new in 2.5
               );

  warning("stats: %d (%s)... %ld %ld   %ld  ret=%d\n", 
          stat->pid, 
          stat->comm,
          stat->vsize,
          stat->rss,
          stat->maj_flt,
          ret);
  
  if( ret != 39 && ret != 41 ) {
    warning("error parsing /proc/self/stat - got %d items\n", ret);
    //bzero(stat, sizeof(proc_self_stat_t));
    if( fd >= 0) close(fd);
    fd = -1;
    return;
  }
}




void refresh_global_stats(proc_global_stats_t *stat)
{
  static int proc_stat_fd = -1;
  char buf[1024], *p, *end;
  int ret;

  ret = read_proc_file(&proc_stat_fd, "/proc/stat", buf, sizeof(buf)-1);
  if( ret < 0 ) return;
  buf[ret] = '\0';


  // FIXME: parse CPU info (?)

  // parse pageing activity
  p = strstr(buf,"page ");
  if( p == NULL ) {warning("couldn't find page data in /proc/stat!\n"); return;}

  p += 5; // strlen("page ");
  stat->pages_in = strtol(p, &end, 0);
  if( p == end ) {warning("bad pagein data\n"); return;}
  
  p = end;
  stat->pages_out = strtol(p, &end, 0);
  if( p == end ) {warning("bad pagein data\n"); return;}

  
  // parse swap file activity
  p = strstr(buf,"swap ");
  if( p == NULL ) {warning("couldn't find swap data in /proc/stat!\n"); return;}

  p += 5; // strlen("swap ");
  stat->pages_swapin = strtol(p, &end, 0);
  if( p == end ) {warning("bad pswapin data\n"); return;}
  
  p = end;
  stat->pages_swapout = strtol(p, &end, 0);
  if( p == end ) {warning("bad pswapout data\n"); return;}


  warning("gstats:   %d %d   %d %d\n",
          stat->pages_in, stat->pages_out,
          stat->pages_swapin, stat->pages_swapout);
}


#ifdef READPROC_DEFINE_MAIN


void printstats(proc_self_stat_t *stat)
{
  printf("%s:  vsize=%lu  rss=%ld  min_flt=%lu  maj_flt=%lu\n", 
         stat->comm, stat->vsize/1024/1024, stat->rss*4/1024,
         stat->min_flt, stat->maj_flt);
}


int main(int argc, char **argv)
{
  proc_self_stat_t selfstats;
  (void) argc; (void) argv;

  refresh_process_stats(&selfstats); printstats(&selfstats);

  malloc(1024*1024);
  refresh_process_stats(&selfstats);
  printstats(&selfstats);

  {
    char *p = malloc(1024*1024);
    int i;

    refresh_process_stats(&selfstats); printstats(&selfstats);
    for( i=0; i<1024; i++ )
      p[i*1024] = 4;
    refresh_process_stats(&selfstats); printstats(&selfstats);
  }

  malloc(1024*1024);
  refresh_process_stats(&selfstats); printstats(&selfstats);

}


#endif
