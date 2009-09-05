#ifndef ROS_KERN_SYSCALL_H
#define ROS_KERN_SYSCALL_H
#ifndef ROS_KERNEL
# error "This is ROS kernel header; user programs should not #include it"
#endif

#include <ros/syscall.h>
#include <process.h>

intreg_t (syscall)(env_t* e, uintreg_t num, uintreg_t a1, uintreg_t a2,
                   uintreg_t a3, uintreg_t a4, uintreg_t a5);
intreg_t syscall_async(env_t* e, syscall_req_t *syscall);
intreg_t process_generic_syscalls(env_t* e, uint32_t max);
#endif /* !ROS_KERN_SYSCALL_H */
