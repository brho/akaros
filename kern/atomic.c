#include <kern/atomic.h>
#include <kern/apic.h>

// byte per cpu, as mentioned below
void init_barrier(barrier_t cpu_array)
{
	extern uint8_t num_cpus;
	uint8_t i;
	for(i = 0; i < num_cpus; i++)
		cpu_array[i] = 1;
}

// primitive barrier function.  all cores call this.
// should change this to use bits and lock bit ops.
// currently uses a byte per core, and assumes it was 
// initialized by a core such that up num_cpus entries
// are all 1
void barrier(barrier_t cpu_array)
{
	extern uint8_t num_cpus;
	uint8_t i;

	cpu_array[lapic_get_id()] = 0;
	for(i = 0; i < num_cpus; i++) {
		while(cpu_array[i]) 
			cpu_relax();
	}
}
