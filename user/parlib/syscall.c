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

size_t sys_getpcoreid(void)
{
	 return ros_syscall(SYS_getpcoreid, 0, 0, 0, 0, 0, 0);
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
                    struct event_msg *u_msg, bool priv)
{
	return ros_syscall(SYS_self_notify, vcoreid, ev_type, u_msg, priv, 0, 0);
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

/* enable_my_notif tells the kernel whether or not it is okay to turn on notifs
 * when our calling vcore 'yields'.  This controls whether or not the vcore will
 * get started from vcore_entry() or not, and whether or not remote cores need
 * to sys_change_vcore to preempt-recover the calling vcore.  Only set this to
 * FALSE if you are unable to handle starting fresh at vcore_entry().  One
 * example of this is in mcs_pdr_locks */
void sys_change_vcore(uint32_t vcoreid, bool enable_my_notif)
{
	ros_syscall(SYS_change_vcore, vcoreid, enable_my_notif, 0, 0, 0, 0);
}

int sys_change_to_m(void)
{
	return ros_syscall(SYS_change_to_m, 0, 0, 0, 0, 0, 0);
}

int sys_poke_ksched(int res_type)
{
	return ros_syscall(SYS_poke_ksched, res_type, 0, 0, 0, 0, 0);
}
