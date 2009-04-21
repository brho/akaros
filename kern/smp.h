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

struct IPIWrapper;
typedef struct IPIWrapper ipi_wrapper_t;

LIST_HEAD(ipi_wrapper_list_t, ipi_wrapper_t);
typedef LIST_ENTRY(ipi_wrapper_t) ipi_wrapper_list_entry_t;

struct IPIWrapper {
	ipi_wrapper_list_entry_t ipi_link;	/* list link */

	checklist_t* front_cpu_list;
	checklist_t* back_cpu_list;
	uint8_t vector;
};

void smp_boot(void);
void smp_call_function_self(isr_t handler, bool wait);
void smp_call_function_all(isr_t handler, bool wait);
void smp_call_function_single(uint8_t dest, isr_t handler, bool wait);

#endif /* !ROS_INC_SMP_H */
