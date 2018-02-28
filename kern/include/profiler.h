/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <stdio.h>
#include <ros/profiler_records.h>

struct hw_trapframe;
struct proc;
struct file_or_chan;
struct cmdbuf;

int profiler_configure(struct cmdbuf *cb);
void profiler_append_configure_usage(char *msgbuf, size_t buflen);
void profiler_init(void);
void profiler_setup(void);
void profiler_cleanup(void);
void profiler_start(void);
void profiler_stop(void);
void profiler_push_kernel_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                    uint64_t info);
void profiler_push_user_backtrace(uintptr_t *pc_list, size_t nr_pcs,
                                  uint64_t info);
void profiler_trace_data_flush(void);
int profiler_size(void);
int profiler_read(void *va, int n);
void profiler_notify_mmap(struct proc *p, uintptr_t addr, size_t size, int prot,
						  int flags, struct file_or_chan *foc, size_t offset);
void profiler_notify_new_process(struct proc *p);
