/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_TRAP_H
#define ROS_KERN_TRAP_H
#ifndef ROS_KERNEL
# error "This is an ROS kernel header; user programs should not #include it"
#endif

#include <arch/mmu.h>
#include <arch/trap.h>

// func ptr for interrupt service routines
typedef void (*poly_isr_t)(trapframe_t* tf, TV(t) data);
typedef void (*isr_t)(trapframe_t* tf, void * data);
typedef struct InterruptHandler {
	isr_t isr;
	void* data;
} handler_t;

void idt_init(void);
void register_interrupt_handler(handler_t (COUNT(256)table)[], uint8_t int_num,
                                poly_isr_t handler, TV(t) data);
void (IN_HANDLER print_trapframe)(trapframe_t *tf);
void (IN_HANDLER page_fault_handler)(trapframe_t *tf);

void sysenter_init(void);
extern void sysenter_handler();
#endif /* ROS_KERN_TRAP_H */
