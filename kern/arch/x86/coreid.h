#ifndef ROS_ARCH_COREID_H
#define ROS_ARCH_COREID_H

#include <ros/common.h>
#include <arch/apic.h>

static inline int get_hw_coreid(uint32_t coreid) __attribute__((always_inline));
static inline int hw_core_id(void) __attribute__((always_inline));
static inline int get_os_coreid(int hw_coreid) __attribute__((always_inline));
static inline int core_id(void) __attribute__((always_inline));
static inline int node_id(void) __attribute__((always_inline));
static inline int core_id_early(void) __attribute__((always_inline));

/* declared in smp.c */
extern int hw_coreid_lookup[MAX_NUM_CORES];
extern int os_coreid_lookup[MAX_NUM_CORES];

/* os_coreid -> hw_coreid */
static inline int get_hw_coreid(uint32_t coreid)
{
	return hw_coreid_lookup[coreid];
}

static inline int hw_core_id(void)
{
	return lapic_get_id();
}

/* hw_coreid -> os_coreid */
static inline int get_os_coreid(int hw_coreid)
{
	return os_coreid_lookup[hw_coreid];
}

static inline int node_id(void)
{
	return 0;
}

static inline int core_id(void)
{
	int coreid;
	/* assuming we're right after stacktop.  gs base is the pcpui struct, but we
	 * don't have access to the pcpui struct or to the extern per_cpu_info here,
	 * due to include loops. */
	asm volatile ("movl %%gs:8, %0" : "=r"(coreid));
	return coreid;
}

/* Tracks whether it is safe to execute core_id() or not. */
extern bool core_id_ready;
static inline int core_id_early(void)
{
	if (!core_id_ready)
		return 0;
	return core_id();
}

#endif /* ROS_ARCH_COREID_H */
