#include <smp.h>
#include <arch/arch.h>
#include <arch/smp.h>
#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>
#include <atomic.h>
#include <pmap.h>

volatile uint32_t num_cpus_booted = 0;

void
smp_boot(void)
{
	smp_percpu_init();
	num_cpus_booted = 1;
	while(num_cpus_booted < num_cpus);
	printd("%d cores reporting!\n", num_cpus);
}

void
smp_init(void)
{
	smp_percpu_init();

	__sync_fetch_and_add(&num_cpus_booted, 1);
	printd("Good morning, Vietnam! (core id = %d)\n",core_id());

	smp_idle();
}

handler_wrapper_t*
smp_make_wrapper()
{
	static handler_wrapper_t
	wrapper_pool[MAX_NUM_CPUS*8] = {{{0},SPINLOCK_INITIALIZER}};

	size_t i;
	for(i = 0; i < sizeof(wrapper_pool)/sizeof(wrapper_pool[0]); i++)
		if(spin_trylock(&wrapper_pool[i].lock) == 0)
			return &wrapper_pool[i];
	return NULL;
}

void smp_call_wrapper(uint32_t src, isr_t handler, handler_wrapper_t *wrapper,
                      void *data)
{
	if(wrapper)
		wrapper->wait_list[core_id()] = 0;
	handler(0, data);
}

int smp_call_function_self(isr_t handler, void* data,
                           handler_wrapper_t** wait_wrapper)
{
	return smp_call_function_single(core_id(), handler, data, wait_wrapper);
}

int smp_call_function_all(isr_t handler, void* data,
                          handler_wrapper_t** wait_wrapper)
{
	int8_t state = 0;
	int i, me;
	handler_wrapper_t* wrapper = 0;
	if(wait_wrapper)
	{
		wrapper = *wait_wrapper = smp_make_wrapper();
		if(!wrapper)
			return -ENOMEM;

		for(i = 0; i < num_cpus; i++)
			wrapper->wait_list[i] = 1;
	}

	enable_irqsave(&state);

	// send to others
	for(i = 0, me = core_id(); i < num_cpus; i++)
	{
		if(i == me)
			continue;

		send_kernel_message(i, (amr_t)smp_call_wrapper, (long)handler,
		                    (long)wrapper, (long)data, KMSG_IMMEDIATE);
	}

	// send to me
	send_kernel_message(me, (amr_t)smp_call_wrapper, (long)handler,
	                    (long)wrapper, (long)data, KMSG_IMMEDIATE);

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

	send_kernel_message(dest, (amr_t)smp_call_wrapper, (long)handler,
	                    (long)wrapper, (long)data, KMSG_IMMEDIATE);

	cpu_relax(); // wait to get the interrupt, if it's to this core

	disable_irqsave(&state);

	return 0;
}

int smp_call_wait(handler_wrapper_t* wrapper)
{
	int i;
	for(i = 0; i < num_cpus; i++)
		while(wrapper->wait_list[i]);

	spin_unlock(&wrapper->lock);
	return 0;
}

/* Perform any initialization needed by per_cpu_info.  Right now, this just
 * inits the amsg list (which sparc will probably also want).  Make sure every
 * core calls this at some point in the smp_boot process. */
void __arch_pcpu_init(uint32_t coreid)
{
	// Switch to the real L1 page table, rather than the boot page table which
	// has the [0,KERNSIZE-1] identity mapping.
	extern pte_t l1pt[NPTENTRIES];
	lcr3(PADDR(l1pt));

	register uintptr_t sp asm ("sp");
	set_stack_top(ROUNDUP(sp, PGSIZE));
}
