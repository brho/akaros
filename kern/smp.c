#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/x86.h>

#include <kern/smp.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <kern/apic.h>
#include <kern/atomic.h>

volatile uint8_t num_cpus = 0xee;
uintptr_t smp_stack_top;
barrier_t generic_barrier;

/*************************** IPI Wrapper Stuff ********************************/
// checklists to protect the global interrupt_handlers for 0xf0, f1, f2, f3, f4
// need to be global, since there is no function that will always exist for them
handler_wrapper_t             handler_wrappers[NUM_HANDLER_WRAPPERS];
// tracks number of global waits on smp_calls, must be <= NUM_HANDLER_WRAPPERS
uint32_t outstanding_calls = 0; 

#define DECLARE_HANDLER_CHECKLISTS(vector)                          \
	INIT_CHECKLIST(f##vector##_cpu_list, MAX_NUM_CPUS);

#define INIT_HANDLER_WRAPPER(v)                                     \
{                                                                   \
	handler_wrappers[(v)].vector = 0xf##v;                          \
	handler_wrappers[(v)].cpu_list = &f##v##_cpu_list;              \
	handler_wrappers[(v)].cpu_list->mask.size = num_cpus;           \
}

DECLARE_HANDLER_CHECKLISTS(0);
DECLARE_HANDLER_CHECKLISTS(1);
DECLARE_HANDLER_CHECKLISTS(2);
DECLARE_HANDLER_CHECKLISTS(3);
DECLARE_HANDLER_CHECKLISTS(4);

static void init_smp_call_function(void)
{
	INIT_HANDLER_WRAPPER(0);
	INIT_HANDLER_WRAPPER(1);
	INIT_HANDLER_WRAPPER(2);
	INIT_HANDLER_WRAPPER(3);
	INIT_HANDLER_WRAPPER(4);
}

/******************************************************************************/

static void smp_mtrr_handler(trapframe_t *tf)
{
	setup_default_mtrrs(&generic_barrier);
}

void smp_boot(void)
{
	#define boot_vector 0xeb
	// this needs to be set in smp_entry too...
	#define trampoline_pg 0x00001000
	page_t *smp_stack;
	extern isr_t interrupt_handlers[];
	// NEED TO GRAB A LOWMEM FREE PAGE FOR AP BOOTUP CODE
	// page1 (2nd page) is reserved, hardcoded in pmap.c
	extern smp_entry(), smp_entry_end(), smp_boot_lock(), smp_semaphore();
	memset(KADDR(trampoline_pg), 0, PGSIZE);
	memcpy(KADDR(trampoline_pg), &smp_entry, &smp_entry_end - &smp_entry);

	// This mapping allows access to the trampoline with paging on and off
	// via trampoline_pg
	page_insert(boot_pgdir, pa2page(trampoline_pg), (void*)trampoline_pg, PTE_W);

	// Allocate a stack for the cores starting up.  One for all, must share
	if (page_alloc(&smp_stack))
		panic("No memory for SMP boot stack!");
	smp_stack_top = (uintptr_t)(page2kva(smp_stack) + PGSIZE);

	// Start the IPI process (INIT, wait, SIPI, wait, SIPI, wait)
	send_init_ipi();
	// SDM 3A is a little wonky wrt the proper delays.  These are my best guess.
	udelay(10000);
	// first SIPI
	send_startup_ipi(0x01);
	/* BOCHS does not like this second SIPI.
	// second SIPI
	udelay(200);
	send_startup_ipi(0x01);
	*/
	udelay(100000);

	// Each core will also increment smp_semaphore, and decrement when it is done,
	// all in smp_entry.  It's purpose is to keep Core0 from competing for the
	// smp_boot_lock.  So long as one AP increments the sem before the final
	// LAPIC timer goes off, all available cores will be initialized.
	while(*(volatile uint32_t*)(&smp_semaphore - &smp_entry + trampoline_pg));

	// From here on, no other cores are coming up.  Grab the lock to ensure it.
	// Another core could be in it's prelock phase and be trying to grab the lock
	// forever....
	// The lock exists on the trampoline, so it can be grabbed right away in
	// real mode.  If core0 wins the race and blocks other CPUs from coming up
	// it can crash the machine if the other cores are allowed to proceed with
	// booting.  Specifically, it's when they turn on paging and have that temp
	// mapping pulled out from under them.  Now, if a core loses, it will spin
	// on the trampoline (which we must be careful to not deallocate)
	spin_lock((uint32_t*)(&smp_boot_lock - &smp_entry + trampoline_pg));
	cprintf("Num_Cpus Detected: %d\n", num_cpus);

	// Deregister smp_boot_handler
	register_interrupt_handler(interrupt_handlers, boot_vector, 0);
	// Remove the mapping of the page used by the trampoline
	page_remove(boot_pgdir, (void*)trampoline_pg);
	// It had a refcount of 2 earlier, so we need to dec once more to free it
	// but only if all cores are in (or we reset / reinit those that failed)
	// TODO after we parse ACPI tables
	if (num_cpus == 8) // TODO - ghetto coded for our 8 way SMPs
		page_decref(pa2page(trampoline_pg));
	// Dealloc the temp shared stack
	page_decref(smp_stack);

	// Set up the generic remote function call facility
	init_smp_call_function();

	// Set up all cores to use the proper MTRRs
	init_barrier(&generic_barrier, num_cpus); // barrier used by smp_mtrr_handler
	smp_call_function_all(smp_mtrr_handler, 0);

	// Should probably flush everyone's TLB at this point, to get rid of
	// temp mappings that were removed.  TODO
}

/*
 * This is called from smp_entry by each core to finish the core bootstrapping.
 * There is a spinlock around this entire function in smp_entry, for a few reasons,
 * the most important being that all cores use the same stack when entering here.
 */
uint32_t smp_main(void)
{
	/*
	// Print some diagnostics.  Uncomment if there're issues.
	cprintf("Good morning Vietnam!\n");
	cprintf("This core's Default APIC ID: 0x%08x\n", lapic_get_default_id());
	cprintf("This core's Current APIC ID: 0x%08x\n", lapic_get_id());
	if (read_msr(IA32_APIC_BASE) & 0x00000100)
		cprintf("I am the Boot Strap Processor\n");
	else
		cprintf("I am an Application Processor\n");
	cprintf("Num_Cpus: %d\n\n", num_cpus);
	*/

	// Get a per-core kernel stack
	page_t *my_stack;
	if (page_alloc(&my_stack))
		panic("Unable to alloc a per-core stack!");
	memset(page2kva(my_stack), 0, PGSIZE);

	// Set up a gdt / gdt_pd for this core, stored at the top of the stack
	// This is necessary, eagle-eyed readers know why
	// GDT should be 4-byte aligned.  TS isn't aligned.  Not sure if it matters.
	pseudodesc_t *my_gdt_pd = page2kva(my_stack) + PGSIZE -
		sizeof(pseudodesc_t) - sizeof(segdesc_t)*SEG_COUNT;
	segdesc_t *my_gdt = page2kva(my_stack) + PGSIZE -
		sizeof(segdesc_t)*SEG_COUNT;
	// TS also needs to be permanent
	taskstate_t *my_ts = page2kva(my_stack) + PGSIZE -
		sizeof(pseudodesc_t) - sizeof(segdesc_t)*SEG_COUNT -
		sizeof(taskstate_t);
	// Usable portion of the KSTACK grows down from here
	// Won't actually start using this stack til our first interrupt
	// (issues with changing the stack pointer and then trying to "return")
	uintptr_t my_stack_top = (uintptr_t)my_ts;

	// Build and load the gdt / gdt_pd
	memcpy(my_gdt, gdt, sizeof(segdesc_t)*SEG_COUNT);
	*my_gdt_pd = (pseudodesc_t) {
		sizeof(segdesc_t)*SEG_COUNT - 1, (uintptr_t) my_gdt };
	asm volatile("lgdt %0" : : "m"(*my_gdt_pd));

	// Need to set the TSS so we know where to trap on this core
	my_ts->ts_esp0 = my_stack_top;
	my_ts->ts_ss0 = GD_KD;
	// Initialize the TSS field of my_gdt.
	my_gdt[GD_TSS >> 3] = SEG16(STS_T32A, (uint32_t) (my_ts), sizeof(taskstate_t), 0);
	my_gdt[GD_TSS >> 3].sd_s = 0;
	// Load the TSS
	ltr(GD_TSS);

	// Loads the same IDT used by the other cores
	asm volatile("lidt idt_pd");

	// APIC setup
	// set LINT0 to receive ExtINTs (KVM's default).  At reset they are 0x1000.
	write_mmreg32(LAPIC_LVT_LINT0, 0x700);
	// mask it to shut it up for now.  Doesn't seem to matter yet, since both
	// KVM and Bochs seem to only route the PIC to core0.
	mask_lapic_lvt(LAPIC_LVT_LINT0);
	// and then turn it on
	lapic_enable();

	// set a default logical id for now
	lapic_set_logid(lapic_get_id());

	return my_stack_top; // will be loaded in smp_entry.S
}

// this idles a core, sometimes we need to call it directly.  never returns.
// the pause is somewhat of a hack, since kvm isn't halting.  not sure what the
// deal is with that.
void smp_idle(void)
{
	asm volatile("1: hlt; pause; jmp 1b;");
}

static int smp_call_function(uint8_t type, uint8_t dest, isr_t handler, 
                              handler_wrapper_t** wait_wrapper)
{
	extern isr_t interrupt_handlers[];
	int8_t state = 0;
	uint32_t wrapper_num;
	handler_wrapper_t* wrapper;

	// prevents us from ever having more than NUM_HANDLER_WRAPPERS callers in
	// the process of competing for vectors.  not decremented until both after
	// the while(1) loop and after it's been waited on.
	atomic_inc(&outstanding_calls);
	if (outstanding_calls > NUM_HANDLER_WRAPPERS) {
		atomic_dec(&outstanding_calls);
		return E_BUSY;
	}
	
	// assumes our cores are numbered in order
	if ((type == 4) && (dest >= num_cpus))
		panic("Destination CPU does not exist!");

	// build the mask based on the type and destination
	INIT_CHECKLIST_MASK(cpu_mask, MAX_NUM_CPUS);
	// set checklist mask's size dynamically to the num cpus actually present
	cpu_mask.size = num_cpus;
	switch (type) {
		case 1: // self
			SET_BITMASK_BIT(cpu_mask.bits, lapic_get_id());
			break;
		case 2: // all
			FILL_BITMASK(cpu_mask.bits, num_cpus);
			break;
		case 3: // all but self
			FILL_BITMASK(cpu_mask.bits, num_cpus);
			CLR_BITMASK_BIT(cpu_mask.bits, lapic_get_id());
			break;
		case 4: // physical mode
			// note this only supports sending to one specific physical id
			// (only sets one bit, so if multiple cores have the same phys id
			// the first one through will set this).
			SET_BITMASK_BIT(cpu_mask.bits, dest);
			break;
		case 5: // logical mode
			// TODO
			warn("Logical mode bitmask handler protection not implemented!");
			break;
		default:
			panic("Invalid type for cross-core function call!");
	}

	// Find an available vector/wrapper.  Starts with this core's id (mod the
	// number of wrappers).  Walk through on conflict.
	// Commit returns an error if it wanted to give up for some reason,
	// like taking too long to acquire the lock or clear the mask, at which
	// point, we try the next one.
	// When we are done, wrapper points to the one we finally got.
	// this wrapper_num trick doesn't work as well if you send a bunch in a row
	// and wait, since you always check your main one (which is currently busy).
	wrapper_num = lapic_get_id() % NUM_HANDLER_WRAPPERS;
	while(1) {
		wrapper = &handler_wrappers[wrapper_num];
		if (!commit_checklist_wait(wrapper->cpu_list, &cpu_mask))
			break;
		wrapper_num = (wrapper_num + 1) % NUM_HANDLER_WRAPPERS;
		/*
		uint32_t count = 0;
		// instead of deadlock, smp_call can fail with this.  makes it harder
		// to use (have to check your return value).  consider putting a delay
		// here too (like if wrapper_num == initial_wrapper_num)
		if (count++ > NUM_HANDLER_WRAPPERS * 1000) // note 1000 isn't enough...
			return E_BUSY;
		*/
	}

	// Wanting to wait is expressed by having a non-NULL handler_wrapper_t**
	// passed in.  Pass out our reference to wrapper, to wait later.
	// If we don't want to wait, release the checklist (though it is still not
	// clear, so it can't be used til everyone checks in).
	if (wait_wrapper)
		*wait_wrapper = wrapper;
	else {
		release_checklist(wrapper->cpu_list);
		atomic_dec(&outstanding_calls);
	}

	// now register our handler to run
	register_interrupt_handler(interrupt_handlers, wrapper->vector, handler);
	// WRITE MEMORY BARRIER HERE
	enable_irqsave(&state);
	// Send the proper type of IPI.  I made up these numbers.
	switch (type) {
		case 1:
			send_self_ipi(wrapper->vector);
			break;
		case 2:
			send_broadcast_ipi(wrapper->vector);
			break;
		case 3:
			send_all_others_ipi(wrapper->vector);
			break;
		case 4: // physical mode
			send_ipi(dest, 0, wrapper->vector);
			break;
		case 5: // logical mode
			send_ipi(dest, 1, wrapper->vector);
			break;
		default:
			panic("Invalid type for cross-core function call!");
	}
	// wait long enough to receive our own broadcast (PROBABLY WORKS) TODO
	lapic_wait_to_send();
	disable_irqsave(&state);
	return 0;
}

// I'd rather have these functions take an arbitrary function and arguments...
// Right now, I build a handler that just calls whatever I want, which is
// another layer of indirection.
int smp_call_function_self(isr_t handler, handler_wrapper_t** wait_wrapper)
{
	return smp_call_function(1, 0, handler, wait_wrapper);
}

int smp_call_function_all(isr_t handler, handler_wrapper_t** wait_wrapper)
{
	return smp_call_function(2, 0, handler, wait_wrapper);
}

int smp_call_function_single(uint8_t dest, isr_t handler,
                             handler_wrapper_t** wait_wrapper)
{
	return smp_call_function(4, dest, handler, wait_wrapper);
}

// If you want to wait, pass the address of a pointer up above, then call
// this to do the actual waiting
int smp_call_wait(handler_wrapper_t* wrapper)
{
	if (wrapper) {
		waiton_checklist(wrapper->cpu_list);
		return 0;
	} else {
		warn("Attempting to wait on null wrapper!  Check your return values!");
		return E_FAIL;
	}
}

