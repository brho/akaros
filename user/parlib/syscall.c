// System call stubs.

#include <parlib.h>

int sys_proc_destroy(int pid, int exitcode)
{
	return ros_syscall(SYS_proc_destroy, pid, exitcode, 0, 0, 0);
}

void* sys_brk(void* addr)
{
	return (void*)ros_syscall(SYS_brk, addr, 0, 0, 0, 0);
}

int sys_getpid(void)
{
	 return ros_syscall(SYS_getpid, 0, 0, 0, 0, 0);
}

size_t sys_getcpuid(void)
{
	 return ros_syscall(SYS_getcpuid, 0, 0, 0, 0, 0);
}

ssize_t sys_cputs(const uint8_t *s, size_t len)
{
    return ros_syscall(SYS_cputs, s,  len, 0, 0, 0);
}

uint16_t sys_cgetc(void)
{
    return ros_syscall(SYS_cgetc, 0, 0, 0, 0, 0);
}

int sys_null(void)
{
    return ros_syscall(SYS_null, 0, 0, 0, 0, 0);
}

ssize_t sys_shared_page_alloc(void** addr, pid_t p2, 
                              int p1_flags, int p2_flags
                             ) 
{
	return ros_syscall(SYS_shared_page_alloc, addr, 
	               p2, p1_flags, p2_flags, 0);
}

ssize_t sys_shared_page_free(void* addr, pid_t p2) 
{
	return ros_syscall(SYS_shared_page_free, addr, p2, 0,0,0);
}

//Write a buffer over the serial port
ssize_t sys_serial_write(void* buf, size_t len) 
{
	return ros_syscall(SYS_serial_write, buf, len, 0, 0, 0);
}

//Read a buffer over the serial port
ssize_t sys_serial_read(void* buf, size_t len) 
{
	return ros_syscall(SYS_serial_read, buf, len, 0, 0, 0);
}

//Write a buffer over ethernet
ssize_t sys_eth_write(void* buf, size_t len) 
{
	if (len == 0)
		return 0;
	
	return ros_syscall(SYS_eth_write, buf, len, 0, 0, 0);
}

//Read a buffer via ethernet
ssize_t sys_eth_read(void* buf, size_t len) 
{
	if (len == 0)
		return 0;
		
	return ros_syscall(SYS_eth_read, buf, len, 0, 0, 0);
}

/* Request resources from the kernel.  Flags in ros/resource.h. */
ssize_t sys_resource_req(int type, size_t amt_max, size_t amt_min, uint32_t flags)
{
	return ros_syscall(SYS_resource_req, type, amt_max, amt_min, flags, 0);
}

void sys_reboot(void)
{
	ros_syscall(SYS_reboot,0,0,0,0,0);
}

void sys_yield(bool being_nice)
{
	ros_syscall(SYS_yield, being_nice, 0, 0, 0, 0);
}

int sys_proc_create(char* path)
{
	return ros_syscall(SYS_proc_create, (uintreg_t)path, 0, 0, 0, 0);
}

int sys_proc_run(int pid)
{
	return ros_syscall(SYS_proc_run, pid, 0, 0, 0, 0);
}

/* We need to do some hackery to pass 6 arguments.  Arg4 pts to the real arg4,
 * arg5, and arg6.  Keep this in sync with kern/src/syscall.c.
 * TODO: consider a syscall_multi that can take more args, and keep it in sync
 * with the kernel.  Maybe wait til we fix sysenter to have 5 or 6 args. */
void *CT(length) sys_mmap(void *SNT addr, size_t length, int prot, int flags,
                          int fd, size_t offset)
{
	struct args {
		int _flags;
		int _fd;
		size_t _offset;
	} extra_args;
	extra_args._flags = flags;
	extra_args._fd = fd;
	extra_args._offset = offset;
	// TODO: deputy bitches about this
	return (void*CT(length))TC(ros_syscall(SYS_mmap, addr, length,
	                                       prot, &extra_args, 0));
}

int sys_notify(int pid, unsigned int notif, struct notif_event *ne)
{
	return ros_syscall(SYS_notify, pid, notif, ne, 0, 0);
}

int sys_self_notify(uint32_t vcoreid, unsigned int notif,
                    struct notif_event *ne)
{
	return ros_syscall(SYS_self_notify, vcoreid, notif, ne, 0, 0);
}

int sys_halt_core(unsigned int usec)
{
	return ros_syscall(SYS_halt_core, usec, 0, 0, 0, 0);
}
