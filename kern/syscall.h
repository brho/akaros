#ifndef ROS_KERN_SYSCALL_H
#define ROS_KERN_SYSCALL_H
#ifndef ROS_KERNEL
# error "This is ROS kernel header; user programs should not #include it"
#endif

#include <inc/syscall.h>
#include <inc/env.h>

uint32_t (SYNCHRONOUS syscall)(uint32_t num, uint32_t a1, uint32_t a2,
                               uint32_t a3, uint32_t a4, uint32_t a5);
uint32_t syscall_async(syscall_req_t *syscall);
uint32_t process_generic_syscalls(env_t* e, uint32_t max);

#endif /* !ROS_KERN_SYSCALL_H */
