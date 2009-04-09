#ifndef ROS_INC_SMP_H
#define ROS_INC_SMP_H

/* SMP related functions */

#include <inc/types.h>

#include <kern/trap.h>

void smp_call_function_self(isr_t handler, uint8_t vector);
void smp_call_function_all(isr_t handler, uint8_t vector);
void smp_call_function_single(uint8_t dest, isr_t handler, uint8_t vector);

#endif /* !ROS_INC_SMP_H */
