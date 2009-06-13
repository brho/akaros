// System call stubs.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/x86.h>
#include <parlib.h>

// TODO: modify to take only four parameters
static intreg_t syscall_sysenter(uint16_t num, intreg_t a1,
                                 intreg_t a2, intreg_t a3,
                                 intreg_t a4, intreg_t a5)
{
	intreg_t ret;
	asm volatile(
	    "pushl %%ebp\n\t"
	    "pushl %%esi\n\t"
	    "movl %%esp, %%ebp\n\t"
	    "leal after_sysenter, %%esi\n\t"
	    "sysenter\n\t"
	    "after_sysenter:\n\t"
	    "\tpopl %%esi\n"
	    "\tpopl %%ebp\n"
	    :"=a" (ret)
	    : "a" (num),
	      "d" (a1),
	      "c" (a2),
	      "b" (a3),
	      "D" (a4)
	    : "cc", "memory", "%esp");
	return ret;
}

static intreg_t syscall_trap(uint16_t num, intreg_t a1,
                             intreg_t a2, intreg_t a3,
                             intreg_t a4, intreg_t a5)
{
	debug("duh!\n");
	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		: "=a" (ret)
		: "i" (T_SYSCALL),
		  "a" (num),
		  "d" (a1),
		  "c" (a2),
		  "b" (a3),
		  "D" (a4),
		  "S" (a5)
		: "cc", "memory");

	return ret;
}

static intreg_t syscall(uint16_t num, intreg_t a1,
                        intreg_t a2, intreg_t a3,
                        intreg_t a4, intreg_t a5)
{
	#ifndef SYSCALL_TRAP
		return syscall_sysenter(num, a1, a2, a3, a4, a5);
	#else
		return syscall_trap(num, a1, a2, a3, a4, a5);
	#endif
}

void sys_env_destroy(envid_t envid)
{
	syscall(SYS_env_destroy, envid, 0, 0, 0, 0);
	while(1); //Should never get here...
}

envid_t sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0);
}

envid_t sys_getcpuid(void)
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
