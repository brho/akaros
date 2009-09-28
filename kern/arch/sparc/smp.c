#include <smp.h>
#include <arch/arch.h>
#include <arch/smp.h>
#include <stdio.h>
#include <string.h>
#include <ros/error.h>
#include <assert.h>
#include <atomic.h>

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

void
smp_boot(void)
{
	extern int time_for_smp_init;
	num_cpus = 1;
	cprintf("Cores, report in!\n");
	time_for_smp_init = 1;

	while(*(volatile uint8_t*)&num_cpus < num_cores());

	cprintf("All cores reporting!\n");
}

void
smp_init(void)
{
	static spinlock_t report_in_lock = 0;

	cprintf("Good morning, Vietnam! (core id = %d)\n",core_id());

	spin_lock(&report_in_lock);
	num_cpus++;
	spin_unlock(&report_in_lock);

	smp_idle();
}

handler_wrapper_t
wrapper_pool[MAX_NUM_CPUS*8] = {{{0},0}};

handler_wrapper_t*
smp_make_wrapper()
{
	int i;
	for(i = 0; i < sizeof(wrapper_pool)/sizeof(wrapper_pool[0]); i++)
		if(spin_trylock(&wrapper_pool[i].lock) == 0)
			return &wrapper_pool[i];
	return NULL;
}

void
smp_call_wrapper(trapframe_t* tf, uint32_t src, isr_t handler,
                 handler_wrapper_t* wrapper,void* data)
{
	if(wrapper)
		wrapper->wait_list[core_id()] = 0;
	handler(tf,data);
}

int smp_call_function_self(isr_t handler, void* data,
                           handler_wrapper_t** wait_wrapper)
{
	return smp_call_function_single(core_id(),handler,data,wait_wrapper);
}

int smp_call_function_all(isr_t handler, void* data,
                          handler_wrapper_t** wait_wrapper)
{
	int8_t state = 0;
	int i;
	handler_wrapper_t* wrapper = 0;
	if(wait_wrapper)
	{
		wrapper = *wait_wrapper = smp_make_wrapper();
		if(!wrapper)
			return -ENOMEM;

		for(i = 0; i < num_cores(); i++)
			wrapper->wait_list[i] = 1;
	}

	enable_irqsave(&state);

	// send to others
	for(i = 0; i < num_cores(); i++)
	{
		if(i == core_id())
			continue;

		while(send_active_message(i,(amr_t)smp_call_wrapper,
	        	                  handler, wrapper, data) != 0);
	}

	// send to me
	while(send_active_message(core_id(),(amr_t)smp_call_wrapper,
	                          handler,wrapper,data) != 0);

	cpu_relax(); // wait to get the interrupt

	disable_irqsave(&state);

	return 0;
}

int smp_call_function_single(uint32_t dest, isr_t handler, void* data,
                             handler_wrapper_t** wait_wrapper)
{
	int8_t state = 0;
	handler_wrapper_t* wrapper = 0;
	if(wait_wrapper)
	{
		wrapper = *wait_wrapper = smp_make_wrapper();
		if(!wrapper)
			return -ENOMEM;
		wrapper->wait_list[dest] = 1;
	}

	enable_irqsave(&state);

	while(send_active_message(dest,(amr_t)smp_call_wrapper,
	                          handler,wrapper,data) != 0);

	cpu_relax(); // wait to get the interrupt, if it's to this core

	disable_irqsave(&state);

	return 0;
}

int smp_call_wait(handler_wrapper_t* wrapper)
{
	int i;
	for(i = 0; i < num_cores(); i++)
		while(wrapper->wait_list[i]);

	spin_unlock(&wrapper->lock);
	return 0;
}
