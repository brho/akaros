#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <inc/types.h>
#include <inc/queue.h>

#include <kern/trap.h>
#include <kern/atomic.h>

#ifdef __BOCHS__
#define SMP_CALL_FUNCTION_TIMEOUT    0x00ffffff
#define SMP_BOOT_TIMEOUT             0x0000ffff
#else
#define SMP_CALL_FUNCTION_TIMEOUT    0x7ffffff0
#define SMP_BOOT_TIMEOUT             0x002fffff
#endif

// be careful changing this, esp if you go over 16
#define NUM_HANDLER_WRAPPERS  5

typedef struct HandlerWrapper {
	checklist_t* front_cpu_list;
	checklist_t* back_cpu_list;
	uint8_t vector;
} handler_wrapper_t;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void);

/* SMP utility functions */
void smp_call_function_self(isr_t handler, bool wait);
void smp_call_function_all(isr_t handler, bool wait);
void smp_call_function_single(uint8_t dest, isr_t handler, bool wait);

#endif /* !ROS_INC_SMP_H */
