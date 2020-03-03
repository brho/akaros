/* Copyright (c) 2015 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details. */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <kmalloc.h>
#include <string.h>
#include <ns.h>
#include <acpi.h>
#include <arch/arch.h>
#include <arch/apic.h>
#include <arch/topology.h>

struct topology_info cpu_topology_info;
int *os_coreid_lookup;

#define num_cpus            (cpu_topology_info.num_cpus)
#define num_sockets         (cpu_topology_info.num_sockets)
#define num_numa            (cpu_topology_info.num_numa)
#define cores_per_numa      (cpu_topology_info.cores_per_numa)
#define cores_per_socket    (cpu_topology_info.cores_per_socket)
#define cores_per_cpu       (cpu_topology_info.cores_per_cpu)
#define cpus_per_socket     (cpu_topology_info.cpus_per_socket)
#define cpus_per_numa       (cpu_topology_info.cpus_per_numa)
#define sockets_per_numa    (cpu_topology_info.sockets_per_numa)
#define max_apic_id         (cpu_topology_info.max_apic_id)
#define core_list           (cpu_topology_info.core_list)

/* Adjust the ids from any given node type to start at 0 and increase from
 * there. We use the id_offset in the core_list to index the proper field. */
static void adjust_ids(int id_offset)
{
	int new_id = 0, old_id = -1;

	for (int i = 0; i < num_cores; i++) {
		for (int j = 0; j < num_cores; j++) {
			int *id_field = ((void*)&core_list[j] + id_offset);
			if (*id_field >= new_id) {
				if (old_id == -1)
					old_id = *id_field;
				if (old_id == *id_field)
					*id_field = new_id;
			}
		}
		old_id=-1;
		new_id++;
	}
}

/* Set the actual socket id from the raw socket id extracted from cpuid.  This
 * algorithm is adapted from the algorithm given at
 * http://wiki.osdev.org/Detecting_CPU_Topology_(80x86) */
static void set_socket_ids(void)
{
	int socket_id, raw_socket_id;

	for (int numa_id = 0; numa_id < num_numa; numa_id++) {
		socket_id = 0;
		for (int i = 0; i < num_cores; i++) {
			if (core_list[i].numa_id == numa_id) {
				if (core_list[i].socket_id == -1) {
					core_list[i].socket_id = socket_id;
					raw_socket_id =
						core_list[i].raw_socket_id;
					for (int j = i; j < num_cores; j++) {
						if (core_list[j].numa_id ==
						    numa_id) {
							if (core_list[j].raw_socket_id == raw_socket_id) {
								core_list[j].socket_id = socket_id;
							}
						}
					}
				}
				socket_id++;
			}
		}
	}
}

/* Loop through our Srat table to find a matching numa domain for the given
 * apid_id. */
static int find_numa_domain(int apic_id)
{
	if (srat == NULL)
		return -1;

	for (int i = 0; i < srat->nchildren; i++) {
		struct Srat *temp = srat->children[i]->tbl;

		if (temp != NULL && temp->type == SRlapic) {
			if (temp->lapic.apic == apic_id)
				return temp->lapic.dom;
		}
	}
	return -1;
}

/* Figure out the maximum number of cores we actually have and set it in our
 * cpu_topology_info struct. */
static void set_num_cores(void)
{
	int old_num_cores = num_cores;

	if (apics == NULL)
		return;

	num_cores = 0;
	for (int i = 0; i < apics->nchildren; i++) {
		struct Apicst *temp = apics->children[i]->tbl;

		// XXX
		if (temp != NULL && (temp->type == ASlapic || temp->type == ASlx2apic))
			num_cores++;
	}
	if (num_cores < old_num_cores) {
		warn("Topology found less cores (%d) than early MADT parsing! (%d)", num_cores, old_num_cores);
		// XXX fucked
		if (!num_cores)
			num_cores = 2;
	}
	/* Too many cores will be a problem for some data structures. */
	if (num_cores > old_num_cores)
		panic("Topology found more cores than early MADT parsing!");
}

/* Determine if srat has a unique numa domain compared to to all of the srat
 * records in list_head that are of type SRlapic.
 *
 * Note that this only finds a unique NUMA domain when we're on the last core in
 * the list with that domain.  When we find that one, we'll need to scan the
 * O(n) other cores from the other domains that are ahead of us in the list.
 * It's a little inefficient, but OK for now. */
static bool is_unique_numa(struct Srat *srat, struct Atable **tail,
                           size_t begin, size_t end)
{
	for (int i = begin; i < end; i++) {
		struct Srat *st = tail[i]->tbl;

		if (st && st->type == SRlapic)
			if (srat->lapic.dom == st->lapic.dom)
				return FALSE;
	}
	return TRUE;
}

/* Figure out the maximum number of numa domains we actually have.
 * This code should always return >= 0 domains. */
static int get_num_numa(void)
{
	int numa = 0;

	if (srat == NULL)
		return 0;

	for (int i = 0; i < srat->nchildren; i++) {
		struct Srat *temp = srat->children[i]->tbl;

		if (temp != NULL && temp->type == SRlapic)
			if (is_unique_numa(temp, srat->children, i + 1,
					   srat->nchildren))
				numa++;
	}

	return numa;
}

/* Set num_numa in our topology struct */
static void set_num_numa(void)
{
	num_numa = get_num_numa();
}

/* Figure out what the max apic_id we will ever have is and set it in our
 * cpu_topology_info struct. */
static void set_max_apic_id(void)
{
	for (int i = 0; i < apics->nchildren; i++) {
		struct Apicst *temp = apics->children[i]->tbl;

		switch (temp->type) {
		case ASlapic:
			if (temp->lapic.id > max_apic_id)
				max_apic_id = temp->lapic.id;
			break;
		case ASlx2apic:
			if (temp->lx2apic.id > max_apic_id)
				max_apic_id = temp->lx2apic.id;
			break;
		}
	}
}

static void init_os_coreid_lookup(void)
{
	/* Allocate (max_apic_id+1) entries in our os_coreid_lookup table.
	 * There may be holes in this table because of the way apic_ids work,
	 * but a little wasted space is OK for a constant time lookup of apic_id
	 * -> logical core id (from the OS's perspective). Memset the array to
	 *  -1 to to represent invalid entries (which it's very possible we
	 *  might have if the apic_id space has holes in it).  */
	os_coreid_lookup = kmalloc((max_apic_id + 1) * sizeof(int), 0);
	memset(os_coreid_lookup, -1, (max_apic_id + 1) * sizeof(int));

	/* Loop through and set all valid entries to 0 to start with (making
	 * them temporarily valid, but not yet set to the correct value). This
	 * step is necessary because there is no ordering to the linked list we
	 * are
	 * pulling these ids from. After this, loop back through and set the
	 * mapping appropriately. */
	for (int i = 0; i < apics->nchildren; i++) {
		struct Apicst *temp = apics->children[i]->tbl;

		switch (temp->type) {
		case ASlapic:
			os_coreid_lookup[temp->lapic.id] = 0;
			break;
		case ASlx2apic:
			os_coreid_lookup[temp->lx2apic.id] = 0;
			break;
		}
	}
	int os_coreid = 0;

	for (int i = 0; i <= max_apic_id; i++)
		if (os_coreid_lookup[i] == 0)
			os_coreid_lookup[i] = os_coreid++;
}

static void init_core_list(uint32_t core_bits, uint32_t cpu_bits)
{
	/* Assuming num_cores and max_apic_id have been set, we can allocate our
	 * core_list to the proper size. Initialize all entries to 0s to being
	 * with. */
	core_list = kzmalloc(num_cores * sizeof(struct core_info), 0);

	/* Loop through all possible apic_ids and fill in the core_list array
	 * with *relative* topology info. We will change this relative info to
	 * absolute info in a future step. As part of this step, we update our
	 * os_coreid_lookup array to contain the proper value. */
	int os_coreid = 0;
	int max_cpus = (1 << cpu_bits);
	int max_cores_per_cpu = (1 << core_bits);
	int max_logical_cores = (1 << (core_bits + cpu_bits));
	int raw_socket_id = 0, cpu_id = 0, core_id = 0;

	for (int apic_id = 0; apic_id <= max_apic_id; apic_id++) {
		if (os_coreid_lookup[apic_id] != -1) {
			raw_socket_id = apic_id & ~(max_logical_cores - 1);
			cpu_id = (apic_id >> core_bits) & (max_cpus - 1);
			core_id = apic_id & (max_cores_per_cpu - 1);

			core_list[os_coreid].numa_id =
				find_numa_domain(apic_id);
			core_list[os_coreid].raw_socket_id = raw_socket_id;
			core_list[os_coreid].socket_id = -1;
			core_list[os_coreid].cpu_id = cpu_id;
			core_list[os_coreid].core_id = core_id;
			core_list[os_coreid].apic_id = apic_id;
			os_coreid++;
		}
	}

	/* In general, the various id's set in the previous step are all unique
	 * in terms of representing the topology (i.e. all cores under the same
	 * socket have the same socket_id set), but these id's are not
	 * necessarily contiguous, and are only relative to the level of the
	 * hierarchy they exist at (e.g.  cpu_id 4 may exist under *both*
	 * socket_id 0 and socket_id 1). In this step, we squash these id's down
	 * so they are contiguous. In a following step, we will make them all
	 * absolute instead of relative. */
	adjust_ids(offsetof(struct core_info, numa_id));
	adjust_ids(offsetof(struct core_info, raw_socket_id));
	adjust_ids(offsetof(struct core_info, cpu_id));
	adjust_ids(offsetof(struct core_info, core_id));

	/* We haven't yet set the socket id of each core yet. So far, all we've
	 * extracted is a "raw" socket id from the top bits in our apic id, but
	 * we need to condense these down into something workable for a socket
	 * id, per numa domain. OSDev has an algorithm for doing so
	 * (http://wiki.osdev.org/Detecting_CPU_Topology_%2880x86%29).
	 * We adapt it for our setup. */
	set_socket_ids();
}

static void init_core_list_flat(void)
{
	/* Assuming num_cores and max_apic_id have been set, we can allocate our
	 * core_list to the proper size. Initialize all entries to 0s to being
	 * with. */
	core_list = kzmalloc(num_cores * sizeof(struct core_info), 0);

	/* Loop through all possible apic_ids and fill in the core_list array
	 * with flat topology info. */
	int os_coreid = 0;

	for (int apic_id = 0; apic_id <= max_apic_id; apic_id++) {
		if (os_coreid_lookup[apic_id] != -1) {
			core_list[os_coreid].numa_id = 0;
			core_list[os_coreid].raw_socket_id = 0;
			core_list[os_coreid].socket_id = 0;
			core_list[os_coreid].cpu_id = 0;
			core_list[os_coreid].core_id = os_coreid;
			core_list[os_coreid].apic_id = apic_id;
			os_coreid++;
		}
	}
}

static void set_remaining_topology_info(void)
{
	/* Assuming we have our core_list set up with relative topology info,
	 * loop through our core_list and calculate the other statistics that we
	 * hold in our cpu_topology_info struct. */
	int last_numa = -1, last_socket = -1, last_cpu = -1, last_core = -1;
	for (int i = 0; i < num_cores; i++) {
		if (core_list[i].socket_id > last_socket) {
			last_socket = core_list[i].socket_id;
			sockets_per_numa++;
		}
		if (core_list[i].cpu_id > last_cpu) {
			last_cpu = core_list[i].cpu_id;
			cpus_per_socket++;
		}
		if (core_list[i].core_id > last_core) {
			last_core = core_list[i].core_id;
			cores_per_cpu++;
		}
	}
	cores_per_socket = cpus_per_socket * cores_per_cpu;
	cores_per_numa = sockets_per_numa * cores_per_socket;
	cpus_per_numa = sockets_per_numa * cpus_per_socket;
	num_sockets = sockets_per_numa * num_numa;
	num_cpus = cpus_per_socket * num_sockets;
}

static void update_core_list_with_absolute_ids(void)
{
	/* Fix up our core_list to have absolute id's at every level. */
	for (int i = 0; i < num_cores; i++) {
		struct core_info *c = &core_list[i];
		c->socket_id = num_sockets/num_numa * c->numa_id + c->socket_id;
		c->cpu_id = num_cpus/num_sockets * c->socket_id + c->cpu_id;
		c->core_id = num_cores/num_cpus * c->cpu_id + c->core_id;
	}
}

static void build_topology(uint32_t core_bits, uint32_t cpu_bits)
{
	set_num_cores();
	set_num_numa();
	set_max_apic_id();
	init_os_coreid_lookup();
	init_core_list(core_bits, cpu_bits);
	set_remaining_topology_info();
	update_core_list_with_absolute_ids();
}

static void build_flat_topology(void)
{
	set_num_cores();
	num_numa = 1;
	set_max_apic_id();
	init_os_coreid_lookup();
	init_core_list_flat();
	set_remaining_topology_info();
}

void topology_init(void)
{
	uint32_t eax, ebx, ecx, edx;
	int smt_leaf, core_leaf;
	uint32_t core_bits = 0, cpu_bits = 0;

	eax = 0x0000000b;
	ecx = 1;
	cpuid(eax, ecx, &eax, &ebx, &ecx, &edx);
	core_leaf = (ecx >> 8) & 0x00000002;
	if (core_leaf == 2) {
		cpu_bits = eax;
		eax = 0x0000000b;
		ecx = 0;
		cpuid(eax, ecx, &eax, &ebx, &ecx, &edx);
		smt_leaf = (ecx >> 8) & 0x00000001;
		if (smt_leaf == 1) {
			core_bits = eax;
			cpu_bits = cpu_bits - core_bits;
		}
	}
	/* BIOSes are not strictly required to put NUMA information
	 * into the ACPI table. If there is no information the safest
	 * thing to do is assume it's a non-NUMA system, i.e. flat. */
	if (cpu_bits && get_num_numa())
		build_topology(core_bits, cpu_bits);
	else
		build_flat_topology();
}

// XXX busted on cta9.  should be 2 sockets.  probably ACPI vs cpuid?
void print_cpu_topology(void)
{
	printk("num_numa: %d, num_sockets: %d, num_cpus: %d, num_cores: %d\n",
	       num_numa, num_sockets, num_cpus, num_cores);
	for (int i = 0; i < num_cores; i++) {
		printk("OScoreid: %3d, HWcoreid: %3d, RawSocketid: %3d, "
		       "Numa Domain: %3d, Socket: %3d, Cpu: %3d, Core: %3d\n",
		       i,
		       core_list[i].apic_id,
		       core_list[i].numa_id,
		       core_list[i].raw_socket_id,
		       core_list[i].socket_id,
		       core_list[i].cpu_id,
		       core_list[i].core_id);
	}
}
