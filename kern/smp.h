#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <inc/types.h>

#include <kern/trap.h>
#include <kern/atomic.h>

#ifdef __BOCHS__
#define SMP_CALL_FUNCTION_TIMEOUT    0x00ffffff
#define SMP_BOOT_TIMEOUT             0x0000ffff
#else
#define SMP_CALL_FUNCTION_TIMEOUT    0x7ffffff0
#define SMP_BOOT_TIMEOUT             0x002fffff
#endif

typedef struct handler_wrapper {
	checklist_t* frontend;
	checklist_t* backend;
	uint8_t vector;
} handler_wrapper_t;

void smp_boot(void);

void smp_call_function_self(isr_t handler, uint8_t vector);
void smp_call_function_all(isr_t handler, uint8_t vector);
void smp_call_function_single(uint8_t dest, isr_t handler, uint8_t vector);

#endif /* !ROS_INC_SMP_H */
