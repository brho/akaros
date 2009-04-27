#ifndef ROS_INC_TESTING_H
#define ROS_INC_TESTING_H

/* This is just a dumping ground for old code used for testing.
 * Someone should go through old commits and bring back other relevant tests.
 * Someone else should actually make these useful on their own
 */

#include <inc/types.h>

#include <kern/trap.h>

void test_ipi_sending(void);
void test_pic_reception(void);
void test_print_info(void);
void test_barrier(void);
void test_interrupts_irqsave(void);
void test_bitmasks(void);
void test_checklists(void);
void test_pit(void);
void test_smp_call_functions(void);

void test_hello_world_handler(trapframe_t *tf, void* data);
void test_print_info_handler(trapframe_t *tf, void* data);
void test_barrier_handler(trapframe_t *tf, void* data);

#endif /* !ROS_INC_TESTING_H */
