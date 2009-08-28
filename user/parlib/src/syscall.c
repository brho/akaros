// System call stubs.

#include <parlib.h>

error_t sys_proc_destroy(int pid)
{
	return syscall(SYS_proc_destroy, pid, 0, 0, 0, 0);
}

int sys_getpid(void)
{
	 return syscall(SYS_getpid, 0, 0, 0, 0, 0);
}

size_t sys_getcpuid(void)
{
	 return syscall(SYS_getcpuid, 0, 0, 0, 0, 0);
}

ssize_t sys_cputs(const uint8_t *s, size_t len)
{
    return syscall(SYS_cputs, (intreg_t) s,  len, 0, 0, 0);
}

uint16_t sys_cgetc(void)
{
    return syscall(SYS_cgetc, 0, 0, 0, 0, 0);
}

ssize_t sys_shared_page_alloc(void** addr, pid_t p2, 
                              int p1_flags, int p2_flags
                             ) 
{
	return syscall(SYS_shared_page_alloc, (intreg_t)addr, 
	               p2, p1_flags, p2_flags, 0);
}

ssize_t sys_shared_page_free(void* addr, pid_t p2) 
{
	return syscall(SYS_shared_page_free, (intreg_t)addr, p2, 0,0,0);
}

//Write a buffer over the serial port
ssize_t sys_serial_write(void* buf, size_t len) 
{
	return syscall(SYS_serial_write, (intreg_t)buf, len, 0, 0, 0);
}

//Read a buffer over the serial port
ssize_t sys_serial_read(void* buf, size_t len) 
{
	return syscall(SYS_serial_read, (intreg_t)buf, len, 0, 0, 0);
}

//Run a binary loaded at the specificed address with the specified arguments
ssize_t sys_run_binary(void* binary_buf, void* arg, size_t len) 
{
	return syscall(SYS_run_binary, (intreg_t)binary_buf, (intreg_t)arg, len, 0, 0);
}

//Write a buffer over ethernet
ssize_t sys_eth_write(void* buf, size_t len) 
{
	return syscall(SYS_eth_write, (intreg_t)buf, len, 0, 0, 0);
}

//Read a buffer via ethernet
ssize_t sys_eth_read(void* buf, size_t len) 
{
	return syscall(SYS_eth_read, (intreg_t)buf, len, 0, 0, 0);
}
