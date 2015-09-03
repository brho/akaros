/* Copyright (c) 2015 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#ifndef AKAROS_ARCH_TOPOLOGY_H
#define AKAROS_ARCH_TOPOLOGY_H

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

#endif /* AKAROS_ARCH_TOPOLOGY_H */
