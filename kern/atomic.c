#include <kern/atomic.h>
#include <kern/apic.h>

// byte per cpu, as mentioned below
void init_barrier_all(barrier_t* cpu_barrier)
{
	extern uint8_t num_cpus;
	uint8_t i;
	cpu_barrier->ready = 0;
	for(i = 0; i < num_cpus; i++)
		cpu_barrier->cpu_array[i] = 1;
}

// primitive barrier function.  all cores call this.
// consider changing this to use bits and lock bit ops.
// currently uses a byte per core, and assumes it was 
// initialized by a core such that num_cpus entries
// are all 1
void barrier_all(barrier_t* cpu_barrier)
{
	extern uint8_t num_cpus;
	uint8_t i;
	uint8_t local_ready = cpu_barrier->ready;

	cpu_barrier->cpu_array[lapic_get_id()] = 0;
	if (lapic_get_id())
		while(cpu_barrier->ready == local_ready)
			cpu_relax();
	else {
		for(i = 0; i < num_cpus; i++) {
			while(cpu_barrier->cpu_array[i]) 
				cpu_relax();
			cpu_barrier->cpu_array[i] = 1;
		}
		// if we need to wmb(), it'll be here
		cpu_barrier->ready++;
	}
}
