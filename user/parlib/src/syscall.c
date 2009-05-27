// System call stubs.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <arch/x86.h>
#include <parlib.h>

// TODO: modify to take only four parameters
static uint32_t
syscall_sysenter(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	uint32_t ret;
    asm volatile(
            //"pushl %%ecx\n\t"
            //"pushl %%edx\n\t"
            "pushl %%ebp\n\t"
			"pushl %%esi\n\t"
            "movl %%esp, %%ebp\n\t"
            "leal after_sysenter, %%esi\n\t"
            "sysenter\n\t"
            "after_sysenter:\n\t"
			"popl %%esi\n\t"
            "popl %%ebp\n\t"
            //"popl %%edx\n\t"
            //"popl %%ecx"
            :"=a" (ret)
            : "a" (num),
                "d" (a1),
                "c" (a2),
                "b" (a3),
                "D" (a4)
        : "cc", "memory", "%esp");
	return ret;
}

static inline uint32_t syscall_trap(int num, uint32_t a1, uint32_t a2, 
                                    uint32_t a3, uint32_t a4, uint32_t a5)
{
	uint32_t ret;

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

static inline uint32_t syscall(int num, uint32_t a1, uint32_t a2, uint32_t a3,
                               uint32_t a4, uint32_t a5)
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

uint32_t sys_getcpuid(void)
{
	 return syscall(SYS_getcpuid, 0, 0, 0, 0, 0);
}

error_t sys_cputs(const char *s, size_t len)
{
    return syscall(SYS_cputs, (uint32_t) s,  len, 0, 0, 0);
}

//Write a buffer over the serial port
error_t sys_serial_write(void* buf, uint16_t len) 
{
	return -1;
}

//Read a buffer over the serial port
uint16_t sys_serial_read(void* buf, uint16_t len) 
{
	return 0;
}
