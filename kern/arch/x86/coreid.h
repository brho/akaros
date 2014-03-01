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
extern int hw_coreid_lookup[MAX_NUM_CPUS];
extern int os_coreid_lookup[MAX_NUM_CPUS];

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

#ifdef CONFIG_FAST_COREID
static inline int core_id(void)
{
	int ret;
	asm volatile ("rdtscp" : "=c"(ret) : : "eax", "edx");
	return ret;
}
#else
/* core_id() returns the OS core number, not to be confused with the
 * hardware-specific core identifier (such as the lapic id) returned by
 * hw_core_id() */
static inline int core_id(void)
{
	return get_os_coreid(hw_core_id());
}
#endif /* CONFIG_FAST_COREID */

static inline int core_id_early(void)
{
	if (!core_id_ready)
		return 0;
	return core_id();
}

#endif /* ROS_ARCH_COREID_H */
