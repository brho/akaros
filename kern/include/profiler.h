/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <stdio.h>
#include <ros/profiler_records.h>

struct hw_trapframe;
struct proc;
struct file;
struct cmdbuf;

int profiler_configure(struct cmdbuf *cb);
void profiler_append_configure_usage(char *msgbuf, size_t buflen);
void profiler_init(void);
void profiler_setup(void);
void profiler_cleanup(void);
void profiler_add_kernel_backtrace(uintptr_t pc, uintptr_t fp, uint64_t info);
void profiler_add_user_backtrace(uintptr_t pc, uintptr_t fp, uint64_t info);
void profiler_add_trace(uintptr_t pc, uint64_t info);
void profiler_control_trace(int onoff);
void profiler_trace_data_flush(void);
void profiler_add_hw_sample(struct hw_trapframe *hw_tf, uint64_t info);
int profiler_size(void);
int profiler_read(void *va, int n);
void profiler_notify_mmap(struct proc *p, uintptr_t addr, size_t size, int prot,
						  int flags, struct file *f, size_t offset);
void profiler_notify_new_process(struct proc *p);
