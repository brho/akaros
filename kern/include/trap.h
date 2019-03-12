/* See COPYRIGHT for copyright information. */

#pragma once

#define ROS_KERN_TRAP_H

#include <ros/trapframe.h>
#include <arch/arch.h>
#include <arch/mmu.h>
#include <sys/queue.h>
#include <arch/trap.h>

// func ptr for interrupt service routines
typedef void (*isr_t)(struct hw_trapframe *hw_tf, void *data);

void idt_init(void);
int register_irq(int irq, isr_t handler, void *irq_arg, uint32_t tbdf);
int route_irqs(int cpu_vec, int coreid);
void print_trapframe(struct hw_trapframe *hw_tf);
void print_swtrapframe(struct sw_trapframe *sw_tf);
void print_vmtrapframe(struct vm_trapframe *vm_tf);
void print_user_ctx(struct user_context *ctx);
/* Generic per-core timer interrupt handler.  set_percore_timer() will fire the
 * timer_interrupt(). */
void set_core_timer(uint32_t usec, bool periodic);
void timer_interrupt(struct hw_trapframe *hw_tf, void *data);

extern inline void save_fp_state(struct ancillary_state *silly);
extern inline void restore_fp_state(struct ancillary_state *silly);
extern inline void init_fp_state(void);
/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace */
void set_stack_top(uintptr_t stacktop);
uintptr_t get_stack_top(void);

void send_nmi(uint32_t os_coreid);
int reflect_current_context(void);
void reflect_unhandled_trap(unsigned int trap_nr, unsigned int err,
                            unsigned long aux);
void __arch_reflect_trap_hwtf(struct hw_trapframe *hw_tf, unsigned int trap_nr,
                              unsigned int err, unsigned long aux);

uintptr_t get_user_ctx_pc(struct user_context *ctx);
uintptr_t get_user_ctx_fp(struct user_context *ctx);
uintptr_t get_user_ctx_sp(struct user_context *ctx);

/* Partial contexts are those where the full context is split between the parts
 * in the struct and the parts still loaded in hardware.
 *
 * Finalizing a context ensures that the full context is saved in the struct and
 * nothing remains in hardware.  Finalize does two things: makes sure the
 * context can be run again on another core and makes sure the core can run
 * another context.
 *
 * arch_finalize_ctx() must be idempotent and have no effect on a full context.
 * It is up to the architecture to keep track of whether or not a context is
 * full or partial and handle finalize calls on a context that might not be
 * partial.  They can do so in the ctx itself, in their own arch-dependent
 * manner.
 *
 * The kernel's guarantee to the arches is that:
 * - finalize will be called after proc_pop_ctx (i.e. after it runs) at least
 * once, before that context is used again on another core or before another
 * context is used on this core.
 * - the arches can store the partial status and anything else it wants in the
 * *ctx without fear of it being tampered with.
 * - user-provided contexts will be passed to proc_secure_ctx, and those
 * contexts are full/finalized already.  Anything else is a user bug.  The
 * arches enforce this.
 * - an arch will never be asked to pop a partial context that was not already
 * loaded onto the current core.
 * - contexts will be finalized before handing them back to the user. */
extern inline void arch_finalize_ctx(struct user_context *ctx);
extern inline bool arch_ctx_is_partial(struct user_context *ctx);
void copy_current_ctx_to(struct user_context *to_ctx);

/* Kernel messages.  This is an in-order 'active message' style messaging
 * subsystem, where you can instruct other cores (including your own) to execute
 * a function (with arguments), either immediately or whenever the kernel is
 * able to abandon its context/stack (permanent swap).
 *
 * These are different (for now) than the smp_calls in smp.h, since
 * they will be executed immediately (for urgent messages), and in the order in
 * which they are sent.  smp_calls are currently not run in order, and they must
 * return (possibly passing the work to a workqueue, which is really just a
 * routine message, so they really need to just return).
 *
 * Eventually, smp_call will be replaced by these.
 *
 * Also, a big difference is that smp_calls can use the same message (registered
 * in the interrupt_handlers[] for x86) for every recipient, but the kernel
 * messages require a unique message.  Also for now, but it might be like that
 * for a while on x86 (til we have a broadcast). */

#define KMSG_IMMEDIATE 			1
#define KMSG_ROUTINE 			2

typedef void (*amr_t)(uint32_t srcid, long a0, long a1, long a2);

struct kernel_message
{
	STAILQ_ENTRY(kernel_message) link;
	uint32_t srcid;
	uint32_t dstid;
	amr_t pc;
	long arg0;
	long arg1;
	long arg2;
}__attribute__((aligned(8)));

STAILQ_HEAD(kernel_msg_list, kernel_message);
typedef struct kernel_message kernel_message_t;

void kernel_msg_init(void);
uint32_t send_kernel_message(uint32_t dst, amr_t pc, long arg0, long arg1,
                             long arg2, int type);
void handle_kmsg_ipi(struct hw_trapframe *hw_tf, void *data);
bool has_routine_kmsg(void);
void process_routine_kmsg(void);
void print_kmsgs(uint32_t coreid);

/* Runs a function with up to two arguments as a routine kernel message.  Kernel
 * messages can have three arguments, but the deferred function pointer counts
 * as one.  Note the arguments to the function will be treated as longs. */
#define run_as_rkm(f, ...) do {                                                \
	static_assert(MACRO_NR_ARGS(__VA_ARGS__) <= 2);                        \
	PASTE(__run_as_rkm_, MACRO_NR_ARGS(__VA_ARGS__))(f, ##__VA_ARGS__);    \
} while (0)

#define __run_as_rkm_0(f) \
	send_kernel_message(core_id(), __kmsg_trampoline, (long)f, 0xcafebabe, \
	                    0xcafebabe, KMSG_ROUTINE)
#define __run_as_rkm_1(f, a1) \
	send_kernel_message(core_id(), __kmsg_trampoline, (long)f, (long)a1, \
	                    0xcafebabe, KMSG_ROUTINE)
#define __run_as_rkm_2(f, a1, a2) \
	send_kernel_message(core_id(), __kmsg_trampoline, (long)f, (long)a1, \
	                    (long)a2, KMSG_ROUTINE)
void __kmsg_trampoline(uint32_t srcid, long a0, long a1, long a2);

/* Kernel context depths.  IRQ depth is how many nested IRQ stacks/contexts we
 * are working on.  Kernel trap depth is how many nested kernel traps (not
 * user-space traps) we have.
 *
 * Some examples:
 * 	(original context in parens, +(x, y) is the change to IRQ and ktrap
 * 	depth):
 * - syscall (user): +(0, 0)
 * - trap (user): +(0, 0)
 * - irq (user): +(1, 0)
 * - irq (kernel, handling syscall): +(1, 0)
 * - trap (kernel, regardless of context): +(0, 1)
 * - NMI (kernel): it's actually a kernel trap, even though it is
 *   sent by IPI.  +(0, 1)
 * - NMI (user): just a trap.  +(0, 0)
 *
 * So if the user traps in for a syscall (0, 0), then the kernel takes an IRQ
 * (1, 0), and then another IRQ (2, 0), and then the kernel page faults (a
 * trap), we're at (2, 1).
 *
 * Or if we're in userspace, then an IRQ arrives, we're in the kernel at (1, 0).
 * Note that regardless of whether or not we are in userspace or the kernel when
 * an irq arrives, we still are only at level 1 irq depth.  We don't care if we
 * have one or 0 kernel contexts under us.  (The reason for this is that I care
 * if it is *possible* for us to interrupt the kernel, not whether or not it
 * actually happened). */

/* uint32_t __ctx_depth is laid out like so:
 *
 * +------8------+------8------+------8------+------8------+
 * |    Flags    |    Unused   | Kernel Trap |  IRQ Depth  |
 * |             |             |    Depth    |             |
 * +-------------+-------------+-------------+-------------+
 *
 */
#define __CTX_IRQ_D_SHIFT		0
#define __CTX_KTRAP_D_SHIFT		8
#define __CTX_FLAG_SHIFT		24
#define __CTX_IRQ_D_MASK		((1 << 8) - 1)
#define __CTX_KTRAP_D_MASK		((1 << 8) - 1)
#define __CTX_NESTED_CTX_MASK		((1 << 16) - 1)
#define __CTX_CANNOT_BLOCK		(1 << (__CTX_FLAG_SHIFT + 0))

/* Basic functions to get or change depths */

#define irq_depth(pcpui)                                                       \
	(((pcpui)->__ctx_depth >> __CTX_IRQ_D_SHIFT) & __CTX_IRQ_D_MASK)

#define ktrap_depth(pcpui)                                                     \
	(((pcpui)->__ctx_depth >> __CTX_KTRAP_D_SHIFT) & __CTX_KTRAP_D_MASK)

#define inc_irq_depth(pcpui)                                                   \
	((pcpui)->__ctx_depth += 1 << __CTX_IRQ_D_SHIFT)

#define dec_irq_depth(pcpui)                                                   \
	((pcpui)->__ctx_depth -= 1 << __CTX_IRQ_D_SHIFT)

#define inc_ktrap_depth(pcpui)                                                 \
	((pcpui)->__ctx_depth += 1 << __CTX_KTRAP_D_SHIFT)

#define dec_ktrap_depth(pcpui)                                                 \
	((pcpui)->__ctx_depth -= 1 << __CTX_KTRAP_D_SHIFT)

#define set_cannot_block(pcpui)                                                \
	((pcpui)->__ctx_depth |= __CTX_CANNOT_BLOCK)

#define clear_cannot_block(pcpui)                                              \
	((pcpui)->__ctx_depth &= ~__CTX_CANNOT_BLOCK)

/* Functions to query the kernel context depth/state.  I haven't fully decided
 * on whether or not 'default' context includes RKMs or not.  Will depend on
 * how we use it.  Check the code below to see what the latest is. */

#define in_irq_ctx(pcpui)                                                      \
	(irq_depth(pcpui))

/* Right now, anything (KTRAP, IRQ, or RKM) makes us not 'default' */
#define in_default_ctx(pcpui)                                                  \
	(!(pcpui)->__ctx_depth)

/* Can block only if we have no nested contexts (ktraps or irqs, (which are
 * potentially nested contexts)) and not in an explicit CANNOT_BLOCK. */
#define can_block(pcpui)                                                       \
	(!((pcpui)->__ctx_depth & (__CTX_NESTED_CTX_MASK | __CTX_CANNOT_BLOCK)))

/* TRUE if we are allowed to spin, given that the 'lock' was declared as not
 * grabbable from IRQ context.  Meaning, we can't grab the lock from any nested
 * context.  (And for most locks, we can never grab them from a kernel trap
 * handler).
 *
 * Example is a lock that is not declared as irqsave, but we later grab it from
 * irq context.  This could deadlock the system, even if it doesn't do it this
 * time.  This function will catch that. */
#define can_spinwait_noirq(pcpui)                                              \
	(!((pcpui)->__ctx_depth & __CTX_NESTED_CTX_MASK))

/* TRUE if we are allowed to spin, given that the 'lock' was declared as
 * potentially grabbable by IRQ context (such as with an irqsave lock).  We can
 * never grab from a ktrap, since there is no way to prevent that.  And we must
 * have IRQs disabled, since an IRQ handler could attempt to grab the lock. */
#define can_spinwait_irq(pcpui)                                                \
	((!ktrap_depth(pcpui) && !irq_is_enabled()))

/* Debugging */
void print_kctx_depths(const char *str);
