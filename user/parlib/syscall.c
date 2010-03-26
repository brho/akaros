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

//Run a binary loaded at the specificed address with the specified arguments
ssize_t sys_run_binary(void* binary_buf, size_t len,
                       const procinfo_t* pi, size_t num_colors) 
{
	return ros_syscall(SYS_run_binary, binary_buf, len,
                                       pi,num_colors,0);
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
ssize_t sys_resource_req(int type, size_t amount, uint32_t flags)
{
	return ros_syscall(SYS_resource_req, type, amount, flags, 0, 0);
}

void sys_reboot()
{
	ros_syscall(SYS_reboot,0,0,0,0,0);
}

void sys_yield()
{
	ros_syscall(SYS_yield,0,0,0,0,0);
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

