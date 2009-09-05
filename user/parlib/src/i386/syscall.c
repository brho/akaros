// System call stubs.

#include <ros/common.h>
#include <arch/arch.h>

// TODO: fix sysenter to take all 5 params
static inline intreg_t syscall_sysenter(uint16_t num, intreg_t a1,
                                 intreg_t a2, intreg_t a3,
                                 intreg_t a4, intreg_t a5)
{
	intreg_t ret;
	asm volatile ("  pushl %%ebp;        "
	              "  pushl %%esi;        "
	              "  movl %%esp, %%ebp;  "
	              "  leal 1f, %%esi;     "
	              "  sysenter;           "
	              "1:                    "
	              "  popl %%esi;         "
	              "  popl %%ebp;         "
	              : "=a" (ret)
	              : "a" (num),
	                "d" (a1),
	                "c" (a2),
	                "b" (a3),
	                "D" (a4)
	              : "cc", "memory");
	return ret;
}

static inline intreg_t syscall_trap(uint16_t num, intreg_t a1,
                             intreg_t a2, intreg_t a3,
                             intreg_t a4, intreg_t a5)
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

	asm volatile("int %1"
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

intreg_t syscall(uint16_t num, intreg_t a1,
                intreg_t a2, intreg_t a3,
                intreg_t a4, intreg_t a5)
{
	#ifndef SYSCALL_TRAP
		return syscall_sysenter(num, a1, a2, a3, a4, a5);
	#else
		return syscall_trap(num, a1, a2, a3, a4, a5);
	#endif
}
