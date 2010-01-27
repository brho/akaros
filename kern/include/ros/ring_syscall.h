#ifndef _ROS_RING_SYSCALL_H
#define _ROS_RING_SYSCALL_H

#include <ros/common.h>
#include <ros/ring_buffer.h>
#include <ros/sysevent.h>

#define NUM_SYSCALL_ARGS 6
typedef struct syscall_req {
        uint32_t num;
        uint32_t flags;
        uint32_t args[NUM_SYSCALL_ARGS];
} syscall_req_t;

typedef struct syscall_rsp {
        int32_t retval;
} syscall_rsp_t;

// Generic Syscall Ring Buffer
#define SYSCALLRINGSIZE    PGSIZE
//DEFINE_RING_TYPES_WITH_SIZE(syscall, syscall_req_t, syscall_rsp_t,
//SYSCALLRINGSIZE);
DEFINE_RING_TYPES(syscall, syscall_req_t, syscall_rsp_t);

#endif
