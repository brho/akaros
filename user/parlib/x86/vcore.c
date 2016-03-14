#include <ros/syscall.h>
#include <parlib/arch/vcore.h>
#include <parlib/stdio.h>

struct syscall vc_entry = {
	.num = SYS_vc_entry,
	.err = 0,
	.retval = 0,
	.flags = 0,
	.ev_q = 0,
	.u_data = 0,
	.arg0 = 0,
	.arg1 = 0,
	.arg2 = 0,
	.arg3 = 0,
	.arg4 = 0,
	.arg5 = 0,
};

void print_hw_tf(struct hw_trapframe *hw_tf)
{
	printf("[user] HW TRAP frame 0x%016x\n", hw_tf);
	printf("  rax  0x%016lx\n",           hw_tf->tf_rax);
	printf("  rbx  0x%016lx\n",           hw_tf->tf_rbx);
	printf("  rcx  0x%016lx\n",           hw_tf->tf_rcx);
	printf("  rdx  0x%016lx\n",           hw_tf->tf_rdx);
	printf("  rbp  0x%016lx\n",           hw_tf->tf_rbp);
	printf("  rsi  0x%016lx\n",           hw_tf->tf_rsi);
	printf("  rdi  0x%016lx\n",           hw_tf->tf_rdi);
	printf("  r8   0x%016lx\n",           hw_tf->tf_r8);
	printf("  r9   0x%016lx\n",           hw_tf->tf_r9);
	printf("  r10  0x%016lx\n",           hw_tf->tf_r10);
	printf("  r11  0x%016lx\n",           hw_tf->tf_r11);
	printf("  r12  0x%016lx\n",           hw_tf->tf_r12);
	printf("  r13  0x%016lx\n",           hw_tf->tf_r13);
	printf("  r14  0x%016lx\n",           hw_tf->tf_r14);
	printf("  r15  0x%016lx\n",           hw_tf->tf_r15);
	printf("  trap 0x%08x\n",             hw_tf->tf_trapno);
	printf("  gsbs 0x%016lx\n",           hw_tf->tf_gsbase);
	printf("  fsbs 0x%016lx\n",           hw_tf->tf_fsbase);
	printf("  err  0x--------%08x\n",     hw_tf->tf_err);
	printf("  rip  0x%016lx\n",           hw_tf->tf_rip);
	printf("  cs   0x------------%04x\n", hw_tf->tf_cs);
	printf("  flag 0x%016lx\n",           hw_tf->tf_rflags);
	printf("  rsp  0x%016lx\n",           hw_tf->tf_rsp);
	printf("  ss   0x------------%04x\n", hw_tf->tf_ss);
}

void print_sw_tf(struct sw_trapframe *sw_tf)
{
	printf("[user] SW TRAP frame 0x%016p\n", sw_tf);
	printf("  rbx  0x%016lx\n",           sw_tf->tf_rbx);
	printf("  rbp  0x%016lx\n",           sw_tf->tf_rbp);
	printf("  r12  0x%016lx\n",           sw_tf->tf_r12);
	printf("  r13  0x%016lx\n",           sw_tf->tf_r13);
	printf("  r14  0x%016lx\n",           sw_tf->tf_r14);
	printf("  r15  0x%016lx\n",           sw_tf->tf_r15);
	printf("  gsbs 0x%016lx\n",           sw_tf->tf_gsbase);
	printf("  fsbs 0x%016lx\n",           sw_tf->tf_fsbase);
	printf("  rip  0x%016lx\n",           sw_tf->tf_rip);
	printf("  rsp  0x%016lx\n",           sw_tf->tf_rsp);
	printf(" mxcsr 0x%08x\n",             sw_tf->tf_mxcsr);
	printf(" fpucw 0x%04x\n",             sw_tf->tf_fpucw);
}

void print_vm_tf(struct vm_trapframe *vm_tf)
{
	printf("[user] VM Trapframe 0x%016x\n", vm_tf);
	printf("  rax  0x%016lx\n",           vm_tf->tf_rax);
	printf("  rbx  0x%016lx\n",           vm_tf->tf_rbx);
	printf("  rcx  0x%016lx\n",           vm_tf->tf_rcx);
	printf("  rdx  0x%016lx\n",           vm_tf->tf_rdx);
	printf("  rbp  0x%016lx\n",           vm_tf->tf_rbp);
	printf("  rsi  0x%016lx\n",           vm_tf->tf_rsi);
	printf("  rdi  0x%016lx\n",           vm_tf->tf_rdi);
	printf("  r8   0x%016lx\n",           vm_tf->tf_r8);
	printf("  r9   0x%016lx\n",           vm_tf->tf_r9);
	printf("  r10  0x%016lx\n",           vm_tf->tf_r10);
	printf("  r11  0x%016lx\n",           vm_tf->tf_r11);
	printf("  r12  0x%016lx\n",           vm_tf->tf_r12);
	printf("  r13  0x%016lx\n",           vm_tf->tf_r13);
	printf("  r14  0x%016lx\n",           vm_tf->tf_r14);
	printf("  r15  0x%016lx\n",           vm_tf->tf_r15);
	printf("  rip  0x%016lx\n",           vm_tf->tf_rip);
	printf("  rflg 0x%016lx\n",           vm_tf->tf_rflags);
	printf("  rsp  0x%016lx\n",           vm_tf->tf_rsp);
	printf("  cr2  0x%016lx\n",           vm_tf->tf_cr2);
	printf("  cr3  0x%016lx\n",           vm_tf->tf_cr3);
	printf("Gpcore 0x%08x\n",             vm_tf->tf_guest_pcoreid);
	printf("Flags  0x%08x\n",             vm_tf->tf_flags);
	printf("Inject 0x%08x\n",             vm_tf->tf_trap_inject);
	printf("ExitRs 0x%08x\n",             vm_tf->tf_exit_reason);
	printf("ExitQl 0x%08x\n",             vm_tf->tf_exit_qual);
	printf("Intr1  0x%016lx\n",           vm_tf->tf_intrinfo1);
	printf("Intr2  0x%016lx\n",           vm_tf->tf_intrinfo2);
	printf("GVA    0x%016lx\n",           vm_tf->tf_guest_va);
	printf("GPA    0x%016lx\n",           vm_tf->tf_guest_pa);
}

void print_user_context(struct user_context *ctx)
{
	switch (ctx->type) {
	case ROS_HW_CTX:
		print_hw_tf(&ctx->tf.hw_tf);
		break;
	case ROS_SW_CTX:
		print_sw_tf(&ctx->tf.sw_tf);
		break;
	case ROS_VM_CTX:
		print_vm_tf(&ctx->tf.vm_tf);
		break;
	default:
		printf("Unknown context type %d\n", ctx->type);
	}
}
