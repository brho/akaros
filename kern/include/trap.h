/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_TRAP_H
#define ROS_KERN_TRAP_H
#ifndef ROS_KERNEL
# error "This is an ROS kernel header; user programs should not #include it"
#endif

#include <arch/arch.h>
#include <arch/mmu.h>
#include <arch/trap.h>
#include <sys/queue.h>

// func ptr for interrupt service routines
typedef void ( *poly_isr_t)(trapframe_t* tf, TV(t) data);
typedef void (*isr_t)(trapframe_t* tf, void * data);
typedef struct InterruptHandler {
	poly_isr_t isr;
	TV(t) data;
} handler_t;

#ifdef __IVY__
#pragma cilnoremove("iht_lock")
extern spinlock_t iht_lock;
#endif
extern handler_t LCKD(&iht_lock) (CT(NUM_INTERRUPT_HANDLERS) RO interrupt_handlers)[];

void idt_init(void);
void
register_interrupt_handler(handler_t SSOMELOCK (CT(NUM_INTERRUPT_HANDLERS)table)[],
                           uint8_t int_num,
                           poly_isr_t handler, TV(t) data);
void print_trapframe(trapframe_t *tf);
void page_fault_handler(trapframe_t *tf);
/* Generic per-core timer interrupt handler.  set_percore_timer() will fire the
 * timer_interrupt(). */
void set_core_timer(uint32_t usec, bool periodic);
void timer_interrupt(struct trapframe *tf, void *data);

void sysenter_init(void);
extern void sysenter_handler();

void save_fp_state(struct ancillary_state *silly);
void restore_fp_state(struct ancillary_state *silly);
/* Set stacktop for the current core to be the stack the kernel will start on
 * when trapping/interrupting from userspace */
void set_stack_top(uintptr_t stacktop);
uintptr_t get_stack_top(void);

/* It's important that this is inline and that tf is not a stack variable */
static inline void save_kernel_tf(struct trapframe *tf)
                   __attribute__((always_inline));
void pop_kernel_tf(struct trapframe *tf) __attribute__((noreturn));

/* Kernel messages.  Each arch implements them in their own way.  Both should be
 * guaranteeing in-order delivery.  Kept here in trap.h, since sparc is using
 * trap.h for KMs.  Eventually, both arches will use the same implementation.
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
void kernel_msg_init(void);
typedef void (*amr_t)(trapframe_t* tf, uint32_t srcid,
                      TV(a0t) a0, TV(a1t) a1, TV(a2t) a2);

struct kernel_message
{
	STAILQ_ENTRY(kernel_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t))
		NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) link;
	uint32_t srcid;
	amr_t pc;
	TV(a0t) arg0;
	TV(a1t) arg1;
	TV(a2t) arg2;
};
STAILQ_HEAD(kernel_msg_list, kernel_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t));
typedef struct kernel_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) kernel_message_t;

uint32_t send_kernel_message(uint32_t dst, amr_t pc, TV(a0t) arg0, TV(a1t) arg1,
                             TV(a2t) arg2, int type);
void process_routine_kmsg(struct trapframe *tf);

#endif /* ROS_KERN_TRAP_H */
