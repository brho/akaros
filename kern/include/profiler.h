/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Synchronization is a little hokey - we assume the external caller (kprof)
 * makes sure that there is only one call to profiler_setup(), followed by a
 * call to profiler_cleanup().  Between these calls, that thread can make
 * serialized calls to profiler_{start,stop,trace_data_flush}(). */

#pragma once

#include <stdio.h>
#include <ros/profiler_records.h>

struct proc;
struct file_or_chan;
struct cmdbuf;

/* Caller (kprof) ensures at most one call to setup and then cleanup. */
int profiler_setup(void);
void profiler_cleanup(void);

/* Call these one at a time after setup and before cleanup. */
void profiler_start(void);
void profiler_stop(void);
void profiler_trace_data_flush(void);

/* Call these anytime.  If the profiler is off, they will be ignored.  Some
 * configure options won't take effect until the next profiler run. */
int profiler_configure(struct cmdbuf *cb);
void profiler_append_configure_usage(char *msgbuf, size_t buflen);

void profiler_push_kernel_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                    uint64_t info);
void profiler_push_user_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                  uint64_t info);
size_t profiler_size(void);
size_t profiler_read(void *va, size_t n);
void profiler_notify_mmap(struct proc *p, uintptr_t addr, size_t size, int prot,
			  int flags, struct file_or_chan *foc, size_t offset);
void profiler_notify_new_process(struct proc *p);
