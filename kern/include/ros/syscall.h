#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

#include <arch/arch.h>
#include <ros/bits/syscall.h>
#include <ros/arch/syscall.h>

/* Flags for an individual syscall */
#define SC_DONE					0x0001

struct syscall {
	unsigned int				num;
	long						retval;
	int							err;			/* errno */
	int							flags;
	long						arg0;
	long						arg1;
	long						arg2;
	long						arg3;
	long						arg4;
	long						arg5;
};

#ifndef ROS_KERNEL

/* These are simple synchronous system calls, built on top of the kernel's async
 * interface.  This version makes no assumptions about errno.  You usually don't
 * want this. */
static inline long __ros_syscall(long _num, long _a0, long _a1, long _a2,
                                 long _a3, long _a4, int *errno_loc)
{
	int num_started;
	struct syscall sysc = {0};
	sysc.num = _num;
	sysc.arg0 = _a0;
	sysc.arg1 = _a1;
	sysc.arg2 = _a2;
	sysc.arg3 = _a3;
	sysc.arg4 = _a4;
	// TODO: arg5
	num_started = __ros_arch_syscall(&sysc, 1, 0, 0, 0, 0);
	while (!(sysc.flags & SC_DONE))
		cpu_relax();
	if (errno_loc)
		*errno_loc = sysc.err;
	return sysc.retval;
}

#include <errno.h>

/* This version knows about errno and will handle it. */
static inline long __ros_syscall_errno(long _num, long _a0, long _a1, long _a2,
                                       long _a3, long _a4)
{
	return __ros_syscall(_num, _a0, _a1, _a2, _a3, _a4, &errno);
}

/* Convenience wrapper for __ros_syscall */
#define ros_syscall(which,a0,a1,a2,a3,a4) \
   __ros_syscall_errno(which, (long)(a0), (long)(a1), (long)(a2), (long)(a3), \
                       (long)(a4))

#endif /* ifndef ROS_KERNEL */

#endif /* !ROS_INCLUDE_SYSCALL_H */
