#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <arch/types.h>
#include <arch/atomic.h>
#include <ros/queue.h>
#include <trap.h>
#include <workqueue.h>

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

typedef struct per_cpu_info {
	// Once we have a real kmalloc, we can make this dynamic.  Want a queue.
	work_t delayed_work;
	// will want it padded out to an even cacheline
} per_cpu_info_t;
extern per_cpu_info_t per_cpu_info[MAX_NUM_CPUS];

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
