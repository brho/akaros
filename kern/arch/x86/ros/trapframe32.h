#ifndef ROS_INC_ARCH_TRAPFRAME32_H
#define ROS_INC_ARCH_TRAPFRAME32_H

#ifndef ROS_INC_ARCH_TRAPFRAME_H
#error "Do not include include ros/arch/trapframe32.h directly"
#endif

typedef struct pushregs {
	/* registers as pushed by pusha */
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp; uint32_t reg_oesp;		/* Useless */
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} push_regs_t;

struct hw_trapframe {
	push_regs_t tf_regs;
	uint16_t tf_gs;
	uint16_t tf_padding1;
	uint16_t tf_fs;
	uint16_t tf_padding2;
	uint16_t tf_es;
	uint16_t tf_padding3;
	uint16_t tf_ds;
	uint16_t tf_padding4;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding5;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding6;
};

struct sw_trapframe {
	uint32_t tf_ebp;
	uint32_t tf_ebx;
	uint32_t tf_esi;
	uint32_t tf_edi;
	uint32_t tf_esp;
	uint32_t tf_eip;
	uint32_t tf_mxcsr;
	uint16_t tf_fpucw;
	uint16_t tf_gs;		/* something to track TLS is callee-saved (sort of) */
};

#endif /* ROS_INC_ARCH_TRAPFRAME32_H */
