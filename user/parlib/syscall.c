// System call stubs.

#include <parlib.h>

int sys_proc_destroy(int pid, int exitcode)
{
	return ros_syscall(SYS_proc_destroy, pid, exitcode, 0, 0, 0, 0);
}

int sys_getpid(void)
{
	 return ros_syscall(SYS_getpid, 0, 0, 0, 0, 0, 0);
}

size_t sys_getcpuid(void)
{
	 return ros_syscall(SYS_getcpuid, 0, 0, 0, 0, 0, 0);
}

ssize_t sys_cputs(const uint8_t *s, size_t len)
{
    return ros_syscall(SYS_cputs, s,  len, 0, 0, 0, 0);
}

uint16_t sys_cgetc(void)
{
    return ros_syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
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

//Write a buffer over the serial port
ssize_t sys_serial_write(void* buf, size_t len) 
{
	return ros_syscall(SYS_serial_write, buf, len, 0, 0, 0, 0);
}

//Read a buffer over the serial port
ssize_t sys_serial_read(void* buf, size_t len) 
{
	return ros_syscall(SYS_serial_read, buf, len, 0, 0, 0, 0);
}

//Write a buffer over ethernet
ssize_t sys_eth_write(void* buf, size_t len) 
{
	if (len == 0)
		return 0;
	
	return ros_syscall(SYS_eth_write, buf, len, 0, 0, 0, 0);
}

//Read a buffer via ethernet
ssize_t sys_eth_read(void* buf, size_t len) 
{
	if (len == 0)
		return 0;
		
	return ros_syscall(SYS_eth_read, buf, len, 0, 0, 0, 0);
}

/* Request resources from the kernel.  Flags in ros/resource.h. */
ssize_t sys_resource_req(int type, size_t amt_max, size_t amt_min, uint32_t flags)
{
	return ros_syscall(SYS_resource_req, type, amt_max, amt_min, flags, 0, 0);
}

void sys_reboot(void)
{
	ros_syscall(SYS_reboot, 0, 0, 0, 0, 0, 0);
}

void sys_yield(bool being_nice)
{
	ros_syscall(SYS_yield, being_nice, 0, 0, 0, 0, 0);
}

int sys_proc_create(char *path, size_t path_l, char *argv[], char *envp[])
{
	struct procinfo pi;
	if (procinfo_pack_args(&pi, argv, envp)) {
		errno = ENOMEM;
		return -1;
	}
	return ros_syscall(SYS_proc_create, path, path_l, &pi, 0, 0, 0);
}

int sys_proc_run(int pid)
{
	return ros_syscall(SYS_proc_run, pid, 0, 0, 0, 0, 0);
}

void *CT(length) sys_mmap(void *SNT addr, size_t length, int prot, int flags,
                          int fd, size_t offset)
{
	return (void*)ros_syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}

int sys_notify(int pid, unsigned int ev_type, struct event_msg *u_msg)
{
	return ros_syscall(SYS_notify, pid, ev_type, u_msg, 0, 0, 0);
}

int sys_self_notify(uint32_t vcoreid, unsigned int ev_type,
                    struct event_msg *u_msg)
{
	return ros_syscall(SYS_self_notify, vcoreid, ev_type, u_msg, 0, 0, 0);
}

int sys_halt_core(unsigned int usec)
{
	return ros_syscall(SYS_halt_core, usec, 0, 0, 0, 0, 0);
}

void* sys_init_arsc()
{
	return (void*)ros_syscall(SYS_init_arsc, 0, 0, 0, 0, 0, 0);
}

int sys_block(unsigned int usec)
{
	return ros_syscall(SYS_block, usec, 0, 0, 0, 0, 0);
}
