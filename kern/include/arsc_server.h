/*
 * Copyright (c) 2009 The Regents of the University  of California.  
 * See the COPYRIGHT files at the top of this source tree for full 
 * license information.
 */
#ifndef _ROS_ARSC_SERVER_H
#define __ROS_ARSC_SERVER_H
#include <ros/common.h>
#include <ros/ring_syscall.h>
#include <arch/types.h>
#include <arch/arch.h>

#include <process.h>
#include <syscall.h>
#include <error.h>

extern struct proc_list arsc_proc_list;
extern spinlock_t arsc_proc_lock;

syscall_sring_t* sys_init_arsc(struct proc* p);
intreg_t syscall_async(struct proc* p, syscall_req_t *syscall);
void arsc_server(trapframe_t *tf);

static intreg_t process_generic_syscalls(struct proc* p, size_t max);
#endif //ARSC_SERVER
