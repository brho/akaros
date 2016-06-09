#include <parlib/arch/debug.h>

/* TODO(chrisko): add a way to signal that a register isn't supplied for sw
 * contexts; because gdbserver has a notion of not knowing a register's value.
 */

int d9_fetch_registers(struct uthread *t, struct d9_regs *regs)
{
#define reg_from_hwtf(fld) \
	regs->reg_##fld = t->u_ctx.tf.hw_tf.tf_##fld

#define reg_from_swtf(fld) \
	regs->reg_##fld = t->u_ctx.tf.sw_tf.tf_##fld

	switch (t->u_ctx.type) {
	case ROS_HW_CTX:
		reg_from_hwtf(rax);
		reg_from_hwtf(rbx);
		reg_from_hwtf(rcx);
		reg_from_hwtf(rdx);
		reg_from_hwtf(rsi);
		reg_from_hwtf(rdi);
		reg_from_hwtf(rbp);
		reg_from_hwtf(rsp);
		reg_from_hwtf(r8);
		reg_from_hwtf(r9);
		reg_from_hwtf(r10);
		reg_from_hwtf(r11);
		reg_from_hwtf(r12);
		reg_from_hwtf(r13);
		reg_from_hwtf(r14);
		reg_from_hwtf(r15);
		reg_from_hwtf(rip);
		reg_from_hwtf(cs);
		reg_from_hwtf(ss);
		regs->reg_eflags = (uint32_t) t->u_ctx.tf.hw_tf.tf_rflags;
		break;
	case ROS_SW_CTX:
		reg_from_hwtf(rbx);
		reg_from_hwtf(rbp);
		reg_from_hwtf(rsp);
		reg_from_hwtf(rip);
		reg_from_swtf(r12);
		reg_from_swtf(r13);
		reg_from_swtf(r14);
		reg_from_swtf(r15);
		break;
	case ROS_VM_CTX:
		panic("2LS debug: VM context unsupported\n");
	}

	return 0;
}

int d9_store_registers(struct uthread *t, struct d9_regs *regs)
{
#define reg_to_hwtf(fld) \
	t->u_ctx.tf.hw_tf.tf_##fld = regs->reg_##fld

#define reg_to_swtf(fld) \
	t->u_ctx.tf.sw_tf.tf_##fld = regs->reg_##fld

	switch (t->u_ctx.type) {
	case ROS_HW_CTX:
		reg_to_hwtf(rax);
		reg_to_hwtf(rbx);
		reg_to_hwtf(rcx);
		reg_to_hwtf(rdx);
		reg_to_hwtf(rsi);
		reg_to_hwtf(rdi);
		reg_to_hwtf(rbp);
		reg_to_hwtf(rsp);
		reg_to_hwtf(r8);
		reg_to_hwtf(r9);
		reg_to_hwtf(r10);
		reg_to_hwtf(r11);
		reg_to_hwtf(r12);
		reg_to_hwtf(r13);
		reg_to_hwtf(r14);
		reg_to_hwtf(r15);
		reg_to_hwtf(rip);
		reg_to_hwtf(cs);
		reg_to_hwtf(ss);
		t->u_ctx.tf.hw_tf.tf_rflags = regs->reg_eflags;
		break;
	case ROS_SW_CTX:
		reg_to_hwtf(rbx);
		reg_to_hwtf(rip);
		reg_to_hwtf(rsp);
		reg_to_hwtf(rbp);
		reg_to_hwtf(r12);
		reg_to_hwtf(r13);
		reg_to_hwtf(r14);
		reg_to_hwtf(r15);
		break;
	case ROS_VM_CTX:
		panic("2LS debug: VM context unsupported\n");
	}

	return 0;
}
