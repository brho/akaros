#ifndef _VCORE_H
#define _VCORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/vcore.h>
#include <string.h>

#define LOG2_MAX_VCORES 6
#define MAX_VCORES (1 << LOG2_MAX_VCORES)

#define TRANSITION_STACK_PAGES 2
#define TRANSITION_STACK_SIZE (TRANSITION_STACK_PAGES*PGSIZE)

/* Defined by glibc; Must be implemented by a user level threading library */
extern void vcore_entry();

/* Vcore API functions */
int vcore_init(void);
int vcore_id(void);
int vcore_request(size_t k);
void vcore_yield(void);
size_t max_vcores(void);
size_t num_vcores(void);

#ifdef __cplusplus
}
#endif

#endif
