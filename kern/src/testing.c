
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
#include <ros/ucq.h>

#include <atomic.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <testing.h>
#include <trap.h>
#include <process.h>
#include <syscall.h>
#include <time.h>
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
#include <schedule.h>
#include <umem.h>
#include <ucq.h>
#include <setjmp.h>
#include <apipe.h>
#include <rwlock.h>
#include <rendez.h>

#define l1 (available_caches.l1)
#define l2 (available_caches.l2)
#define l3 (available_caches.l3)

#ifdef CONFIG_X86

void test_ipi_sending(void)
{
	int8_t state = 0;

	register_irq(I_TESTING, test_hello_world_handler, NULL,
	             MKBUS(BusIPI, 0, 0, 0));
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
	send_ipi(0x01, I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 2\n");
	send_ipi(0x02, I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 3\n");
	send_ipi(0x03, I_TESTING);
	udelay(3000000);
	cprintf("\nCORE 0 sending ipi to physical 15\n");
	send_ipi(0x0f, I_TESTING);
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
	register_irq(IdtPIC + IrqCLOCK, test_hello_world_handler, NULL,
	             MKBUS(BusISA, 0, 0, 0));
	pit_set_timer(100,TIMER_RATEGEN); // totally arbitrary time
	pic_unmask_irq(0, 0);
	cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
	cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
	unmask_lapic_lvt(LAPIC_LVT_LINT0);
	cprintf("Core %d's LINT0: 0x%08x\n", core_id(), read_mmreg32(LAPIC_LVT_LINT0));
	enable_irq();
	while(1);
}

#endif // CONFIG_X86


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

static void test_checklist_handler(struct hw_trapframe *hw_tf, void *data)
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

static void test_incrementer_handler(struct hw_trapframe *tf, void *data)
{
	assert(data);
	atomic_inc(data);
}

static void test_null_handler(struct hw_trapframe *tf, void *data)
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

#ifdef CONFIG_X86
void test_lapic_status_bit(void)
{
	register_irq(I_TESTING, test_incrementer_handler, &a,
	             MKBUS(BusIPI, 0, 0, 0));
	#define NUM_IPI 100000
	atomic_set(&a,0);
	printk("IPIs received (should be 0): %d\n", a);
	for(int i = 0; i < NUM_IPI; i++) {
		send_ipi(7, I_TESTING);
		lapic_wait_to_send();
	}
	// need to wait a bit to let those IPIs get there
	udelay(5000000);
	printk("IPIs received (should be %d): %d\n", a, NUM_IPI);
	// hopefully that handler never fires again.  leaving it registered for now.
}
#endif // CONFIG_X86

/************************************************************/
/* ISR Handler Functions */

void test_hello_world_handler(struct hw_trapframe *hw_tf, void *data)
{
	int trapno;
	#if defined(CONFIG_X86)
	trapno = hw_tf->tf_trapno;
	#else
	trapno = 0;
	#endif

	cprintf("Incoming IRQ, ISR: %d on core %d with tf at %p\n",
	        trapno, core_id(), hw_tf);
}

spinlock_t print_info_lock = SPINLOCK_INITIALIZER_IRQSAVE;

void test_print_info_handler(struct hw_trapframe *hw_tf, void *data)
{
	uint64_t tsc = read_tsc();

	spin_lock_irqsave(&print_info_lock);
	cprintf("----------------------------\n");
	cprintf("This is Core %d\n", core_id());
	cprintf("Timestamp = %lld\n", tsc);
#ifdef CONFIG_X86
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
#endif // CONFIG_X86
	cprintf("----------------------------\n");
	spin_unlock_irqsave(&print_info_lock);
}

void test_barrier_handler(struct hw_trapframe *hw_tf, void *data)
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

static void test_waiting_handler(struct hw_trapframe *hw_tf, void *data)
{
	atomic_dec(data);
}

#ifdef CONFIG_X86
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
	register_irq(I_TESTING, test_waiting_handler, &waiting,
	             MKBUS(BusIPI, 0, 0, 0));
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

static void test_km_handler(uint32_t srcid, long a0, long a1, long a2)
{
	printk("Received KM on core %d from core %d: arg0= %p, arg1 = %p, "
	       "arg2 = %p\n", core_id(), srcid, a0, a1, a2);
	return;
}

void test_kernel_messages(void)
{
	printk("Testing Kernel Messages\n");
	/* Testing sending multiples, sending different types, alternating, and
	 * precendence (the immediates should trump the others) */
	printk("sending 5 IMMED to core 1, sending (#,deadbeef,0)\n");
	for (int i = 0; i < 5; i++)
		send_kernel_message(1, test_km_handler, (long)i, 0xdeadbeef, 0,
		                    KMSG_IMMEDIATE);
	udelay(5000000);
	printk("sending 5 routine to core 1, sending (#,cafebabe,0)\n");
	for (int i = 0; i < 5; i++)
		send_kernel_message(1, test_km_handler, (long)i, 0xcafebabe, 0,
		                    KMSG_ROUTINE);
	udelay(5000000);
	printk("sending 10 routine and 3 immediate to core 2\n");
	for (int i = 0; i < 10; i++)
		send_kernel_message(2, test_km_handler, (long)i, 0xcafebabe, 0,
		                    KMSG_ROUTINE);
	for (int i = 0; i < 3; i++)
		send_kernel_message(2, test_km_handler, (long)i, 0xdeadbeef, 0,
		                    KMSG_IMMEDIATE);
	udelay(5000000);
	printk("sending 5 ea alternating to core 2\n");
	for (int i = 0; i < 5; i++) {
		send_kernel_message(2, test_km_handler, (long)i, 0xdeadbeef, 0,
		                    KMSG_IMMEDIATE);
		send_kernel_message(2, test_km_handler, (long)i, 0xcafebabe, 0,
		                    KMSG_ROUTINE);
	}
	udelay(5000000);
	return;
}
#endif // CONFIG_X86
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
		size = (KMALLOC_SMALLEST << i) - sizeof(struct kmalloc_tag);
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
	uintptr_t k = 5;
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
		printk("A BCQ (made of ints): %p\n", bcq);
		printk("\tprod_idx: %p\n", bcq->hdr.prod_idx);
		printk("\tcons_pub_idx: %p\n", bcq->hdr.cons_pub_idx);
		printk("\tcons_pvt_idx: %p\n", bcq->hdr.cons_pvt_idx);
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

/* Test a simple concurrent send and receive (one prod, one cons).  We spawn a
 * process that will go into _M mode on another core, and we'll do the test from
 * an alarm handler run on our core.  When we start up the process, we won't
 * return so we need to defer the work with an alarm. */
void test_ucq(void)
{
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter *waiter = kmalloc(sizeof(struct alarm_waiter), 0);

	/* Alarm handler: what we want to do after the process is up */
	void send_msgs(struct alarm_waiter *waiter)
	{
		struct timer_chain *tchain;
		struct proc *old_proc, *p = waiter->data;
		struct ucq *ucq = (struct ucq*)USTACKTOP;
		struct event_msg msg;

		printk("Running the alarm handler!\n");
		printk("NR msg per page: %d\n", NR_MSG_PER_PAGE);
		/* might not be mmaped yet, if not, abort.  We used to user_mem_check,
		 * but now we just touch it and PF. */
		char touch = *(char*)ucq;
		asm volatile ("" : : "r"(touch));
		/* load their address space */
		old_proc = switch_to(p);
		/* So it's mmaped, see if it is ready (note that this is dangerous) */
		if (!ucq->ucq_ready) {
			printk("Not ready yet\n");
			switch_back(p, old_proc);
			goto abort;
		}
		/* So it's ready, time to finally do the tests... */
		printk("[kernel] Finally starting the tests... \n");
		/* 1: Send a simple message */
		printk("[kernel] #1 Sending simple message (7, deadbeef)\n");
		msg.ev_type = 7;
		msg.ev_arg2 = 0xdeadbeef;
		send_ucq_msg(ucq, p, &msg);
		printk("nr_pages: %d\n", atomic_read(&ucq->nr_extra_pgs));
		/* 2: Send a bunch.  In a VM, this causes one swap, and then a bunch of
		 * mmaps. */
		printk("[kernel] #2 \n");
		for (int i = 0; i < 5000; i++) {
			msg.ev_type = i;
			send_ucq_msg(ucq, p, &msg);
		}
		printk("nr_pages: %d\n", atomic_read(&ucq->nr_extra_pgs));
		printk("[kernel] #3 \n");
		/* 3: make sure we chained pages (assuming 1k is enough) */
		for (int i = 0; i < 1000; i++) {
			msg.ev_type = i;
			send_ucq_msg(ucq, p, &msg);
		}
		printk("nr_pages: %d\n", atomic_read(&ucq->nr_extra_pgs));
		/* other things we could do:
		 *  - concurrent producers / consumers...  ugh.
		 *  - would require a kmsg to another core, instead of a local alarm
		 */
		/* done, switch back and free things */
		switch_back(p, old_proc);
		proc_decref(p);
		kfree(waiter); /* since it was kmalloc()d */
		return;
	abort:
		tchain = &per_cpu_info[core_id()].tchain;
		/* Set to run again */
		set_awaiter_rel(waiter, 1000000);
		set_alarm(tchain, waiter);
	}
	/* Set up a handler to run the real part of the test */
	init_awaiter(waiter, send_msgs);
	set_awaiter_rel(waiter, 1000000);	/* 1s should be long enough */
	set_alarm(tchain, waiter);
	/* Just spawn the program */
	struct file *program;
	program = do_file_open("/bin/ucq", 0, 0);
	if (!program) {
		printk("Unable to find /bin/ucq!\n");
		return;
	}
	char *p_envp[] = {"LD_LIBRARY_PATH=/lib", 0};
	struct proc *p = proc_create(program, 0, p_envp);
	proc_wakeup(p);
	/* instead of getting rid of the reference created in proc_create, we'll put
	 * it in the awaiter */
	waiter->data = p;
	kref_put(&program->f_kref);
	/* Should never return from schedule (env_pop in there) also note you may
	 * not get the process you created, in the event there are others floating
	 * around that are runnable */
	run_scheduler();
	smp_idle();
	assert(0);
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

	if (radix_insert(tree, 0, (void*)0xdeadbeef, 0))
		printk("Failed to insert at 0!\n");
	radix_delete(tree, 0);
	if (radix_insert(tree, 0, (void*)0xdeadbeef, 0))
		printk("Failed to re-insert at 0!\n");

	if (radix_insert(tree, 3, (void*)0xdeadbeef, 0))
		printk("Failed to insert first!\n");
	radix_insert(tree, 4, (void*)0x04040404, 0);
	assert((void*)0xdeadbeef == radix_lookup(tree, 3));
	for (int i = 5; i < 100; i++)
		if ((retval = radix_lookup(tree, i))) {
			printk("Extra item %p at slot %d in tree %p\n", retval, i,
			       tree);
			print_radix_tree(tree);
			monitor(0);
		}
	if (radix_insert(tree, 65, (void*)0xcafebabe, 0))
		printk("Failed to insert a two-tier!\n");
	if (!radix_insert(tree, 4, (void*)0x03030303, 0))
		printk("Should not let us reinsert\n");
	if (radix_insert(tree, 4095, (void*)0x4095, 0))
		printk("Failed to insert a two-tier boundary!\n");
	if (radix_insert(tree, 4096, (void*)0x4096, 0))
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

/* Kernel message to restart our kthread */
static void __test_up_sem(uint32_t srcid, long a0, long a1, long a2)
{
	struct semaphore *sem = (struct semaphore*)a0;
	printk("[kmsg] Upping the sem to start the kthread, stacktop is %p\n",
		   get_stack_top());
	if (!sem_up(sem)) {
		printk("[kmsg] Crap, the sem didn't have a kthread waiting!\n");
		return;
	}
	printk("Kthread will restart when we handle the __launch RKM\n");
}

/* simple test - start one, do something else, and resume it.  For lack of a
 * better infrastructure, we send ourselves a kmsg to run the kthread, which
 * we'll handle in smp_idle (which you may have to manually call).  Note this
 * doesn't test things like memory being leaked, or dealing with processes. */
void test_kthreads(void)
{
	struct semaphore sem;
	sem_init(&sem, 1);		/* set to 1 to test the unwind */
	printk("We're a kthread!  Stacktop is %p.  Testing suspend, etc...\n",
	       get_stack_top());
	/* So we have something that will wake us up.  Routine messages won't get
	 * serviced in the kernel right away. */
	send_kernel_message(core_id(), __test_up_sem, (long)&sem, 0, 0,
	                    KMSG_ROUTINE);
	/* Actually block (or try to) */
	/* This one shouldn't block - but will test the unwind (if 1 above) */
	printk("About to sleep, but should unwind (signal beat us)\n");
	sem_down(&sem);
	/* This one is for real, yo.  Run and tell that. */
	printk("About to sleep for real\n");
	sem_down(&sem);
	printk("Kthread restarted!, Stacktop is %p.\n", get_stack_top());
}

/* Second player's kmsg */
static void __test_kref_2(uint32_t srcid, long a0, long a1, long a2)
{
	struct kref *kref = (struct kref*)a0;
	bool *done = (bool*)a1;
	enable_irq();
	for (int i = 0; i < 10000000; i++) {
		kref_get(kref, 1);
		set_core_timer(1, TRUE);
		udelay(2);
		kref_put(kref);
	}
	*done = TRUE;
}

/* Runs a simple test between core 0 (caller) and core 2 */
void test_kref(void)
{
	struct kref local_kref;
	bool done = FALSE;
	
	kref_init(&local_kref, fake_release, 1);
	send_kernel_message(2, __test_kref_2, (long)&local_kref, (long)&done, 0,
	                    KMSG_ROUTINE);
	for (int i = 0; i < 10000000; i++) {
		kref_get(&local_kref, 1);
		udelay(2);
		kref_put(&local_kref);
	}
	while (!done)
		cpu_relax();
	assert(kref_refcnt(&local_kref) == 1);
	printk("[TEST-KREF] Simple 2-core getting/putting passed.\n");
}

void test_atomics(void)
{
	/* subtract_and_test */
	atomic_t num;
	/* Test subing to 0 */
	atomic_init(&num, 1);
	assert(atomic_sub_and_test(&num, 1) == 1);
	atomic_init(&num, 2);
	assert(atomic_sub_and_test(&num, 2) == 1);
	/* Test not getting to 0 */
	atomic_init(&num, 1);
	assert(atomic_sub_and_test(&num, 0) == 0);
	atomic_init(&num, 2);
	assert(atomic_sub_and_test(&num, 1) == 0);
	/* Test negatives */
	atomic_init(&num, -1);
	assert(atomic_sub_and_test(&num, 1) == 0);
	atomic_init(&num, -1);
	assert(atomic_sub_and_test(&num, -1) == 1);
	/* Test larger nums */
	atomic_init(&num, 265);
	assert(atomic_sub_and_test(&num, 265) == 1);
	atomic_init(&num, 265);
	assert(atomic_sub_and_test(&num, 2) == 0);

	/* CAS */
	/* Simple test, make sure the bool retval of CAS handles failure */
	void test_cas_val(long init_val)
	{
		atomic_t actual_num;
		long old_num;
		int attempt;
		atomic_init(&actual_num, init_val);
		attempt = 0;
		do {
			old_num = atomic_read(&actual_num);
			/* First time, try to fail */
			if (attempt == 0) 
				old_num++;
			attempt++;	
		} while (!atomic_cas(&actual_num, old_num, old_num + 10));
		if (atomic_read(&actual_num) != init_val + 10)
			printk("FUCK, CAS test failed for %d\n", init_val);
	}
	test_cas_val(257);
	test_cas_val(1);
}

/* Helper KMSG for test_abort.  Core 1 does this, while core 0 sends an IRQ. */
static void __test_try_halt(uint32_t srcid, long a0, long a1, long a2)
{
	disable_irq();
	/* wait 10 sec.  should have a bunch of ints pending */
	udelay(10000000);
	printk("Core 1 is about to halt\n");
	cpu_halt();
	printk("Returned from halting on core 1\n");
}

/* x86 test, making sure our cpu_halt() and handle_irq() work.  If you want to
 * see it fail, you'll probably need to put a nop in the asm for cpu_halt(), and
 * comment out abort_halt() in handle_irq(). */
void test_abort_halt(void)
{
#ifdef CONFIG_X86
	send_kernel_message(1, __test_try_halt, 0, 0, 0, KMSG_ROUTINE);
	/* wait 1 sec, enough time to for core 1 to be in its KMSG */
	udelay(1000000);
	/* Send an IPI */
	send_ipi(0x01, I_TESTING);
	printk("Core 0 sent the IPI\n");
#endif /* CONFIG_X86 */
}

/* Funcs and global vars for test_cv() */
static struct cond_var local_cv;
static atomic_t counter;
static struct cond_var *cv = &local_cv;
static volatile bool state = FALSE;		/* for test 3 */

void __test_cv_signal(uint32_t srcid, long a0, long a1, long a2)
{
	if (atomic_read(&counter) % 4)
		cv_signal(cv);
	else
		cv_broadcast(cv);
	atomic_dec(&counter);
}

void __test_cv_waiter(uint32_t srcid, long a0, long a1, long a2)
{
	cv_lock(cv);
	/* check state, etc */
	cv_wait_and_unlock(cv);
	atomic_dec(&counter);
}

void __test_cv_waiter_t3(uint32_t srcid, long a0, long a1, long a2)
{
	udelay(a0);
	/* if state == false, we haven't seen the signal yet */
	cv_lock(cv);
	while (!state) {
		cpu_relax();
		cv_wait(cv);	/* unlocks and relocks */
	}
	cv_unlock(cv);
	/* Make sure we are done, tell the controller we are done */
	cmb();
	assert(state);
	atomic_dec(&counter);
}

void test_cv(void)
{
	int nr_msgs;

	cv_init(cv);
	/* Test 0: signal without waiting */
	cv_broadcast(cv);
	cv_signal(cv);
	kthread_yield();
	printk("test_cv: signal without waiting complete\n");

	/* Test 1: single / minimal shit */
	nr_msgs = num_cpus - 1; /* not using cpu 0 */
	atomic_init(&counter, nr_msgs);
	for (int i = 1; i < num_cpus; i++)
		send_kernel_message(i, __test_cv_waiter, 0, 0, 0, KMSG_ROUTINE);
	udelay(1000000);
	cv_signal(cv);
	kthread_yield();
	while (atomic_read(&counter) != nr_msgs - 1)
		cpu_relax();
	printk("test_cv: single signal complete\n");
	cv_broadcast(cv);
	/* broadcast probably woke up the waiters on our core.  since we want to
	 * spin on their completion, we need to yield for a bit. */
	kthread_yield();
	while (atomic_read(&counter))
		cpu_relax();
	printk("test_cv: broadcast signal complete\n");

	/* Test 2: shitloads of waiters and signalers */
	nr_msgs = 0x500;	/* any more than 0x20000 could go OOM */
	atomic_init(&counter, nr_msgs);
	for (int i = 0; i < nr_msgs; i++) {
		int cpu = (i % (num_cpus - 1)) + 1;
		if (atomic_read(&counter) % 5)
			send_kernel_message(cpu, __test_cv_waiter, 0, 0, 0, KMSG_ROUTINE);
		else
			send_kernel_message(cpu, __test_cv_signal, 0, 0, 0, KMSG_ROUTINE);
	}
	kthread_yield();	/* run whatever messages we sent to ourselves */
	while (atomic_read(&counter)) {
		cpu_relax();
		cv_broadcast(cv);
		udelay(1000000);
		kthread_yield();	/* run whatever messages we sent to ourselves */
	}
	assert(!cv->nr_waiters);
	printk("test_cv: massive message storm complete\n");

	/* Test 3: basic one signaller, one receiver.  we want to vary the amount of
	 * time the sender and receiver delays, starting with (1ms, 0ms) and ending
	 * with (0ms, 1ms).  At each extreme, such as with the sender waiting 1ms,
	 * the receiver/waiter should hit the "check and wait" point well before the
	 * sender/signaller hits the "change state and signal" point. */
	for (int i = 0; i < 1000; i++) {
		for (int j = 0; j < 10; j++) {	/* some extra chances at each point */
			state = FALSE;
			atomic_init(&counter, 1);	/* signal that the client is done */
			/* client waits for i usec */
			send_kernel_message(2, __test_cv_waiter_t3, i, 0, 0, KMSG_ROUTINE);
			cmb();
			udelay(1000 - i);	/* senders wait time: 1000..0 */
			state = TRUE;
			cv_signal(cv);
			/* signal might have unblocked a kthread, let it run */
			kthread_yield();
			/* they might not have run at all yet (in which case they lost the
			 * race and don't need the signal).  but we need to wait til they're
			 * done */
			while (atomic_read(&counter))
				cpu_relax();
			assert(!cv->nr_waiters);
		}
	}
	printk("test_cv: single sender/receiver complete\n");
}

/* Based on a bug I noticed.  TODO: actual memset test... */
void test_memset(void)
{
	#define ARR_SZ 256
	
	void print_array(char *c, size_t len)
	{
		for (int i = 0; i < len; i++)
			printk("%04d: %02x\n", i, *c++);
	}
	
	void check_array(char *c, char x, size_t len)
	{
		for (int i = 0; i < len; i++) {
			if (*c != x) {
				printk("Char %d is %c (%02x), should be %c (%02x)\n", i, *c,
				       *c, x, x);
				break;
			}
			c++;
		}
	}
	
	void run_check(char *arr, int ch, size_t len)
	{
		char *c = arr;
		for (int i = 0; i < ARR_SZ; i++)
			*c++ = 0x0;
		memset(arr, ch, len - 4);
		check_array(arr, ch, len - 4);
		check_array(arr + len - 4, 0x0, 4);
	}

	char bytes[ARR_SZ];
	run_check(bytes, 0xfe, 20);
	run_check(bytes, 0xc0fe, 20);
	printk("Done!\n");
}

void __attribute__((noinline)) __longjmp_wrapper(struct jmpbuf* jb)
{
	asm ("");
	printk("Starting: %s\n", __FUNCTION__);
	longjmp(jb, 1);
	// Should never get here
	printk("Exiting: %s\n", __FUNCTION__); 
}

void test_setjmp()
{
	struct jmpbuf jb;
	printk("Starting: %s\n", __FUNCTION__);
	if (setjmp(&jb)) {
	  printk("After second setjmp return: %s\n", __FUNCTION__);
    }
    else {
	  printk("After first setjmp return: %s\n", __FUNCTION__);
      __longjmp_wrapper(&jb);
    }
	printk("Exiting: %s\n", __FUNCTION__);
}

void test_apipe(void)
{
	static struct atomic_pipe test_pipe;

	struct some_struct {
		long x;
		int y;
	};
	/* Don't go too big, or you'll run off the stack */
	#define MAX_BATCH 100

	void __test_apipe_writer(uint32_t srcid, long a0, long a1, long a2)
	{
		int ret, count_todo;
		int total = 0;
		struct some_struct local_str[MAX_BATCH];
		for (int i = 0; i < MAX_BATCH; i++) {
			local_str[i].x = 0xf00;
			local_str[i].y = 0xba5;
		}
		/* testing 0, and max out at 50. [0, ... 50] */
		for (int i = 0; i < MAX_BATCH + 1; i++) {
			count_todo = i;
			while (count_todo) {
				ret = apipe_write(&test_pipe, &local_str, count_todo);
				/* Shouldn't break, based on the loop counters */
				if (!ret) {
					printk("Writer breaking with %d left\n", count_todo);
					break;
				}
				total += ret;
				count_todo -= ret;
			}
		}
		printk("Writer done, added %d elems\n", total);
		apipe_close_writer(&test_pipe);
	}

	void __test_apipe_reader(uint32_t srcid, long a0, long a1, long a2)
	{
		int ret, count_todo;
		int total = 0;
		struct some_struct local_str[MAX_BATCH] = {{0}};
		/* reversed loop compared to the writer [50, ... 0] */
		for (int i = MAX_BATCH; i >= 0; i--) {
			count_todo = i;
			while (count_todo) {
				ret = apipe_read(&test_pipe, &local_str, count_todo);
				if (!ret) {
					printk("Reader breaking with %d left\n", count_todo);
					break;
				}
				total += ret;
				count_todo -= ret;
			}
		}
		printk("Reader done, took %d elems\n", total);
		for (int i = 0; i < MAX_BATCH; i++) {
			assert(local_str[i].x == 0xf00);
			assert(local_str[i].y == 0xba5);
		}
		apipe_close_reader(&test_pipe);
	}

	void *pipe_buf = kpage_alloc_addr();
	assert(pipe_buf);
	apipe_init(&test_pipe, pipe_buf, PGSIZE, sizeof(struct some_struct));
	printd("*ap_buf %p\n", test_pipe.ap_buf);
	printd("ap_ring_sz %p\n", test_pipe.ap_ring_sz);
	printd("ap_elem_sz %p\n", test_pipe.ap_elem_sz);
	printd("ap_rd_off %p\n", test_pipe.ap_rd_off);
	printd("ap_wr_off %p\n", test_pipe.ap_wr_off);
	printd("ap_nr_readers %p\n", test_pipe.ap_nr_readers);
	printd("ap_nr_writers %p\n", test_pipe.ap_nr_writers);
	send_kernel_message(0, __test_apipe_writer, 0, 0, 0, KMSG_ROUTINE);
	/* Once we start synchronizing with a kmsg / kthread that could be on a
	 * different core, we run the chance of being migrated when we block. */
	__test_apipe_reader(0, 0, 0, 0);
	/* Wait til the first test is done */
	while (test_pipe.ap_nr_writers) {
		kthread_yield();
		cpu_relax();
	}
	/* Try cross core (though CV wake ups schedule on the waking core) */
	apipe_open_reader(&test_pipe);
	apipe_open_writer(&test_pipe);
	send_kernel_message(1, __test_apipe_writer, 0, 0, 0, KMSG_ROUTINE);
	__test_apipe_reader(0, 0, 0, 0);
	/* We could be on core 1 now.  If we were called from core0, our caller
	 * might expect us to return while being on core 0 (like if we were kfunc'd
	 * from the monitor.  Be careful if you copy this code. */
}

static struct rwlock rwlock, *rwl = &rwlock;
static atomic_t rwlock_counter;
void test_rwlock(void)
{
	bool ret;
	rwinit(rwl);
	/* Basic: can i lock twice, recursively? */
	rlock(rwl);
	ret = canrlock(rwl);
	assert(ret);
	runlock(rwl);
	runlock(rwl);
	/* Other simply tests */
	wlock(rwl);
	wunlock(rwl);

	/* Just some half-assed different operations */
	void __test_rwlock(uint32_t srcid, long a0, long a1, long a2)
	{
		int rand = read_tsc() & 0xff;
		for (int i = 0; i < 10000; i++) {
			switch ((rand * i) % 5) {
				case 0:
				case 1:
					rlock(rwl);
					runlock(rwl);
					break;
				case 2:
				case 3:
					if (canrlock(rwl))
						runlock(rwl);
					break;
				case 4:
					wlock(rwl);
					wunlock(rwl);
					break;
			}
		}
		/* signal to allow core 0 to finish */
		atomic_dec(&rwlock_counter);
	}
		
	/* send 4 messages to each non core 0 */
	atomic_init(&rwlock_counter, (num_cpus - 1) * 4);
	for (int i = 1; i < num_cpus; i++)
		for (int j = 0; j < 4; j++)
			send_kernel_message(i, __test_rwlock, 0, 0, 0, KMSG_ROUTINE);
	while (atomic_read(&rwlock_counter))
		cpu_relax();
	printk("rwlock test complete\n");
}

/* Funcs and global vars for test_rv() */
static struct rendez local_rv;
static struct rendez *rv = &local_rv;
/* reusing state and counter from test_cv... */

static int __rendez_cond(void *arg)
{
	return *(bool*)arg;
}

void __test_rv_wakeup(uint32_t srcid, long a0, long a1, long a2)
{
	if (atomic_read(&counter) % 4)
		cv_signal(cv);
	else
		cv_broadcast(cv);
	atomic_dec(&counter);
}

void __test_rv_sleeper(uint32_t srcid, long a0, long a1, long a2)
{
	rendez_sleep(rv, __rendez_cond, (void*)&state);
	atomic_dec(&counter);
}

void __test_rv_sleeper_timeout(uint32_t srcid, long a0, long a1, long a2)
{
	/* half-assed amount of time. */
	rendez_sleep_timeout(rv, __rendez_cond, (void*)&state, a0);
	atomic_dec(&counter);
}

void test_rv(void)
{
	int nr_msgs;

	rendez_init(rv);
	/* Test 0: signal without waiting */
	rendez_wakeup(rv);
	kthread_yield();
	printk("test_rv: wakeup without sleeping complete\n");

	/* Test 1: a few sleepers */
	nr_msgs = num_cpus - 1; /* not using cpu 0 */
	atomic_init(&counter, nr_msgs);
	state = FALSE;
	for (int i = 1; i < num_cpus; i++)
		send_kernel_message(i, __test_rv_sleeper, 0, 0, 0, KMSG_ROUTINE);
	udelay(1000000);
	cmb();
	state = TRUE;
	rendez_wakeup(rv);
	/* broadcast probably woke up the waiters on our core.  since we want to
	 * spin on their completion, we need to yield for a bit. */
	kthread_yield();
	while (atomic_read(&counter))
		cpu_relax();
	printk("test_rv: bulk wakeup complete\n");

	/* Test 2: different types of sleepers / timeouts */
	state = FALSE;
	nr_msgs = 0x500;	/* any more than 0x20000 could go OOM */
	atomic_init(&counter, nr_msgs);
	for (int i = 0; i < nr_msgs; i++) {
		int cpu = (i % (num_cpus - 1)) + 1;
		/* timeouts from 0ms ..5000ms (enough that they should wake via cond */
		if (atomic_read(&counter) % 5)
			send_kernel_message(cpu, __test_rv_sleeper_timeout, i * 4, 0, 0,
			                    KMSG_ROUTINE);
		else
			send_kernel_message(cpu, __test_rv_sleeper, 0, 0, 0, KMSG_ROUTINE);
	}
	kthread_yield();	/* run whatever messages we sent to ourselves */
	state = TRUE;
	while (atomic_read(&counter)) {
		cpu_relax();
		rendez_wakeup(rv);
		udelay(1000000);
		kthread_yield();	/* run whatever messages we sent to ourselves */
	}
	assert(!rv->cv.nr_waiters);
	printk("test_rv: lots of sleepers/timeouts complete\n");
}

/* Cheap test for the alarm internal management */
void test_alarm(void)
{
	uint64_t now = tsc2usec(read_tsc());
	struct alarm_waiter await1, await2;
	struct timer_chain *tchain = &per_cpu_info[0].tchain;
	void shouldnt_run(struct alarm_waiter *awaiter)
	{
		printk("Crap, %p ran!\n", awaiter);
	}
	void empty_run(struct alarm_waiter *awaiter)
	{
		printk("Yay, %p ran (hopefully twice)!\n", awaiter);
	}
	/* Test basic insert, move, remove */
	init_awaiter(&await1, shouldnt_run);
	set_awaiter_abs(&await1, now + 1000000000);
	set_alarm(tchain, &await1);
	reset_alarm_abs(tchain, &await1, now + 1000000000 - 50);
	reset_alarm_abs(tchain, &await1, now + 1000000000 + 50);
	unset_alarm(tchain, &await1);
	/* Test insert of one that fired already */
	init_awaiter(&await2, empty_run);
	set_awaiter_rel(&await2, 1);
	set_alarm(tchain, &await2);
	enable_irq();
	udelay(1000);
	reset_alarm_abs(tchain, &await2, now + 10);
	udelay(1000);
	unset_alarm(tchain, &await2);

	printk("%s complete\n", __FUNCTION__);
}

/* Linker function tests.  Keep them commented, etc. */
#if 0
linker_func_1(xme11)
{
	printk("xme11\n");
}

linker_func_1(xme12)
{
	printk("xme12\n");
}

linker_func_1(xme13)
{
	printk("xme13\n");
}

linker_func_1(xme14)
{
	printk("xme14\n");
}

linker_func_1(xme15)
{
	printk("xme15\n");
}

linker_func_2(xme21)
{
	printk("xme21\n");
}

linker_func_2(xme22)
{
	printk("xme22\n");
}

linker_func_2(xme23)
{
	printk("xme23\n");
}

linker_func_2(xme24)
{
	printk("xme24\n");
}

linker_func_2(xme25)
{
	printk("xme25\n");
}

linker_func_3(xme31)
{
	printk("xme31\n");
}

linker_func_3(xme32)
{
	printk("xme32\n");
}

linker_func_3(xme33)
{
	printk("xme33\n");
}

linker_func_3(xme34)
{
	printk("xme34\n");
}

linker_func_3(xme35)
{
	printk("xme35\n");
}

linker_func_4(xme41)
{
	printk("xme41\n");
}

linker_func_4(xme42)
{
	printk("xme42\n");
}

linker_func_4(xme43)
{
	printk("xme43\n");
}

linker_func_4(xme44)
{
	printk("xme44\n");
}

linker_func_4(xme45)
{
	printk("xme45\n");
}
#endif /* linker func tests */
