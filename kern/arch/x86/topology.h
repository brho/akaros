/* Copyright (c) 2015 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#pragma once

#include <ros/common.h>
#include <arch/apic.h>
#include <arch/x86.h>

struct core_info {
	int numa_id;
	int socket_id;
	int cpu_id;
	int core_id;
	int raw_socket_id;
	int apic_id;
};

struct topology_info {
	int num_cores;
	int num_cpus;
	int num_sockets;
	int num_numa;
	int cores_per_cpu;
	int cores_per_socket;
	int cores_per_numa;
	int cpus_per_socket;
	int cpus_per_numa;
	int sockets_per_numa;
	int max_apic_id;
	struct core_info *core_list;
};

extern struct topology_info cpu_topology_info;
extern int *os_coreid_lookup;
#define num_cores (cpu_topology_info.num_cores)

void topology_init();
void print_cpu_topology();

static inline int get_hw_coreid(uint32_t coreid)
{
	return cpu_topology_info.core_list[coreid].apic_id;
}

static inline int hw_core_id(void)
{
	return lapic_get_id();
}

static inline int get_os_coreid(int hw_coreid)
{
	return os_coreid_lookup[hw_coreid];
}

static inline int numa_id(void)
{
	int os_coreid = os_coreid_lookup[lapic_get_id()];

	return cpu_topology_info.core_list[os_coreid].numa_id;
}

static inline int core_id(void)
{
	int coreid;
	/* assuming we're right after stacktop.  gs base is the pcpui struct,
	 * but we don't have access to the pcpui struct or to the extern
	 * per_cpu_info here, due to include loops. */
	asm volatile ("movl %%gs:8, %0" : "=r"(coreid));
	return coreid;
}

/* Tracks whether it is safe to execute core_id() or not. */
static inline int core_id_early(void)
{
	extern bool core_id_ready;

	if (!core_id_ready)
		return 0;
	return core_id();
}
