\
#ifndef READPROC_H
#define READPROC_H

typedef struct {
  int pid;
  char comm[256]; // FIXME: a really long command name would mess things up
  char state;
  int ppid;
  int pgrp;
  int session;
  int tty_nr;
  int tty_pgrp;

  unsigned long flags;
  unsigned long min_flt;
  unsigned long cmin_flt;
  unsigned long maj_flt;
  unsigned long cmaj_flt;
  unsigned long tms_utime;
  unsigned long tms_stime;
  long tms_cutime;
  long tms_cstime;

  long priority;
  long nice;
  long removed; 
  long it_real_value;
  unsigned long long start_time; // just unsigned long in 2.4
  
  unsigned long vsize;
  long rss;
  unsigned long rss_rlim_cur;
  unsigned long start_code;
  unsigned long end_code;
  unsigned long start_stack;
  unsigned long esp;
  unsigned long eip;

  unsigned long pending_sig; // obsolete - use /proc/self/status
  unsigned long blocked_sig; // obsolete - use /proc/self/status
  unsigned long sigign;      // obsolete - use /proc/self/status
  unsigned long sigcatch;    // obsolete - use /proc/self/status
  unsigned long wchan;

  unsigned long nswap;
  unsigned long cnswap;
  int exit_signal;
  int cpu_num;

  unsigned long rt_priority; // new in 2.5
  unsigned long policy;      // new in 2.5

} proc_self_stat_t;


typedef struct {
  int pages_in;
  int pages_out;
  int pages_swapin;
  int pages_swapout;
} proc_global_stats_t;

void refresh_process_stats(proc_self_stat_t *stat);
void refresh_global_stats(proc_global_stats_t *stat);

#endif // READPROC_H
