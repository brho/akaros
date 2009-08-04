#include <arch/arch.h>

#include <atomic.h>

void init_barrier(barrier_t* barrier, uint32_t count)
{
	barrier->lock = 0;
	barrier->init_count = count;
	barrier->current_count = count;
	barrier->ready = 0;
}

void reset_barrier(barrier_t* barrier)
{
	barrier->current_count = barrier->init_count;
}

void waiton_barrier(barrier_t* barrier)
{
	uint8_t local_ready = barrier->ready;

	spin_lock(&barrier->lock);
	barrier->current_count--;
	if (barrier->current_count) {
		spin_unlock(&barrier->lock);
		while (barrier->ready == local_ready)
			cpu_relax();
	} else {
		spin_unlock(&barrier->lock);
		reset_barrier(barrier);
		barrier->ready++;
	}
}
