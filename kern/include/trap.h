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
void ( print_trapframe)(trapframe_t *tf);
void ( page_fault_handler)(trapframe_t *tf);

void sysenter_init(void);
extern void sysenter_handler();

/* Active messages.  Each arch implements them in their own way.  Both should be
 * guaranteeing in-order delivery.  Kept here in trap.h, since sparc is using
 * trap.h for AMs
 *
 * These are different (for now) than the smp_calls in smp.h, since
 * they will be executed immediately, and in the order in which they are sent.
 * smp_calls are currently not run in order, and if they put things on the
 * workqueue, they don't get run until smp_idle (for now).
 *
 * Also, a big difference is that smp_calls can use the same message (registered
 * in the interrupt_handlers[] for x86) for every recipient, but the active
 * messages require a unique message.  Also for now, but it might be like that
 * for a while on x86. */

void active_msg_init(void);
typedef void (*amr_t)(trapframe_t* tf, uint32_t srcid,
                      TV(a0t) a0, TV(a1t) a1, TV(a2t) a2);

struct active_message
{
	STAILQ_ENTRY(active_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t))
		NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) link;
	uint32_t srcid;
	amr_t pc;
	TV(a0t) arg0;
	TV(a1t) arg1;
	TV(a2t) arg2;
};
STAILQ_HEAD(active_msg_list, active_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t));
typedef struct active_message NTPTV(a0t) NTPTV(a1t) NTPTV(a2t) active_message_t;

uint32_t send_active_message(uint32_t dst, amr_t pc,
                             TV(a0t) arg0, TV(a1t) arg1, TV(a2t) arg2);

#endif /* ROS_KERN_TRAP_H */
