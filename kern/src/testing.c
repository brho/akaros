
#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/mmu.h>
#include <arch/arch.h>
#include <bitmask.h>
#include <smp.h>

#include <ros/memlayout.h>
#include <ros/common.h>
#include <ros/bcq.h>

#include <atomic.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <testing.h>
#include <trap.h>
#include <arch/trap.h>
#include <process.h>
#include <syscall.h>
#include <timing.h>
#include <kfs.h>
#include <multiboot.h>
#include <pmap.h>
#include <page_alloc.h>
#include <pmap.h>
#include <slab.h>
#include <kmalloc.h>
#include <hashtable.h>
#include <radix.h>
#include <monitor.h>
#include <kthread.h>

#define l1 (available_caches.l1)
#define l2 (available_caches.l2)
#define l3 (available_caches.l3)

#ifdef __i386__

void test_ipi_sending(void)
{
	extern handler_t (CT(NUM_INTERRUPT_HANDLERS) RO interrupt_handlers)[];
	int8_t state = 0;

	register_interrupt_handler(interrupt_handlers, I_TESTING,
	                           test_hello_world_handler, NULL);
	enable_irqsave(&state);
	cprintf("\nCORE 0 sending broadcast\n");
	send_broadcast_ipi(I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending all others\n");
	send_all_others_ipi(I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending self\n");
	send_self_ipi(I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 1\n");
	send_ipi(get_hw_coreid(0x01), I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 2\n");
	send_ipi(get_hw_coreid(0x02), I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 3\n");
	send_ipi(get_hw_coreid(0x03), I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 15\n");
	send_ipi(get_hw_coreid(0x0f), I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to logical 2\n");
	send_group_ipi(0x02, I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to logical 1\n");
	send_group_ipi(0x01, I_TESTING);
	udelay(3000000);
	cprintf("\nDone!\n");
	disable_irqsave(&state);
}

// Note this never returns and will muck with any other timer work
void test_pic_reception(void)
{
	register_interrupt_handler(interrupt_handlers, 0x20, test_hello_world_handler, NULL);
	pit_set_timer(100,TIMER_RATEGEN); // totally arbitrary time
	pic_unmask_irq(0);
	cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
	cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	cprintf("Core %d's LINT0: 0x%08x\n", core_id(), read_mmreg32(LAPIC_LVT_LINT0));
	enable_irq();
	while(1);
}

void test_ioapic_pit_reroute(void) 
{
	register_interrupt_handler(interrupt_handlers, 0x20, test_hello_world_handler, NULL);
	ioapic_route_irq(0, 3);	

	cprintf("Starting pit on core 3....\n");
	udelay(3000000);
	pit_set_timer(0xFFFE,TIMER_RATEGEN); // totally arbitrary time
	
	udelay(3000000);
	ioapic_unroute_irq(0);
	udelay(300000);
	cprintf("Masked pit. Waiting before return...\n");
	udelay(3000000);
}

#endif // __i386__


void test_print_info(void)
{
	cprintf("\nCORE 0 asking all cores to print info:\n");
	smp_call_function_all(test_print_info_handler, NULL, 0);
	cprintf("\nDone!\n");
}

void test_page_coloring(void) 
{
/*
	//Print the different cache properties of our machine
	print_cache_properties("L1", l1);
	cprintf("\n");
	print_cache_properties("L2", l2);
	cprintf("\n");
	print_cache_properties("L3", l3);
	cprintf("\n");

	//Print some stats about our memory
	cprintf("Max Address: %llu\n", MAX_VADDR);
	cprintf("Num Pages: %u\n", npages);

	//Declare a local variable for allocating pages	
	page_t* page;

	cprintf("Contents of the page free list:\n");
	for(int i=0; i<llc_cache->num_colors; i++) {
		cprintf("  COLOR %d:\n", i);
		LIST_FOREACH(page, &colored_page_free_list[i], pg_link) {
			cprintf("    Page: %d\n", page2ppn(page));
		}
	}

	//Run through and allocate all pages through l1_page_alloc
	cprintf("Allocating from L1 page colors:\n");
	for(int i=0; i<get_cache_num_page_colors(l1); i++) {
		cprintf("  COLOR %d:\n", i);
		while(colored_page_alloc(l1, &page, i) != -ENOMEM)
			cprintf("    Page: %d\n", page2ppn(page));
	}

	//Put all the pages back by reinitializing
	page_init();
	
	//Run through and allocate all pages through l2_page_alloc
	cprintf("Allocating from L2 page colors:\n");
	for(int i=0; i<get_cache_num_page_colors(l2); i++) {
		cprintf("  COLOR %d:\n", i);
		while(colored_page_alloc(l2, &page, i) != -ENOMEM)
			cprintf("    Page: %d\n", page2ppn(page));
	}

	//Put all the pages back by reinitializing
	page_init();
	
	//Run through and allocate all pages through l3_page_alloc
	cprintf("Allocating from L3 page colors:\n");
	for(int i=0; i<get_cache_num_page_colors(l3); i++) {
		cprintf("  COLOR %d:\n", i);
		while(colored_page_alloc(l3, &page, i) != -ENOMEM)
			cprintf("    Page: %d\n", page2ppn(page));
	}
	
	//Put all the pages back by reinitializing
	page_init();
	
	//Run through and allocate all pages through page_alloc
	cprintf("Allocating from global allocator:\n");
	while(upage_alloc(&page) != -ENOMEM)
		cprintf("    Page: %d\n", page2ppn(page));
	
	if(colored_page_alloc(l2, &page, 0) != -ENOMEM)
		cprintf("Should not get here, all pages should already be gone!\n");
	cprintf("All pages gone for sure...\n");
	
	//Now lets put a few pages back using page_free..
	cprintf("Reinserting pages via page_free and reallocating them...\n");
	page_free(&pages[0]);
	page_free(&pages[15]);
	page_free(&pages[7]);
	page_free(&pages[6]);
	page_free(&pages[4]);

	while(upage_alloc(&page) != -ENOMEM)
		cprintf("Page: %d\n", page2ppn(page));	
	
	page_init();
*/
}

void test_color_alloc() {
	size_t checkpoint = 0;
	uint8_t* colors_map = kmalloc(BYTES_FOR_BITMASK(llc_cache->num_colors), 0);
	cache_color_alloc(l2, colors_map);
	cache_color_alloc(l3, colors_map);
	cache_color_alloc(l3, colors_map);
	cache_color_alloc(l2, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(l2, colors_map);
	cache_color_free(llc_cache, colors_map);
	cache_color_free(llc_cache, colors_map);

print_cache_colors:
	printk("L1 free colors, tot colors: %d\n", l1->num_colors);
	PRINT_BITMASK(l1->free_colors_map, l1->num_colors);
	printk("L2 free colors, tot colors: %d\n", l2->num_colors);
	PRINT_BITMASK(l2->free_colors_map, l2->num_colors);
	printk("L3 free colors, tot colors: %d\n", l3->num_colors);
	PRINT_BITMASK(l3->free_colors_map, l3->num_colors);
	printk("Process allocated colors\n");
	PRINT_BITMASK(colors_map, llc_cache->num_colors);
	printk("test_color_alloc() complete!\n");
}

barrier_t test_cpu_array;

void test_barrier(void)
{
	cprintf("Core 0 initializing barrier\n");
	init_barrier(&test_cpu_array, num_cpus);
	cprintf("Core 0 asking all cores to print ids, barrier, rinse, repeat\n");
	smp_call_function_all(test_barrier_handler, NULL, 0);
}

void test_interrupts_irqsave(void)
{
	int8_t state = 0;
	printd("Testing Nesting Enabling first, turning ints off:\n");
	disable_irq();
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Enabling IRQSave\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Done.  Should have been 0, 200, 200, 200, 0\n");

	printd("Testing Nesting Disabling first, turning ints on:\n");
	state = 0;
	enable_irq();
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Enabling IRQSave Once\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Done.  Should have been 200, 0, 0, 0, 200 \n");

	state = 0;
	disable_irq();
	printd("Ints are off, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Done.  Should have been 200, 0\n");

	state = 0;
	enable_irq();
	printd("Ints are on, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Done.  Should have been 200, 200\n");

	state = 0;
	disable_irq();
	printd("Ints are off, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	printd("Done.  Should have been 0, 0\n");

	state = 0;
	enable_irq();
	printd("Ints are on, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(!irq_is_enabled());
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	assert(irq_is_enabled());
	printd("Done.  Should have been 0, 200\n");

	disable_irq();
	cprintf("Passed enable_irqsave tests\n");
}

void test_bitmasks(void)
{
#define masksize 67
	DECL_BITMASK(mask, masksize);
	printk("size of mask %d\n", sizeof(mask));
	CLR_BITMASK(mask, masksize);
	PRINT_BITMASK(mask, masksize);
	printk("cleared\n");
	SET_BITMASK_BIT(mask, 0);
	SET_BITMASK_BIT(mask, 11);
	SET_BITMASK_BIT(mask, 17);
	SET_BITMASK_BIT(mask, masksize-1);
	printk("bits set\n");
	PRINT_BITMASK(mask, masksize);
	DECL_BITMASK(mask2, masksize);
	COPY_BITMASK(mask2, mask, masksize);
	printk("copy of original mask, should be the same as the prev\n");
	PRINT_BITMASK(mask2, masksize);
	CLR_BITMASK_BIT(mask, 11);
	printk("11 cleared\n");
	PRINT_BITMASK(mask, masksize);
	printk("bit 17 is %d (should be 1)\n", GET_BITMASK_BIT(mask, 17));
	printk("bit 11 is %d (should be 0)\n", GET_BITMASK_BIT(mask, 11));
	FILL_BITMASK(mask, masksize);
	PRINT_BITMASK(mask, masksize);
	printk("should be all 1's, except for a few at the end\n");
	printk("Is Clear?: %d (should be 0)\n", BITMASK_IS_CLEAR(mask,masksize));
	CLR_BITMASK(mask, masksize);
	PRINT_BITMASK(mask, masksize);
	printk("Is Clear?: %d (should be 1)\n", BITMASK_IS_CLEAR(mask,masksize));
	printk("should be cleared\n");
}

checklist_t *RO the_global_list;

void test_checklist_handler(trapframe_t *tf, void* data)
{
	udelay(1000000);
	cprintf("down_checklist(%x,%d)\n", the_global_list, core_id());
	down_checklist(the_global_list);
}

void test_checklists(void)
{
	INIT_CHECKLIST(a_list, MAX_NUM_CPUS);
	the_global_list = &a_list;
	printk("Checklist Build, mask size: %d\n", sizeof(a_list.mask.bits));
	printk("mask\n");
	PRINT_BITMASK(a_list.mask.bits, a_list.mask.size);
	SET_BITMASK_BIT(a_list.mask.bits, 11);
	printk("Set bit 11\n");
	PRINT_BITMASK(a_list.mask.bits, a_list.mask.size);

	CLR_BITMASK(a_list.mask.bits, a_list.mask.size);
	INIT_CHECKLIST_MASK(a_mask, MAX_NUM_CPUS);
	FILL_BITMASK(a_mask.bits, num_cpus);
	//CLR_BITMASK_BIT(a_mask.bits, core_id());
	//SET_BITMASK_BIT(a_mask.bits, 1);
	//printk("New mask (1, 17, 25):\n");
	printk("Created new mask, filled up to num_cpus\n");
	PRINT_BITMASK(a_mask.bits, a_mask.size);
	printk("committing new mask\n");
	commit_checklist_wait(&a_list, &a_mask);
	printk("Old mask (copied onto):\n");
	PRINT_BITMASK(a_list.mask.bits, a_list.mask.size);
	//smp_call_function_single(1, test_checklist_handler, 0, 0);

	smp_call_function_all(test_checklist_handler, NULL, 0);

	printk("Waiting on checklist\n");
	waiton_checklist(&a_list);
	printk("Done Waiting!\n");

}

atomic_t a, b, c;

#ifdef __IVY__
void test_incrementer_handler(trapframe_t *tf, atomic_t *data)
#else
void test_incrementer_handler(trapframe_t *tf, void *data)
#endif
{
	assert(data);
	atomic_inc(data);
}

void test_null_handler(trapframe_t *tf, void* data)
{
	asm volatile("nop");
}

void test_smp_call_functions(void)
{
	int i;
	atomic_init(&a, 0);
	atomic_init(&b, 0);
	atomic_init(&c, 0);
	handler_wrapper_t *waiter0 = 0, *waiter1 = 0, *waiter2 = 0, *waiter3 = 0,
	                  *waiter4 = 0, *waiter5 = 0;
	uint8_t me = core_id();
	printk("\nCore %d: SMP Call Self (nowait):\n", me);
	printk("---------------------\n");
	smp_call_function_self(test_hello_world_handler, NULL, 0);
	printk("\nCore %d: SMP Call Self (wait):\n", me);
	printk("---------------------\n");
	smp_call_function_self(test_hello_world_handler, NULL, &waiter0);
	smp_call_wait(waiter0);
	printk("\nCore %d: SMP Call All (nowait):\n", me);
	printk("---------------------\n");
	smp_call_function_all(test_hello_world_handler, NULL, 0);
	printk("\nCore %d: SMP Call All (wait):\n", me);
	printk("---------------------\n");
	smp_call_function_all(test_hello_world_handler, NULL, &waiter0);
	smp_call_wait(waiter0);
	printk("\nCore %d: SMP Call All-Else Individually, in order (nowait):\n", me);
	printk("---------------------\n");
	for(i = 1; i < num_cpus; i++)
		smp_call_function_single(i, test_hello_world_handler, NULL, 0);
	printk("\nCore %d: SMP Call Self (wait):\n", me);
	printk("---------------------\n");
	smp_call_function_self(test_hello_world_handler, NULL, &waiter0);
	smp_call_wait(waiter0);
	printk("\nCore %d: SMP Call All-Else Individually, in order (wait):\n", me);
	printk("---------------------\n");
	for(i = 1; i < num_cpus; i++)
	{
		smp_call_function_single(i, test_hello_world_handler, NULL, &waiter0);
		smp_call_wait(waiter0);
	}
	printk("\nTesting to see if any IPI-functions are dropped when not waiting:\n");
	printk("A: %d, B: %d, C: %d (should be 0,0,0)\n", atomic_read(&a), atomic_read(&b), atomic_read(&c));
	smp_call_function_all(test_incrementer_handler, &a, 0);
	smp_call_function_all(test_incrementer_handler, &b, 0);
	smp_call_function_all(test_incrementer_handler, &c, 0);
	// if i can clobber a previous IPI, the interleaving might do it
	smp_call_function_single(1 % num_cpus, test_incrementer_handler, &a, 0);
	smp_call_function_single(2 % num_cpus, test_incrementer_handler, &b, 0);
	smp_call_function_single(3 % num_cpus, test_incrementer_handler, &c, 0);
	smp_call_function_single(4 % num_cpus, test_incrementer_handler, &a, 0);
	smp_call_function_single(5 % num_cpus, test_incrementer_handler, &b, 0);
	smp_call_function_single(6 % num_cpus, test_incrementer_handler, &c, 0);
	smp_call_function_all(test_incrementer_handler, &a, 0);
	smp_call_function_single(3 % num_cpus, test_incrementer_handler, &c, 0);
	smp_call_function_all(test_incrementer_handler, &b, 0);
	smp_call_function_single(1 % num_cpus, test_incrementer_handler, &a, 0);
	smp_call_function_all(test_incrementer_handler, &c, 0);
	smp_call_function_single(2 % num_cpus, test_incrementer_handler, &b, 0);
	// wait, so we're sure the others finish before printing.
	// without this, we could (and did) get 19,18,19, since the B_inc
	// handler didn't finish yet
	smp_call_function_self(test_null_handler, NULL, &waiter0);
	// need to grab all 5 handlers (max), since the code moves to the next free.
	smp_call_function_self(test_null_handler, NULL, &waiter1);
	smp_call_function_self(test_null_handler, NULL, &waiter2);
	smp_call_function_self(test_null_handler, NULL, &waiter3);
	smp_call_function_self(test_null_handler, NULL, &waiter4);
	smp_call_wait(waiter0);
	smp_call_wait(waiter1);
	smp_call_wait(waiter2);
	smp_call_wait(waiter3);
	smp_call_wait(waiter4);
	printk("A: %d, B: %d, C: %d (should be 19,19,19)\n", atomic_read(&a), atomic_read(&b), atomic_read(&c));
	printk("Attempting to deadlock by smp_calling with an outstanding wait:\n");
	smp_call_function_self(test_null_handler, NULL, &waiter0);
	printk("Sent one\n");
	smp_call_function_self(test_null_handler, NULL, &waiter1);
	printk("Sent two\n");
	smp_call_wait(waiter0);
	printk("Wait one\n");
	smp_call_wait(waiter1);
	printk("Wait two\n");
	printk("\tMade it through!\n");
	printk("Attempting to deadlock by smp_calling more than are available:\n");
	printk("\tShould see an Insufficient message and a kernel warning.\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter0))
		printk("\tInsufficient handlers to call function (0)\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter1))
		printk("\tInsufficient handlers to call function (1)\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter2))
		printk("\tInsufficient handlers to call function (2)\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter3))
		printk("\tInsufficient handlers to call function (3)\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter4))
		printk("\tInsufficient handlers to call function (4)\n");
	if (smp_call_function_self(test_null_handler, NULL, &waiter5))
		printk("\tInsufficient handlers to call function (5)\n");
	smp_call_wait(waiter0);
	smp_call_wait(waiter1);
	smp_call_wait(waiter2);
	smp_call_wait(waiter3);
	smp_call_wait(waiter4);
	smp_call_wait(waiter5);
	printk("\tMade it through!\n");

	printk("Done\n");
}

#ifdef __i386__
void test_lapic_status_bit(void)
{
	register_interrupt_handler(interrupt_handlers, I_TESTING,
	                           test_incrementer_handler, &a);
	#define NUM_IPI 100000
	atomic_set(&a,0);
	printk("IPIs received (should be 0): %d\n", a);
	for(int i = 0; i < NUM_IPI; i++) {
		send_ipi(get_hw_coreid(7), I_TESTING);
		lapic_wait_to_send();
	}
	// need to wait a bit to let those IPIs get there
	udelay(5000000);
	printk("IPIs received (should be %d): %d\n", a, NUM_IPI);
	// hopefully that handler never fires again.  leaving it registered for now.
}
#endif // __i386__

/************************************************************/
/* ISR Handler Functions */

void test_hello_world_handler(trapframe_t *tf, void* data)
{
	int trapno;
	#if defined(__i386__)
	trapno = tf->tf_trapno;
	#elif defined(__sparc_v8__)
	trapno = (tf->tbr >> 4) & 0xFF;
	#else
	trapno = 0;
	#endif

	cprintf("Incoming IRQ, ISR: %d on core %d with tf at 0x%08x\n",
	        trapno, core_id(), tf);
}

spinlock_t print_info_lock = SPINLOCK_INITIALIZER;

void test_print_info_handler(trapframe_t *tf, void* data)
{
	uint64_t tsc = read_tsc();

	spin_lock_irqsave(&print_info_lock);
	cprintf("----------------------------\n");
	cprintf("This is Core %d\n", core_id());
	cprintf("Timestamp = %lld\n", tsc);
#ifdef __i386__
	cprintf("Hardware core %d\n", hw_core_id());
	cprintf("MTRR_DEF_TYPE = 0x%08x\n", read_msr(IA32_MTRR_DEF_TYPE));
	cprintf("MTRR Phys0 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x200), read_msr(0x201));
	cprintf("MTRR Phys1 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x202), read_msr(0x203));
	cprintf("MTRR Phys2 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x204), read_msr(0x205));
	cprintf("MTRR Phys3 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x206), read_msr(0x207));
	cprintf("MTRR Phys4 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x208), read_msr(0x209));
	cprintf("MTRR Phys5 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x20a), read_msr(0x20b));
	cprintf("MTRR Phys6 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x20c), read_msr(0x20d));
	cprintf("MTRR Phys7 Base = 0x%016llx, Mask = 0x%016llx\n",
	        read_msr(0x20e), read_msr(0x20f));
#endif // __i386__
	cprintf("----------------------------\n");
	spin_unlock_irqsave(&print_info_lock);
}

void test_barrier_handler(trapframe_t *tf, void* data)
{
	cprintf("Round 1: Core %d\n", core_id());
	waiton_barrier(&test_cpu_array);
	waiton_barrier(&test_cpu_array);
	waiton_barrier(&test_cpu_array);
	waiton_barrier(&test_cpu_array);
	waiton_barrier(&test_cpu_array);
	waiton_barrier(&test_cpu_array);
	cprintf("Round 2: Core %d\n", core_id());
	waiton_barrier(&test_cpu_array);
	cprintf("Round 3: Core %d\n", core_id());
	// uncomment to see it fucked up
	//cprintf("Round 4: Core %d\n", core_id());
}

#ifdef __IVY__
static void test_waiting_handler(trapframe_t *tf, atomic_t *data)
#else
static void test_waiting_handler(trapframe_t *tf, void *data)
#endif
{
	atomic_dec(data);
}

#ifdef __i386__
void test_pit(void)
{
	cprintf("Starting test for PIT now (10s)\n");
	udelay_pit(10000000);
	cprintf("End now\n");
	cprintf("Starting test for TSC (if stable) now (10s)\n");
	udelay(10000000);
	cprintf("End now\n");

	cprintf("Starting test for LAPIC (if stable) now (10s)\n");
	enable_irq();
	lapic_set_timer(10000000, FALSE);

	atomic_t waiting;
	atomic_init(&waiting, 1);
	register_interrupt_handler(interrupt_handlers, I_TESTING,
	                           test_waiting_handler, &waiting);
	while(atomic_read(&waiting))
		cpu_relax();
	cprintf("End now\n");
}

void test_circ_buffer(void)
{
	int arr[5] = {0, 1, 2, 3, 4};

	for (int i = 0; i < 5; i++) {
		FOR_CIRC_BUFFER(i, 5, j)
			printk("Starting with current = %d, each value = %d\n", i, j);
	}
	return;
}

void test_km_handler(trapframe_t* tf, uint32_t srcid, void *a0, void *a1,
                     void *a2)
{
	printk("Received KM on core %d from core %d: arg0= 0x%08x, arg1 = "
	       "0x%08x, arg2 = 0x%08x\n", core_id(), srcid, a0, a1, a2);
	return;
}

void test_kernel_messages(void)
{
	printk("Testing Kernel Messages\n");
	/* Testing sending multiples, sending different types, alternating, and
	 * precendence (the immediates should trump the others) */
	printk("sending 5 IMMED to core 1, sending (#,deadbeef,0)\n");
	for (int i = 0; i < 5; i++)
		send_kernel_message(1, test_km_handler, (void*)i, (void*)0xdeadbeef,
		                    (void*)0, KMSG_IMMEDIATE);
	udelay(5000000);
	printk("sending 5 routine to core 1, sending (#,cafebabe,0)\n");
	for (int i = 0; i < 5; i++)
		send_kernel_message(1, test_km_handler, (void*)i, (void*)0xcafebabe,
		                    (void*)0, KMSG_ROUTINE);
	udelay(5000000);
	printk("sending 10 routine and 3 immediate to core 2\n");
	for (int i = 0; i < 10; i++)
		send_kernel_message(2, test_km_handler, (void*)i, (void*)0xcafebabe,
		                    (void*)0, KMSG_ROUTINE);
	for (int i = 0; i < 3; i++)
		send_kernel_message(2, test_km_handler, (void*)i, (void*)0xdeadbeef,
		                    (void*)0, KMSG_IMMEDIATE);
	udelay(5000000);
	printk("sending 5 ea alternating to core 2\n");
	for (int i = 0; i < 5; i++) {
		send_kernel_message(2, test_km_handler, (void*)i, (void*)0xdeadbeef,
		                    (void*)0, KMSG_IMMEDIATE);
		send_kernel_message(2, test_km_handler, (void*)i, (void*)0xcafebabe,
		                    (void*)0, KMSG_ROUTINE);
	}
	udelay(5000000);
	return;
}
#endif // __i386__

static void test_single_cache(int iters, size_t size, int align, int flags,
                              void (*ctor)(void *, size_t),
                              void (*dtor)(void *, size_t))
{
	struct kmem_cache *test_cache;
	void *objects[iters];
	test_cache = kmem_cache_create("test_cache", size, align, flags, ctor, dtor);
	printk("Testing Kmem Cache:\n");
	print_kmem_cache(test_cache);
	for (int i = 0; i < iters; i++) {
		objects[i] = kmem_cache_alloc(test_cache, 0);
		printk("Buffer %d addr = %p\n", i, objects[i]);
	}
	for (int i = 0; i < iters; i++) {
		kmem_cache_free(test_cache, objects[i]);
	}
	kmem_cache_destroy(test_cache);
	printk("\n\n\n\n");
}

void a_ctor(void *buf, size_t size)
{
	printk("constructin tests\n");
}
void a_dtor(void *buf, size_t size)
{
	printk("destructin tests\n");
}

void test_slab(void)
{
	test_single_cache(10, 128, 512, 0, 0, 0);
	test_single_cache(10, 128, 4, 0, a_ctor, a_dtor);
	test_single_cache(10, 1024, 16, 0, 0, 0);
}

void test_kmalloc(void)
{
	printk("Testing Kmalloc\n");
	void *bufs[NUM_KMALLOC_CACHES + 1];	
	size_t size;
	for (int i = 0; i < NUM_KMALLOC_CACHES + 1; i++){
		size = (KMALLOC_SMALLEST << i) - KMALLOC_OFFSET;
		bufs[i] = kmalloc(size, 0);
		printk("Size %d, Addr = %p\n", size, bufs[i]);
	}
	for (int i = 0; i < NUM_KMALLOC_CACHES; i++) {
		printk("Freeing buffer %d\n", i);
		kfree(bufs[i]);
	}
	printk("Testing a large kmalloc\n");
	size = (KMALLOC_LARGEST << 2);
	bufs[0] = kmalloc(size, 0);
	printk("Size %d, Addr = %p\n", size, bufs[0]);
	kfree(bufs[0]);
}

static size_t test_hash_fn_col(void *k)
{
	return (size_t)k % 2; // collisions in slots 0 and 1
}

void test_hashtable(void)
{
	struct test {int x; int y;};
	struct test tstruct[10];

	struct hashtable *h;
	int k = 5;
	struct test *v = &tstruct[0];

	h = create_hashtable(32, __generic_hash, __generic_eq);
	
	// test inserting one item, then finding it again
	printk("Tesing one item, insert, search, and removal\n");
	if(!hashtable_insert(h, (void*)k, v))
		printk("Failed to insert to hashtable!\n");
	v = NULL;
	if (!(v = hashtable_search(h, (void*)k)))
		printk("Failed to find in hashtable!\n");
	if (v != &tstruct[0])
		printk("Got the wrong item! (got %p, wanted %p)\n", v, &tstruct[0]);
	v = NULL;
	if (!(v = hashtable_remove(h, (void*)k)))
		printk("Failed to remove from hashtable!\n");
	// shouldn't be able to find it again
	if ((v = hashtable_search(h, (void*)k)))
		printk("Should not have been able to find in hashtable!\n");
	
	printk("Tesing a bunch of items, insert, search, and removal\n");
	for (int i = 0; i < 10; i++) {
		k = i; // vary the key, we don't do KEY collisions
		if(!hashtable_insert(h, (void*)k, &tstruct[i]))
			printk("Failed to insert iter %d to hashtable!\n", i);
	}
	// read out the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		if (!(v = hashtable_search(h, (void*)k)))
			printk("Failed to find in hashtable!\n");
		if (v != &tstruct[i])
			printk("Got the wrong item! (got %p, wanted %p)\n", v, &tstruct[i]);
	}
	if (hashtable_count(h) != 10)
		printk("Wrong accounting of number of elements!\n");
	// remove the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		if (!(v = hashtable_remove(h, (void*)k)))
			printk("Failed to remove from hashtable!\n");
	}
	// make sure they are all gone
	for (int i = 0; i < 10; i++) {
		k = i;
		if ((v = hashtable_search(h, (void*)k)))
			printk("Should not have been able to find in hashtable!\n");
	}
	if (hashtable_count(h))
		printk("Wrong accounting of number of elements!\n");
	hashtable_destroy(h);

	// same test of a bunch of items, but with collisions.
	printk("Tesing a bunch of items with collisions, etc.\n");
	h = create_hashtable(32, test_hash_fn_col, __generic_eq);
	// insert 10 items
	for (int i = 0; i < 10; i++) {
		k = i; // vary the key, we don't do KEY collisions
		if(!hashtable_insert(h, (void*)k, &tstruct[i]))
			printk("Failed to insert iter %d to hashtable!\n", i);
	}
	// read out the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		if (!(v = hashtable_search(h, (void*)k)))
			printk("Failed to find in hashtable!\n");
		if (v != &tstruct[i])
			printk("Got the wrong item! (got %p, wanted %p)\n", v, &tstruct[i]);
	}
	if (hashtable_count(h) != 10)
		printk("Wrong accounting of number of elements!\n");
	// remove the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		if (!(v = hashtable_remove(h, (void*)k)))
			printk("Failed to remove from hashtable!\n");
	}
	// make sure they are all gone
	for (int i = 0; i < 10; i++) {
		k = i;
		if ((v = hashtable_search(h, (void*)k)))
			printk("Should not have been able to find in hashtable!\n");
	}
	if (hashtable_count(h))
		printk("Wrong accounting of number of elements!\n");
	hashtable_destroy(h);
}

/* Ghetto test, only tests one prod or consumer at a time */
void test_bcq(void)
{
	/* Tests a basic struct */
	struct my_struct {
		int x;
		int y;
	};
	struct my_struct in_struct, out_struct;
	
	DEFINE_BCQ_TYPES(test, struct my_struct, 16);
	struct test_bcq t_bcq;
	bcq_init(&t_bcq, struct my_struct, 16);
	
	in_struct.x = 4;
	in_struct.y = 5;
	out_struct.x = 1;
	out_struct.y = 2;
	
	bcq_enqueue(&t_bcq, &in_struct, 16, 5);
	bcq_dequeue(&t_bcq, &out_struct, 16);
	printk("out x %d. out y %d\n", out_struct.x, out_struct.y);
	
	/* Tests the BCQ a bit more, esp with overflow */
	#define NR_ELEM_A_BCQ 8 /* NOTE: this must be a power of 2! */
	DEFINE_BCQ_TYPES(my, int, NR_ELEM_A_BCQ);
	struct my_bcq a_bcq;
	bcq_init(&a_bcq, int, NR_ELEM_A_BCQ);
	
	int y = 2;
	int output[100];
	int retval[100];

	/* Helpful debugger */
	void print_a_bcq(struct my_bcq *bcq)
	{
		printk("A BCQ (made of ints): %08p\n", bcq);
		printk("\tprod_idx: %08p\n", bcq->hdr.prod_idx);
		printk("\tcons_pub_idx: %08p\n", bcq->hdr.cons_pub_idx);
		printk("\tcons_pvt_idx: %08p\n", bcq->hdr.cons_pvt_idx);
		for (int i = 0; i < NR_ELEM_A_BCQ; i++) {
			printk("Element %d, rdy_for_cons: %02p\n", i,
			       bcq->wraps[i].rdy_for_cons);
		}
	}

	/* Put in more than it can take */
	for (int i = 0; i < 15; i++) {
		y = i;
		retval[i] = bcq_enqueue(&a_bcq, &y, NR_ELEM_A_BCQ, 10);
		printk("enqueued: %d, had retval %d \n", y, retval[i]);
	}
	//print_a_bcq(&a_bcq);
	
	/* Try to dequeue more than we put in */
	for (int i = 0; i < 15; i++) {
		retval[i] = bcq_dequeue(&a_bcq, &output[i], NR_ELEM_A_BCQ);
		printk("dequeued: %d with retval %d\n", output[i], retval[i]);
	}
	//print_a_bcq(&a_bcq);
	
	/* Put in some it should be able to take */
	for (int i = 0; i < 3; i++) {
		y = i;
		retval[i] = bcq_enqueue(&a_bcq, &y, NR_ELEM_A_BCQ, 10);
		printk("enqueued: %d, had retval %d \n", y, retval[i]);
	}
	
	/* Take those, and then a couple extra */
	for (int i = 0; i < 5; i++) {
		retval[i] = bcq_dequeue(&a_bcq, &output[i], NR_ELEM_A_BCQ);
		printk("dequeued: %d with retval %d\n", output[i], retval[i]);
	}
	
	/* Try some one-for-one */
	for (int i = 0; i < 5; i++) {
		y = i;
		retval[i] = bcq_enqueue(&a_bcq, &y, NR_ELEM_A_BCQ, 10);
		printk("enqueued: %d, had retval %d \n", y, retval[i]);
		retval[i] = bcq_dequeue(&a_bcq, &output[i], NR_ELEM_A_BCQ);
		printk("dequeued: %d with retval %d\n", output[i], retval[i]);
	}
}

/* rudimentary tests.  does the basics, create, merge, split, etc.  Feel free to
 * add more, esp for the error conditions and finding free slots.  This is also
 * a bit lazy with setting the caller's fields (perm, flags, etc). */
void test_vm_regions(void)
{
	#define MAX_VMR_TESTS 10
	struct proc pr, *p = &pr;	/* too lazy to even create one */
	int n = 0;
	TAILQ_INIT(&p->vm_regions);

	struct vmr_summary {
		uintptr_t base; 
		uintptr_t end; 
	};
	int check_vmrs(struct proc *p, struct vmr_summary *results, int len, int n)
	{
		int count = 0;
		struct vm_region *vmr;
		TAILQ_FOREACH(vmr, &p->vm_regions, vm_link) {
			if (count >= len) {
				printk("More vm_regions than expected\n");
				break;
			}
			if ((vmr->vm_base != results[count].base) ||
			    (vmr->vm_end != results[count].end)) {
				printk("VM test case %d failed!\n", n);
				print_vmrs(p);
				return -1;
			}
			count++;
		}
		return count;
	}
	struct vm_region *vmrs[MAX_VMR_TESTS];
	struct vmr_summary results[MAX_VMR_TESTS];

	memset(results, 0, sizeof(results));
	/* Make one */
	vmrs[0] = create_vmr(p, 0x2000, 0x1000);
	results[0].base = 0x2000;
	results[0].end = 0x3000;
	check_vmrs(p, results, 1, n++);
	/* Grow it */
	grow_vmr(vmrs[0], 0x4000);
	results[0].base = 0x2000;
	results[0].end = 0x4000;
	check_vmrs(p, results, 1, n++);
	/* Grow it poorly */
	if (-1 != grow_vmr(vmrs[0], 0x3000))
		printk("Bad grow test failed\n");
	check_vmrs(p, results, 1, n++);
	/* Make another right next to it */
	vmrs[1] = create_vmr(p, 0x4000, 0x1000);
	results[1].base = 0x4000;
	results[1].end = 0x5000;
	check_vmrs(p, results, 2, n++);
	/* try to grow through it */
	if (-1 != grow_vmr(vmrs[0], 0x5000))
		printk("Bad grow test failed\n");
	check_vmrs(p, results, 2, n++);
	/* Merge them */
	merge_vmr(vmrs[0], vmrs[1]);
	results[0].end = 0x5000;
	results[1].base = 0;
	results[1].end = 0;
	check_vmrs(p, results, 1, n++);
	vmrs[1]= create_vmr(p, 0x6000, 0x4000);
	results[1].base = 0x6000;
	results[1].end = 0xa000;
	check_vmrs(p, results, 2, n++);
	/* try to merge unmergables (just testing ranges) */
	if (-1 != merge_vmr(vmrs[0], vmrs[1]))
		printk("Bad merge test failed\n");
	check_vmrs(p, results, 2, n++);
	vmrs[2] = split_vmr(vmrs[1], 0x8000);
	results[1].end = 0x8000;
	results[2].base = 0x8000;
	results[2].end = 0xa000;
	check_vmrs(p, results, 3, n++);
	/* destroy one */
	destroy_vmr(vmrs[1]);
	results[1].base = 0x8000;
	results[1].end = 0xa000;
	check_vmrs(p, results, 2, n++);
	/* shrink */
	shrink_vmr(vmrs[2], 0x9000);
	results[1].base = 0x8000;
	results[1].end = 0x9000;
	check_vmrs(p, results, 2, n++);	/* 10 */
	if (vmrs[2] != find_vmr(p, 0x8500))
		printk("Failed to find the right vmr!\n");
	if (vmrs[2] != find_first_vmr(p, 0x8500))
		printk("Failed to find the right vmr!\n");
	if (vmrs[2] != find_first_vmr(p, 0x7500))
		printk("Failed to find the right vmr!\n");
	if (find_first_vmr(p, 0x9500))
		printk("Found a vmr when we shouldn't!\n");
	/* grow up to another */
	grow_vmr(vmrs[0], 0x8000);
	results[0].end = 0x8000;
	check_vmrs(p, results, 2, n++);
	vmrs[0]->vm_prot = 88;
	vmrs[2]->vm_prot = 77;
	/* should be unmergeable due to perms */
	if (-1 != merge_vmr(vmrs[0], vmrs[2]))
		printk("Bad merge test failed\n");
	check_vmrs(p, results, 2, n++);
	/* should merge now */
	vmrs[2]->vm_prot = 88;
	merge_vmr(vmrs[0], vmrs[2]);
	results[0].end = 0x9000;
	check_vmrs(p, results, 1, n++);
	destroy_vmr(vmrs[0]);
	check_vmrs(p, results, 0, n++);
	/* Check the automerge function */
	vmrs[0] = create_vmr(p, 0x2000, 0x1000);
	vmrs[1] = create_vmr(p, 0x3000, 0x1000);
	vmrs[2] = create_vmr(p, 0x4000, 0x1000);
	for (int i = 0; i < 3; i++) {
		vmrs[i]->vm_prot = PROT_READ;
		vmrs[i]->vm_flags = 0;
		vmrs[i]->vm_file = 0; /* would like to test this, it's a pain for now */
	}
	vmrs[0] = merge_me(vmrs[1]);
	results[0].base = 0x2000;
	results[0].end = 0x5000;
	check_vmrs(p, results, 1, n++);
	destroy_vmr(vmrs[0]);
	check_vmrs(p, results, 0, n++);
	/* Check unfixed creation requests */
	vmrs[0] = create_vmr(p, 0x0000, 0x1000);
	vmrs[1] = create_vmr(p, 0x0000, 0x1000);
	vmrs[2] = create_vmr(p, 0x0000, 0x1000);
	results[0].base = 0x0000;
	results[0].end  = 0x1000;
	results[1].base = 0x1000;
	results[1].end  = 0x2000;
	results[2].base = 0x2000;
	results[2].end  = 0x3000;
	check_vmrs(p, results, 3, n++);

	printk("Finished vm_regions test!\n");
}

void test_radix_tree(void)
{
	struct radix_tree real_tree = RADIX_INITIALIZER;
	struct radix_tree *tree = &real_tree;
	void *retval;

	if (radix_insert(tree, 0, (void*)0xdeadbeef))
		printk("Failed to insert at 0!\n");
	radix_delete(tree, 0);
	if (radix_insert(tree, 0, (void*)0xdeadbeef))
		printk("Failed to re-insert at 0!\n");

	if (radix_insert(tree, 3, (void*)0xdeadbeef))
		printk("Failed to insert first!\n");
	radix_insert(tree, 4, (void*)0x04040404);
	assert((void*)0xdeadbeef == radix_lookup(tree, 3));
	for (int i = 5; i < 100; i++)
		if ((retval = radix_lookup(tree, i))) {
			printk("Extra item %08p at slot %d in tree %08p\n", retval, i,
			       tree);
			print_radix_tree(tree);
			monitor(0);
		}
	if (radix_insert(tree, 65, (void*)0xcafebabe))
		printk("Failed to insert a two-tier!\n");
	if (!radix_insert(tree, 4, (void*)0x03030303))
		printk("Should not let us reinsert\n");
	if (radix_insert(tree, 4095, (void*)0x4095))
		printk("Failed to insert a two-tier boundary!\n");
	if (radix_insert(tree, 4096, (void*)0x4096))
		printk("Failed to insert a three-tier!\n");
	//print_radix_tree(tree);
	radix_delete(tree, 65);
	radix_delete(tree, 3);
	radix_delete(tree, 4);
	radix_delete(tree, 4095);
	radix_delete(tree, 4096);
	//print_radix_tree(tree);
	printk("Finished radix tree tests!\n");
}

/* Assorted FS tests, which were hanging around in init.c */
void test_random_fs(void)
{
	int retval = do_symlink("/dir1/sym", "/bin/hello", S_IRWXU);
	if (retval)
		printk("symlink1 creation failed\n");
	retval = do_symlink("/symdir", "/dir1/dir1-1", S_IRWXU);
	if (retval)
		printk("symlink1 creation failed\n");
	retval = do_symlink("/dir1/test.txt", "/dir2/test2.txt", S_IRWXU);
	if (retval)
		printk("symlink2 creation failed\n");
	retval = do_symlink("/dir1/dir1-1/up", "../../", S_IRWXU);
	if (retval)
		printk("symlink3 creation failed\n");
	retval = do_symlink("/bin/hello-sym", "hello", S_IRWXU);
	if (retval)
		printk("symlink4 creation failed\n");
	
	struct dentry *dentry;
	struct nameidata nd_r = {0}, *nd = &nd_r;
	retval = path_lookup("/dir1/sym", 0, nd);
	if (retval)
		printk("symlink lookup failed: %d\n", retval);
	char *symname = nd->dentry->d_inode->i_op->readlink(nd->dentry);
	printk("Pathlookup got %s (sym)\n", nd->dentry->d_name.name);
	if (!symname)
		printk("symlink reading failed\n");
	else
		printk("Symname: %s (/bin/hello)\n", symname);
	path_release(nd);
	/* try with follow */
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/dir1/sym", LOOKUP_FOLLOW, nd);
	if (retval)
		printk("symlink lookup failed: %d\n", retval);
	printk("Pathlookup got %s (hello)\n", nd->dentry->d_name.name);
	path_release(nd);
	
	/* try with a directory */
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/f1-1.txt", 0, nd);
	if (retval)
		printk("symlink lookup failed: %d\n", retval);
	printk("Pathlookup got %s (f1-1.txt)\n", nd->dentry->d_name.name);
	path_release(nd);
	
	/* try with a rel path */
	printk("Try with a rel path\n");
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/up/hello.txt", 0, nd);
	if (retval)
		printk("symlink lookup failed: %d\n", retval);
	printk("Pathlookup got %s (hello.txt)\n", nd->dentry->d_name.name);
	path_release(nd);
	
	printk("Try for an ELOOP\n");
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/up/symdir/up/symdir/up/symdir/up/hello.txt", 0, nd);
	if (retval)
		printk("Symlink lookup failed (it should): %d (-40)\n", retval);
	path_release(nd);
}

/* simple test - start one, do something else, and resume it.  For lack of a
 * better infrastructure, we send ourselves a kmsg to run the kthread, which
 * we'll handle in smp_idle (which you may have to manually call).  Note this
 * doesn't test things like memory being leaked, or dealing with processes. */
void test_kthreads(void)
{
	/* Kernel message to restart our kthread */
	void test_up_sem(struct trapframe *tf, uint32_t srcid, void *a0, void *a1,
	                 void *a2)
	{
		struct semaphore *sem = (struct semaphore*)a0;
		struct kthread *kthread;
		printk("[kmsg] Upping the sem to start the kthread, stacktop is %08p\n",
		       get_stack_top());
		kthread = __up_sem(sem);
		if (!kthread) {
			printk("[kmsg] Crap, the sem didn't have a kthread waiting!\n");
			return;
		}
		printk("[kmsg] Restarting the kthread...\n");
		restart_kthread(kthread);
		panic("[kmsg] Damnit...");
	}
	struct semaphore sem;
	init_sem(&sem, 1);		/* set to 1 to test the unwind */
	printk("We're a kthread!  Stacktop is %08p.  Testing suspend, etc...\n",
	       get_stack_top());
	/* So we have something that will wake us up.  Routine messages won't get
	 * serviced in the kernel right away. */
	send_kernel_message(core_id(), test_up_sem, (void*)&sem, 0, 0,
	                    KMSG_ROUTINE);
	/* Actually block (or try to) */
	/* This one shouldn't block - but will test the unwind (if 1 above) */
	printk("About to sleep, but should unwind (signal beat us)\n");
	sleep_on(&sem);
	/* This one is for real, yo.  Run and tell that. */
	printk("About to sleep for real\n");
	sleep_on(&sem);
	printk("Kthread restarted!, Stacktop is %08p.\n", get_stack_top());
}
