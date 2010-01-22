#ifndef ROS_KERN_SYSCALL_H
#define ROS_KERN_SYSCALL_H
#ifndef ROS_KERNEL
# error "This is ROS kernel header; user programs should not #include it"
#endif

#include <ros/syscall.h>
#include <process.h>

intreg_t syscall(struct proc *p, uintreg_t num, uintreg_t a1, uintreg_t a2,
                 uintreg_t a3, uintreg_t a4, uintreg_t a5);
intreg_t syscall_async(env_t* e, syscall_req_t *syscall);
intreg_t process_generic_syscalls(env_t* e, size_t max);

intreg_t sys_read(struct proc* p, int fd, void* buf, int len);
intreg_t sys_write(struct proc* p, int fd, const void* buf, int len);
intreg_t sys_open(struct proc* p, const char* path);
intreg_t sys_close(struct proc* p, int fd);

#endif /* !ROS_KERN_SYSCALL_H */
