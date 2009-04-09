#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <kern/smp.h>
#include <kern/apic.h>

// as far as completion mechs go, we might want a bit mask that every sender 
// has to toggle.  or a more general barrier that works on a queue / LL, 
// instead of everyone.  TODO!
static void smp_call_function(uint8_t type, uint8_t dest, isr_t handler, uint8_t vector)
{
	extern isr_t interrupt_handlers[];
	uint32_t i, amount = 0x7ffffff0; // should calibrate this!!  just remove it!
	bool state;

	if (!vector)
		vector = 0xf1; //default value
	register_interrupt_handler(interrupt_handlers, vector, handler);
	// WRITE MEMORY BARRIER HERE
	state = enable_irqsave();
	// Send the proper type of IPI.  I made up these numbers.
	switch (type) {
		case 1:
			send_self_ipi(vector);
			break;
		case 2:
			send_broadcast_ipi(vector);
			break;
		case 3:
			send_all_others_ipi(vector);
			break;
		case 4: // physical mode
			send_ipi(dest, 0, vector);
			break;
		case 5: // logical mode
			send_ipi(dest, 1, vector);
			break;
		default:
			//panic("Invalid type for cross-core function call!");
	}
	// wait some arbitrary amount til we think all the cores could be done.
	// very wonky without an idea of how long the function takes.
	// maybe should think of some sort of completion notification mech
	for (i = 0; i < amount; i++)
		asm volatile("nop;");
	disable_irqsave(state);
	// consider doing this, but we can't remove it before the receiver is done
	//register_interrupt_handler(interrupt_handlers, vector, 0);
	// we also will have issues if we call this function again too quickly
}

void smp_call_function_self(isr_t handler, uint8_t vector)
{
	smp_call_function(1, 0, handler, vector);
}

void smp_call_function_all(isr_t handler, uint8_t vector)
{
	smp_call_function(2, 0, handler, vector);
}

void smp_call_function_single(uint8_t dest, isr_t handler, uint8_t vector)
{
	smp_call_function(4, dest, handler, vector);
}

