/* Copyright (c) 2015 The Regents of the University of California
 * Valmon Leymarie <leymariv@berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/topology.h>
#include <sys/queue.h>
#include <env.h>
#include <corerequest.h>
#include <kmalloc.h>

enum pnode_type { CORE, CPU, SOCKET, NUMA, MACHINE, NUM_NODE_TYPES };
static char pnode_label[5][8] = { "CORE", "CPU", "SOCKET", "NUMA", "MACHINE" };
#define UNNAMED_PROC ((void*)-1)

/* Internal representation of a node in the hierarchy of elements in the cpu
 * topology of the machine (i.e. numa domain, socket, cpu, core, etc.). */
struct sched_pnode {
	int id;
	enum pnode_type type;
	struct sched_pnode *parent;

	/* Refcount is used to track how many cores have been allocated beneath the
	 * current node in the hierarchy. */
	int refcount;

	/* All nodes except cores have children. Cores have a sched_pcore. */
	union {
		struct sched_pnode *children;
		struct sched_pcore *sched_pcore;
	};
};

#define num_cpus            (cpu_topology_info.num_cpus)
#define num_sockets         (cpu_topology_info.num_sockets)
#define num_numa            (cpu_topology_info.num_numa)
#define cores_per_numa      (cpu_topology_info.cores_per_numa)
#define cores_per_socket    (cpu_topology_info.cores_per_socket)
#define cores_per_cpu       (cpu_topology_info.cores_per_cpu)
#define cpus_per_socket     (cpu_topology_info.cpus_per_socket)
#define cpus_per_numa       (cpu_topology_info.cpus_per_numa)
#define sockets_per_numa    (cpu_topology_info.sockets_per_numa)

#define child_node_type(t) ((t) - 1)

/* The pcores in the system. (array gets alloced in init()).  */
struct sched_pcore *all_pcores;

/* TAILQ of all unallocated, idle (CG) cores */
struct sched_pcore_tailq idlecores = TAILQ_HEAD_INITIALIZER(idlecores);

/* An array containing the number of nodes at each level. */
static int num_nodes[NUM_NODE_TYPES];

/* A 2D array containing for all core i its distance from a core j. */
static int **core_distance;

/* An array containing the number of children at each level. */
static int num_descendants[NUM_NODE_TYPES][NUM_NODE_TYPES];

/* A list of lookup tables to find specific nodes by type and id. */
static int total_nodes;
static struct sched_pnode *all_nodes;
static struct sched_pnode *node_lookup[NUM_NODE_TYPES];

/* Recursively increase the refcount from the node passed in through all of its
 * ancestors in the hierarchy. */
static void incref_nodes(struct sched_pnode *n)
{
	while (n != NULL) {
		n->refcount++;
		n = n->parent;
	}
}

/* Recursively decrease the refcount from the node passed in through all of its
 * ancestors in the hierarchy. */
static void decref_nodes(struct sched_pnode *n)
{
	while (n != NULL) {
		n->refcount--;
		n = n->parent;
	}
}

/* Create a node and initialize it. This code assumes that child are created
 * before parent nodes. */
static void init_nodes(int type, int num, int nchildren)
{
	/* Initialize the lookup table for this node type. */
	num_nodes[type] = num;
	node_lookup[type] = all_nodes;
	for (int i = CORE; i < type; i++)
		node_lookup[type] += num_nodes[i];

	/* Initialize all fields of each node of this type. */
	for (int i = 0; i < num; i++) {
		struct sched_pnode *n = &node_lookup[type][i];

		/* Initialize the common fields. */
		n->id = i;
		n->type = type;
		n->refcount = 0;
		n->parent = NULL;

		/* If we are a core node, initialize the sched_pcore field. */
		if (n->type == CORE) {
			n->sched_pcore = &all_pcores[n->id];
			n->sched_pcore->sched_pnode = n;
			n->sched_pcore->core_info = &cpu_topology_info.core_list[n->id];
			n->sched_pcore->alloc_proc = NULL;
			n->sched_pcore->prov_proc = NULL;
		}
		/* Otherwise initialize the children field, updating the parent of all
		 * children to the current node. This assumes the children have already
		 * been initialized (i.e. init_nodes() must be called iteratively from
		 * the bottom-up). */
		if (n->type != CORE) {
			n->children = &node_lookup[child_node_type(type)][i * nchildren];
			for (int j = 0; j < nchildren; j++)
				n->children[j].parent = n;
		}
	}
}

/* Allocate a table of distances from one core to an other. Cores on the same
 * cpu have a distance of 1; same socket have a distance of 2; same numa -> 3;
 * same machine -> 4; */
static void init_core_distances(void)
{
	core_distance = kzmalloc(num_cores * sizeof(int*), 0);
	if (core_distance == NULL)
		panic("Out of memory!\n");
	for (int i = 0; i < num_cores; i++) {
		core_distance[i] = kzmalloc(num_cores * sizeof(int), 0);
		if (core_distance[i] == NULL)
			panic("Out of memory!\n");
	}
	for (int i = 0; i < num_cores; i++) {
		for (int j = 0; j < num_cores; j++) {
			for (int k = CPU; k <= MACHINE; k++) {
				if (i/num_descendants[k][CORE] ==
					j/num_descendants[k][CORE]) {
					core_distance[i][j] = k;
					break;
				}
			}
		}
	}
}

/* Initialize any data assocaited with doing core allocation. */
void corealloc_init(void)
{
	void *nodes_and_cores;

	/* Allocate a flat array of nodes. */
	total_nodes = num_cores + num_cpus + num_sockets + num_numa + 1;
	nodes_and_cores = kmalloc(total_nodes * sizeof(struct sched_pnode) +
	                          num_cores * sizeof(struct sched_pcore), 0);
	all_nodes = nodes_and_cores;
	all_pcores = nodes_and_cores + total_nodes * sizeof(struct sched_pnode);

	/* Initialize the number of descendants from our cpu_topology info. */
	num_descendants[CPU][CORE] = cores_per_cpu;
	num_descendants[SOCKET][CORE] = cores_per_socket;
	num_descendants[SOCKET][CPU] = cpus_per_socket;
	num_descendants[NUMA][CORE] = cores_per_numa;
	num_descendants[NUMA][CPU] = cpus_per_numa;
	num_descendants[NUMA][SOCKET] = sockets_per_numa;
	num_descendants[MACHINE][CORE] = num_cores;
	num_descendants[MACHINE][CPU] = num_cpus;
	num_descendants[MACHINE][SOCKET] = num_sockets;
	num_descendants[MACHINE][NUMA] = num_numa;

	/* Initialize the nodes at each level in our hierarchy. */
	init_nodes(CORE, num_cores, 0);
	init_nodes(CPU, num_cpus, cores_per_cpu);
	init_nodes(SOCKET, num_sockets, cpus_per_socket);
	init_nodes(NUMA, num_numa, sockets_per_numa);
	init_nodes(MACHINE, 1, num_numa);

	/* Initialize our table of core_distances */
	init_core_distances();

	/* Remove all ll_cores from consideration for allocation. */
	for (int i = 0; i < num_cores; i++)
		if (is_ll_core(i)) {
			all_pcores[i].alloc_proc = UNNAMED_PROC;
			incref_nodes(all_pcores[i].sched_pnode);
		}

#ifdef CONFIG_DISABLE_SMT
	/* Remove all even cores from consideration for allocation. */
	assert(!(num_cores % 2));
	for (int i = 0; i < num_cores; i += 2) {
		all_pcores[i].alloc_proc = UNNAMED_PROC;
		incref_nodes(all_pcores[i].sched_pnode);
	}
#endif /* CONFIG_DISABLE_SMT */

	/* Fill the idlecores array. */
	for (int i = 0; i < num_cores; i++)
		if (all_pcores[i].alloc_proc != UNNAMED_PROC)
			TAILQ_INSERT_HEAD(&idlecores, &all_pcores[i], alloc_next);
}

/* Initialize any data associated with allocating cores to a process. */
void corealloc_proc_init(struct proc *p)
{
	TAILQ_INIT(&p->ksched_data.crd.alloc_me);
	TAILQ_INIT(&p->ksched_data.crd.prov_alloc_me);
	TAILQ_INIT(&p->ksched_data.crd.prov_not_alloc_me);
}

/* Returns the sum of the distances from one core to all cores in a list. */
static int cumulative_core_distance(struct sched_pcore *c,
                                    struct sched_pcore_tailq cl)
{
	int d = 0;
	struct sched_pcore *temp = NULL;

	TAILQ_FOREACH(temp, &cl, alloc_next) {
		d += core_distance[c->core_info->core_id][temp->core_info->core_id];
	}
	return d;
}

/* Returns the first core in the hierarchy under node n. */
static struct sched_pcore *first_core_in_node(struct sched_pnode *n)
{
	struct sched_pnode *first_child = n;

	while (first_child->type != CORE)
		first_child = &first_child->children[0];
	return first_child->sched_pcore;
}

/* Return the first provisioned core available. Otherwise, return NULL. */
static struct sched_pcore *find_first_provisioned_core(struct proc *p)
{
	return TAILQ_FIRST(&(p->ksched_data.crd.prov_not_alloc_me));
}

/* Returns the best first core to allocate for a proc which owns no core.
 * Return the core that is the farthest from the others's proc cores. */
static struct sched_pcore *find_first_core(struct proc *p)
{
	struct sched_pcore *c;
	struct sched_pnode *n;
	struct sched_pnode *nodes;
	struct sched_pnode *bestn;
	int best_refcount;

	/* Find the best, first provisioned core if there are any. Even if the
	 * whole machine is allocated, we still give out provisioned cores, because
	 * they will be revoked from their current owner if necessary. */
	c = find_first_provisioned_core(p);
	if (c != NULL)
		return c;

	/* Otherwise, if the whole machine is already allocated, there are no
	 * cores left to allocate, and we are done. */
	bestn = &node_lookup[MACHINE][0];
	if (bestn->refcount == num_descendants[MACHINE][CORE])
		return NULL;

	/* Otherwise, we know at least one core is still available, so let's find
	 * the best one to allocate first. We start at NUMA, and loop through the
	 * topology to find it. */
	for (int i = NUMA; i >= CORE; i--) {
		nodes = bestn->children;
		best_refcount = total_nodes;
		bestn = NULL;

		for (int j = 0; j < num_nodes[i]; j++) {
			n = &nodes[j];
			if (n->refcount == 0)
				return first_core_in_node(n);
			if (n->refcount == num_descendants[i][CORE])
				continue;
			if (n->refcount < best_refcount) {
				best_refcount = n->refcount;
				bestn = n;
			}
		}
	}
	return bestn->sched_pcore;
}

/* Return the closest core from the list of provisioned cores to cores we
 * already own. If no cores are available we return NULL.*/
static struct sched_pcore *find_closest_provisioned_core(struct proc *p)
{
	struct sched_pcore_tailq provisioned = p->ksched_data.crd.prov_not_alloc_me;
	struct sched_pcore_tailq allocated = p->ksched_data.crd.alloc_me;
	struct sched_pcore *bestc = NULL;
	struct sched_pcore *c = NULL;
	int bestd = 0;

	TAILQ_FOREACH(c, &provisioned, prov_next) {
		int currd = cumulative_core_distance(c, allocated);

		if ((bestd == 0) || (currd < bestd)) {
			bestd = currd;
			bestc = c;
		}
	}
	return bestc;
}

/* Return the closest core from the list of idlecores to cores we already own.
 * If no cores are available we return NULL.*/
static struct sched_pcore *find_closest_idle_core(struct proc *p)
{
	struct sched_pcore_tailq allocated = p->ksched_data.crd.alloc_me;
	struct sched_pcore *bestc = NULL;
	struct sched_pcore *c = NULL;
	int bestd = 0;

	/* TODO: Add optimization to hand out core at equivalent distance if the
	 * best core found is provisioned to someone else. */
	TAILQ_FOREACH(c, &idlecores, alloc_next) {
		int currd = cumulative_core_distance(c, allocated);

		if ((bestd == 0) || (currd < bestd)) {
			bestd = currd;
			bestc = c;
		}
	}
	return bestc;
}

/* Consider the first core provisioned to a proc by calling
 * find_best_provisioned_core(). Then check siblings of the cores the proc
 * already owns. Calculate for every possible node its
 * cumulative_core_distance() (sum of the distances from this core to all of
 * the cores the proc owns). Allocate the core that has the lowest
 * core_distance.  This code assumes that the scheduler that uses it holds a
 * lock for the duration of the call. */
static struct sched_pcore *find_closest_core(struct proc *p)
{
	struct sched_pcore *bestc;

	bestc = find_closest_provisioned_core(p);
	if (bestc)
		return bestc;
	return find_closest_idle_core(p);
}

/* Find the best core to allocate. If no cores are allocated yet, find one that
 * is as far from the cores allocated to other processes as possible.
 * Otherwise, find a core that is as close as possible to one of the other
 * cores we already own. */
uint32_t __find_best_core_to_alloc(struct proc *p)
{
	struct sched_pcore *c = NULL;

	if (TAILQ_FIRST(&(p->ksched_data.crd.alloc_me)) == NULL)
		c = find_first_core(p);
	else
		c = find_closest_core(p);

	if (c == NULL)
		return -1;
	return spc2pcoreid(c);
}

/* Track the pcore properly when it is allocated to p. This code assumes that
 * the scheduler that uses it holds a lock for the duration of the call. */
void __track_core_alloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;

	assert(pcoreid < num_cores);	/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	assert(spc->alloc_proc != p);	/* corruption or double-alloc */
	spc->alloc_proc = p;
	/* if the pcore is prov to them and now allocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.crd.prov_not_alloc_me, spc, prov_next);
		TAILQ_INSERT_TAIL(&p->ksched_data.crd.prov_alloc_me, spc, prov_next);
	}
	/* Actually allocate the core, removing it from the idle core list. */
	TAILQ_INSERT_TAIL(&p->ksched_data.crd.alloc_me, spc, alloc_next);
	TAILQ_REMOVE(&idlecores, spc, alloc_next);
	incref_nodes(spc->sched_pnode);
}

/* Track the pcore properly when it is deallocated from p. This code assumes
 * that the scheduler that uses it holds a lock for the duration of the call.
 * */
void __track_core_dealloc(struct proc *p, uint32_t pcoreid)
{
	struct sched_pcore *spc;

	assert(pcoreid < num_cores);	/* catch bugs */
	spc = pcoreid2spc(pcoreid);
	spc->alloc_proc = NULL;
	/* if the pcore is prov to them and now deallocated, move lists */
	if (spc->prov_proc == p) {
		TAILQ_REMOVE(&p->ksched_data.crd.prov_alloc_me, spc, prov_next);
		/* this is the victim list, which can be sorted so that we pick the
		 * right victim (sort by alloc_proc reverse priority, etc).  In this
		 * case, the core isn't alloc'd by anyone, so it should be the first
		 * victim. */
		TAILQ_INSERT_HEAD(&p->ksched_data.crd.prov_not_alloc_me, spc,
		                  prov_next);
	}
	/* Actually dealloc the core, putting it back on the idle core list. */
	TAILQ_REMOVE(&(p->ksched_data.crd.alloc_me), spc, alloc_next);
	TAILQ_INSERT_HEAD(&idlecores, spc, alloc_next);
	decref_nodes(spc->sched_pnode);
}

/* Bulk interface for __track_core_dealloc */
void __track_core_dealloc_bulk(struct proc *p, uint32_t *pc_arr,
                               uint32_t nr_cores)
{
	for (int i = 0; i < nr_cores; i++)
		__track_core_dealloc(p, pc_arr[i]);
}

/* Get an idle core from our pcore list and return its core_id. Don't
 * consider the chosen core in the future when handing out cores to a
 * process. This code assumes that the scheduler that uses it holds a lock
 * for the duration of the call. This will not give out provisioned cores. */
int __get_any_idle_core(void)
{
	struct sched_pcore *spc;
	int ret = -1;

	for (int i = 0; i < num_cores; i++) {
		struct sched_pcore *c = &all_pcores[i];

		if (spc->alloc_proc == NULL) {
			spc->alloc_proc = UNNAMED_PROC;
			ret = spc->core_info->core_id;
		}
	}
	return ret;
}

/* Detect if a pcore is idle or not. */
static bool __spc_is_idle(struct sched_pcore *spc)
{
	return (spc->alloc_proc == NULL);
}

/* Same as __get_any_idle_core() except for a specific core id. */
int __get_specific_idle_core(int coreid)
{
	struct sched_pcore *spc = pcoreid2spc(coreid);
	int ret = -1;

	assert((coreid >= 0) && (coreid < num_cores));
	if (__spc_is_idle(pcoreid2spc(coreid)) && !spc->prov_proc) {
		assert(!spc->alloc_proc);
		spc->alloc_proc = UNNAMED_PROC;
		ret = coreid;
	}
	return ret;
}

/* Reinsert a core obtained via __get_any_idle_core() or
 * __get_specific_idle_core() back into the idlecore map. This code assumes
 * that the scheduler that uses it holds a lock for the duration of the call.
 * This will not give out provisioned cores. */
void __put_idle_core(int coreid)
{
	struct sched_pcore *spc = pcoreid2spc(coreid);

	assert((coreid >= 0) && (coreid < num_cores));
	spc->alloc_proc = NULL;
}

/* One off function to make 'pcoreid' the next core chosen by the core
 * allocation algorithm (so long as no provisioned cores are still idle).
 * This code assumes that the scheduler that uses it holds a lock for the
 * duration of the call. */
void __next_core_to_alloc(uint32_t pcoreid)
{
	printk("This function is not supported by this core allocation policy!\n");
}

/* One off function to sort the idle core list for debugging in the kernel
 * monitor. This code assumes that the scheduler that uses it holds a lock
 * for the duration of the call. */
void __sort_idle_cores(void)
{
	printk("This function is not supported by this core allocation policy!\n");
}

/* Print the map of idle cores that are still allocatable through our core
 * allocation algorithm. */
void print_idle_core_map(void)
{
	printk("Idle cores (unlocked!):\n");
	for (int i = 0; i < num_cores; i++) {
		struct sched_pcore *spc_i = &all_pcores[i];

		if (spc_i->alloc_proc == NULL)
			printk("Core %d, prov to %d (%p)\n", spc_i->core_info->core_id,
			       spc_i->prov_proc ? spc_i->prov_proc->pid :
				   0, spc_i->prov_proc);
	}
}
