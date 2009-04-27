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
#define NUM_HANDLER_WRAPPERS		5

typedef struct HandlerWrapper {
	checklist_t* cpu_list;
	uint8_t vector;
} handler_wrapper_t;

/* SMP bootup functions */
void smp_boot(void);
void smp_idle(void);

/* SMP utility functions */
int smp_call_function_self(isr_t handler, void* data,
                           handler_wrapper_t** wait_wrapper);
int smp_call_function_all(isr_t handler, void* data,
                          handler_wrapper_t** wait_wrapper);
int smp_call_function_single(uint8_t dest, isr_t handler, void* data,
                             handler_wrapper_t** wait_wrapper);
int smp_call_wait(handler_wrapper_t* wrapper);

#endif /* !ROS_INC_SMP_H */
