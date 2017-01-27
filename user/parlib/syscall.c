// System call stubs.

#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <parlib/serialize.h>

int sys_proc_destroy(int pid, int exitcode)
{
	return ros_syscall(SYS_proc_destroy, pid, exitcode, 0, 0, 0, 0);
}

size_t sys_getpcoreid(void)
{
	 return ros_syscall(SYS_getpcoreid, 0, 0, 0, 0, 0, 0);
}

int sys_null(void)
{
    return ros_syscall(SYS_null, 0, 0, 0, 0, 0, 0);
}

ssize_t sys_shared_page_alloc(void** addr, pid_t p2, 
                              int p1_flags, int p2_flags
                             ) 
{
	return ros_syscall(SYS_shared_page_alloc, addr, 
	               p2, p1_flags, p2_flags, 0, 0);
}

ssize_t sys_shared_page_free(void* addr, pid_t p2) 
{
	return ros_syscall(SYS_shared_page_free, addr, p2, 0, 0, 0, 0);
}

void sys_reboot(void)
{
	ros_syscall(SYS_reboot, 0, 0, 0, 0, 0, 0);
}

void sys_yield(bool being_nice)
{
	ros_syscall(SYS_proc_yield, being_nice, 0, 0, 0, 0, 0);
}

int sys_proc_create(const char *path, size_t path_l, char *const argv[],
                    char *const envp[], int flags)
{
	struct serialized_data *sd = serialize_argv_envp(argv, envp);
	if (!sd) {
		errno = ENOMEM;
		return -1;
	}
	int ret = ros_syscall(SYS_proc_create, path, path_l,
	                      sd->buf, sd->len, flags, 0);
	free_serialized_data(sd);
	return ret;
}

int sys_proc_run(int pid)
{
	return ros_syscall(SYS_proc_run, pid, 0, 0, 0, 0, 0);
}

void *sys_mmap(void *addr, size_t length, int prot, int flags,
               int fd, size_t offset)
{
	return (void*)ros_syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}

int sys_provision(int pid, unsigned int res_type, long res_val)
{
	return ros_syscall(SYS_provision, pid, res_type, res_val, 0, 0, 0);
}

int sys_notify(int pid, unsigned int ev_type, struct event_msg *u_msg)
{
	return ros_syscall(SYS_notify, pid, ev_type, u_msg, 0, 0, 0);
}

int sys_self_notify(uint32_t vcoreid, unsigned int ev_type,
                    struct event_msg *u_msg, bool priv)
{
	return ros_syscall(SYS_self_notify, vcoreid, ev_type, u_msg, priv, 0, 0);
}

int sys_halt_core(unsigned long usec)
{
	return ros_syscall(SYS_halt_core, usec, 0, 0, 0, 0, 0);
}

void* sys_init_arsc()
{
	return (void*)ros_syscall(SYS_init_arsc, 0, 0, 0, 0, 0, 0);
}

int sys_block(unsigned long usec)
{
	return ros_syscall(SYS_block, usec, 0, 0, 0, 0, 0);
}

/* enable_my_notif tells the kernel whether or not it is okay to turn on notifs
 * when our calling vcore 'yields'.  This controls whether or not the vcore will
 * get started from vcore_entry() or not, and whether or not remote cores need
 * to sys_change_vcore to preempt-recover the calling vcore.  Only set this to
 * FALSE if you are unable to handle starting fresh at vcore_entry().  One
 * example of this is in mcs_pdr_locks.
 *
 * Will return:
 * 		0 if we successfully changed to the target vcore.
 * 		-EBUSY if the target vcore is already mapped (a good kind of failure)
 * 		-EAGAIN if we failed for some other reason and need to try again.  For
 * 		example, the caller could be preempted, and we never even attempted to
 * 		change.
 * 		-EINVAL some userspace bug */
int sys_change_vcore(uint32_t vcoreid, bool enable_my_notif)
{
	/* Since we might be asking to start up on a fresh stack (if
	 * enable_my_notif), we need to use some non-stack memory for the struct
	 * sysc.  Our vcore could get restarted before the syscall finishes (after
	 * unlocking the proc, before finish_sysc()), and the act of finishing would
	 * write onto our stack.  Thus we use the per-vcore struct. */
	int flags;
	/* Need to wait while a previous syscall is not done or locked.  Since this
	 * should only be called from VC ctx, we'll just spin.  Should be extremely
	 * rare.  Note flags is initialized to SC_DONE. */
	do {
		cpu_relax();
		flags = atomic_read(&__vcore_one_sysc.flags);
	} while (!(flags & SC_DONE) || flags & SC_K_LOCK);
	__vcore_one_sysc.num = SYS_change_vcore;
	__vcore_one_sysc.arg0 = vcoreid;
	__vcore_one_sysc.arg1 = enable_my_notif;
	/* keep in sync with glibc sysdeps/ros/syscall.c */
	__ros_arch_syscall((long)&__vcore_one_sysc, 1);
	/* If we returned, either we wanted to (!enable_my_notif) or we failed.
	 * Need to wait til the sysc is finished to find out why.  Again, its okay
	 * to just spin. */
	do {
		cpu_relax();
		flags = atomic_read(&__vcore_one_sysc.flags);
	} while (!(flags & SC_DONE) || flags & SC_K_LOCK);
	return __vcore_one_sysc.retval;
}

int sys_change_to_m(void)
{
	return ros_syscall(SYS_change_to_m, 0, 0, 0, 0, 0, 0);
}

int sys_poke_ksched(int pid, unsigned int res_type)
{
	return ros_syscall(SYS_poke_ksched, pid, res_type, 0, 0, 0, 0);
}

int sys_abort_sysc(struct syscall *sysc)
{
	return ros_syscall(SYS_abort_sysc, sysc, 0, 0, 0, 0, 0);
}

int sys_abort_sysc_fd(int fd)
{
	return ros_syscall(SYS_abort_sysc_fd, fd, 0, 0, 0, 0, 0);
}

int sys_tap_fds(struct fd_tap_req *tap_reqs, size_t nr_reqs)
{
	return ros_syscall(SYS_tap_fds, tap_reqs, nr_reqs, 0, 0, 0, 0);
}

void syscall_async(struct syscall *sysc, unsigned long num, ...)
{
	va_list args;

	sysc->num = num;
	sysc->flags = 0;
	sysc->ev_q = 0;		/* not necessary, but good for debugging */
	/* This is a little dangerous, since we'll usually pull more args than were
	 * passed in, ultimately reading gibberish off the stack. */
	va_start(args, num);
	sysc->arg0 = va_arg(args, long);
	sysc->arg1 = va_arg(args, long);
	sysc->arg2 = va_arg(args, long);
	sysc->arg3 = va_arg(args, long);
	sysc->arg4 = va_arg(args, long);
	sysc->arg5 = va_arg(args, long);
	va_end(args);
	__ros_arch_syscall((long)sysc, 1);
}

void syscall_async_evq(struct syscall *sysc, struct event_queue *evq,
                       unsigned long num, ...)
{
	va_list args;

	sysc->num = num;
	atomic_set(&sysc->flags, SC_UEVENT);
	sysc->ev_q = evq;
	/* This is a little dangerous, since we'll usually pull more args than were
	 * passed in, ultimately reading gibberish off the stack. */
	va_start(args, num);
	sysc->arg0 = va_arg(args, long);
	sysc->arg1 = va_arg(args, long);
	sysc->arg2 = va_arg(args, long);
	sysc->arg3 = va_arg(args, long);
	sysc->arg4 = va_arg(args, long);
	sysc->arg5 = va_arg(args, long);
	va_end(args);
	__ros_arch_syscall((long)sysc, 1);
}
