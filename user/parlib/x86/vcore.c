#include <ros/syscall.h>
#include <parlib/vcore.h>
#include <parlib/stdio.h>
#include <stdlib.h>

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

/* The second-lowest level function jumped to by the kernel on every vcore
 * entry.  We get called from __kernel_vcore_entry.
 *
 * We should consider making it mandatory to set the tls_desc in the kernel. We
 * wouldn't even need to pass the vcore id to user space at all if we did this.
 * It would already be set in the preinstalled TLS as __vcore_id. */
void __attribute__((noreturn)) __kvc_entry_c(void)
{
	/* The kernel sets the TLS desc for us, based on whatever is in VCPD.
	 *
	 * x86 32-bit TLS is pretty jacked up, so the kernel doesn't set the TLS
	 * desc for us.  it's a little more expensive to do it here, esp for
	 * amd64.  Can remove this when/if we overhaul 32 bit TLS. */
	int id = __vcore_id_on_entry;

	#ifndef __x86_64__
	set_tls_desc(vcpd_of(id)->vcore_tls_desc);
	#endif
	/* Every time the vcore comes up, it must set that it is in vcore context.
	 * uthreads may share the same TLS as their vcore (when uthreads do not have
	 * their own TLS), and if a uthread was preempted, __vcore_context == FALSE,
	 * and that will continue to be true the next time the vcore pops up. */
	__vcore_context = TRUE;
	vcore_entry();
	fprintf(stderr, "vcore_entry() should never return!\n");
	abort();
	__builtin_unreachable();
}
