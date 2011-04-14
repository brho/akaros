#ifndef ROS_INCLUDE_SYSCALL_H
#define ROS_INCLUDE_SYSCALL_H

#include <arch/arch.h>
#include <ros/bits/syscall.h>
#include <ros/arch/syscall.h>
#include <ros/event.h>
#include <arch/atomic.h>

/* Flags for an individual syscall.
 * Careful, sparc can't handle flags in byte 3. */
#define SC_DONE					0x0001		/* SC is done */
#define SC_PROGRESS				0x0002		/* SC made progress */
#define SC_UEVENT				0x0004		/* user has an ev_q */

struct syscall {
	unsigned int				num;
	long						retval;
	int							err;			/* errno */
	atomic_t					flags;
	struct event_queue			*ev_q;
	void						*u_data;
	long						arg0;
	long						arg1;
	long						arg2;
	long						arg3;
	long						arg4;
	long						arg5;
};

#ifndef ROS_KERNEL

/* Attempts to block on sysc, returning when it is done or progress has been
 * made. */
void ros_syscall_blockon(struct syscall *sysc);

/* This weak version is meant to work if there is no 2LS.  For now we just
 * spin, but in the future we could block the whole process. */
static inline void __ros_syscall_blockon(struct syscall *sysc)
{
	while (!(atomic_read(&sysc->flags) & (SC_DONE | SC_PROGRESS)))
		cpu_relax();
}
weak_alias(__ros_syscall_blockon, ros_syscall_blockon);

/* TODO: make variants of __ros_syscall() based on the number of args (0 - 6) */
/* These are simple synchronous system calls, built on top of the kernel's async
 * interface.  This version makes no assumptions about errno.  You usually don't
 * want this. */
static inline long __ros_syscall(unsigned int _num, long _a0, long _a1, long _a2,
                                 long _a3, long _a4, long _a5, int *errno_loc)
{
	int num_started;	/* not used yet */
	struct syscall sysc = {0};
	sysc.num = _num;
	sysc.ev_q = 0;
	sysc.arg0 = _a0;
	sysc.arg1 = _a1;
	sysc.arg2 = _a2;
	sysc.arg3 = _a3;
	sysc.arg4 = _a4;
	sysc.arg5 = _a5;
	num_started = __ros_arch_syscall(&sysc, 1);
	/* Don't proceed til we are done */
	while (!(atomic_read(&sysc.flags) & SC_DONE))
		ros_syscall_blockon(&sysc);
	if (errno_loc)
		*errno_loc = sysc.err;
	return sysc.retval;
}

#include <errno.h>

/* This version knows about errno and will handle it. */
static inline long __ros_syscall_errno(unsigned int _num, long _a0, long _a1,
                                       long _a2, long _a3, long _a4, long _a5)
{
	return __ros_syscall(_num, _a0, _a1, _a2, _a3, _a4, _a5, &errno);
}

/* Convenience wrapper for __ros_syscall */
#define ros_syscall(which, a0, a1, a2, a3, a4, a5) \
   __ros_syscall_errno(which, (long)(a0), (long)(a1), (long)(a2), (long)(a3), \
                       (long)(a4), (long)(a5))

#endif /* ifndef ROS_KERNEL */

#endif /* !ROS_INCLUDE_SYSCALL_H */
