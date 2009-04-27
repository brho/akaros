/* See COPYRIGHT for copyright information. */

#ifndef ROS_KERN_TRAP_H
#define ROS_KERN_TRAP_H
#ifndef ROS_KERNEL
# error "This is an ROS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

// func ptr for interrupt service routines
typedef void (*isr_t)(trapframe_t* tf, void* data);
typedef struct InterruptHandler {
	isr_t isr;
	void* data;
} handler_t;

/* The kernel's interrupt descriptor table */
extern gatedesc_t idt[];

void idt_init(void);
void register_interrupt_handler(handler_t (COUNT(256)table)[], uint8_t int_num,
                                isr_t handler, void* data);
void (IN_HANDLER print_regs)(push_regs_t *regs);
void (IN_HANDLER print_trapframe)(trapframe_t *tf);
void (IN_HANDLER page_fault_handler)(trapframe_t *tf);
void backtrace(trapframe_t *tf);

#endif /* ROS_KERN_TRAP_H */
