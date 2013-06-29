#ifndef ROS_INC_ARCH_TRAPFRAME64_H
#define ROS_INC_ARCH_TRAPFRAME64_H

#ifndef ROS_INC_ARCH_TRAPFRAME_H
#error "Do not include include ros/arch/trapframe64.h directly"
#endif

struct hw_trapframe {
	uint64_t tf_gsbase;
	uint64_t tf_fsbase;
	uint64_t tf_rax;
	uint64_t tf_rbx;
	uint64_t tf_rcx;
	uint64_t tf_rdx;
	uint64_t tf_rbp;
	uint64_t tf_rsi;
	uint64_t tf_rdi;
	uint64_t tf_r8;
	uint64_t tf_r9;
	uint64_t tf_r10;
	uint64_t tf_r11;
	uint64_t tf_r12;
	uint64_t tf_r13;
	uint64_t tf_r14;
	uint64_t tf_r15;
	uint32_t tf_trapno;
	uint32_t tf_padding5;
	/* below here defined by x86 hardware (error code optional) */
	uint32_t tf_err;
	uint32_t tf_padding4;
	uint64_t tf_rip;
	uint16_t tf_cs;
	uint16_t tf_padding3;
	uint32_t tf_padding2;
	uint64_t tf_rflags;
	/* unlike 32 bit, SS:RSP is always pushed, even when not changing rings */
	uint64_t tf_rsp;
	uint16_t tf_ss;
	uint16_t tf_padding1;
	uint32_t tf_padding0;
};

struct sw_trapframe {
	uint64_t tf_gsbase;
	uint64_t tf_fsbase;
	uint64_t tf_rbx;
	uint64_t tf_rbp;
	uint64_t tf_r12;
	uint64_t tf_r13;
	uint64_t tf_r14;
	uint64_t tf_r15;
	uint64_t tf_rip;
	uint64_t tf_rsp;
	uint32_t tf_mxcsr;
	uint16_t tf_fpucw;
	uint16_t tf_padding0;
};

#endif /* ROS_INC_ARCH_TRAPFRAME64_H */
