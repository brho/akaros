#ifndef ROS_INC_TESTING_H
#define ROS_INC_TESTING_H

/* This is just a dumping ground for old code used for testing.
 * Someone should go through old commits and bring back other relevant tests.
 * Someone else should actually make these useful on their own
 */

#include <ros/common.h>

void test_ipi_sending(void);
void test_pic_reception(void);
void test_ioapic_pit_reroute(void);
void test_print_info(void);
void test_page_coloring(void);
void test_color_alloc(void);
void test_barrier(void);
void test_interrupts_irqsave(void);
void test_bitmasks(void);
void test_checklists(void);
void test_pit(void);
void test_smp_call_functions(void);
void test_lapic_status_bit(void);
void test_run_measurements(uint32_t job_num);
void test_circ_buffer(void);
void test_active_messages(void);
void test_slab(void);
void test_kmalloc(void);
void test_hashtable(void);

struct trapframe_t;

void test_hello_world_handler(trapframe_t *tf, void* data);
void test_print_info_handler(trapframe_t *tf, void* data);
void test_barrier_handler(trapframe_t *tf, void* data);

#endif /* !ROS_INC_TESTING_H */
