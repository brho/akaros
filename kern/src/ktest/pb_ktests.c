/*
 * Postboot kernel tests: Tests to be ran after boot in kernel mode.
 * TODO: Some of the tests here may not necessarily be tests to be ran after
 *       boot. If that is the case, change them in
 */

#include <arch/mmu.h>
#include <arch/arch.h>
#include <arch/uaccess.h>
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
#include <mm.h>
#include <multiboot.h>
#include <pmap.h>
#include <page_alloc.h>
#include <pmap.h>
#include <slab.h>
#include <kmalloc.h>
#include <hashtable.h>
#include <radix.h>
#include <circular_buffer.h>
#include <monitor.h>
#include <kthread.h>
#include <schedule.h>
#include <umem.h>
#include <init.h>
#include <ucq.h>
#include <setjmp.h>
#include <sort.h>

#include <apipe.h>
#include <rwlock.h>
#include <rendez.h>
#include <ktest.h>
#include <smallidpool.h>
#include <linker_func.h>

KTEST_SUITE("POSTBOOT")

#ifdef CONFIG_X86

// TODO: Do test if possible inside this function, and add assertions.
bool test_ipi_sending(void)
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

	return true;
}

// TODO: Refactor to make it return and add assertions.
// Note this never returns and will muck with any other timer work
bool test_pic_reception(void)
{
	register_irq(IdtPIC + IrqCLOCK, test_hello_world_handler, NULL,
	             MKBUS(BusISA, 0, 0, 0));
	pit_set_timer(100,TIMER_RATEGEN); // totally arbitrary time
	pic_unmask_irq(0, 0);
	cprintf("PIC1 Mask = 0x%04x\n", inb(PIC1_DATA));
	cprintf("PIC2 Mask = 0x%04x\n", inb(PIC2_DATA));
	unmask_lapic_lvt(MSR_LAPIC_LVT_LINT0);
	printk("Core %d's LINT0: 0x%08x\n", core_id(),
	       apicrget(MSR_LAPIC_LVT_TIMER));
	enable_irq();
	while(1);

	return true;
}

#endif // CONFIG_X86

barrier_t test_cpu_array;

// TODO: Add assertions, try to do everything from within this same function.
bool test_barrier(void)
{
	cprintf("Core 0 initializing barrier\n");
	init_barrier(&test_cpu_array, num_cores);
	cprintf("Core 0 asking all cores to print ids, barrier, rinse, repeat\n");
	smp_call_function_all(test_barrier_handler, NULL, 0);

	return true;
}

// TODO: Maybe remove all the printing statements and instead use the
//       KT_ASSERT_M macro to include a message on assertions.
bool test_interrupts_irqsave(void)
{
	int8_t state = 0;
	printd("Testing Nesting Enabling first, turning ints off:\n");
	disable_irq();
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Enabling IRQSave\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Done.  Should have been 0, 200, 200, 200, 0\n");

	printd("Testing Nesting Disabling first, turning ints on:\n");
	state = 0;
	enable_irq();
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Disabling IRQSave Once\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Disabling IRQSave Again\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Enabling IRQSave Once\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Enabling IRQSave Again\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Done.  Should have been 200, 0, 0, 0, 200 \n");

	state = 0;
	disable_irq();
	printd("Ints are off, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Done.  Should have been 200, 0\n");

	state = 0;
	enable_irq();
	printd("Ints are on, enabling then disabling.\n");
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Done.  Should have been 200, 200\n");

	state = 0;
	disable_irq();
	printd("Ints are off, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	printd("Done.  Should have been 0, 0\n");

	state = 0;
	enable_irq();
	printd("Ints are on, disabling then enabling.\n");
	disable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(!irq_is_enabled());
	enable_irqsave(&state);
	printd("Interrupts are: %x\n", irq_is_enabled());
	KT_ASSERT(irq_is_enabled());
	printd("Done.  Should have been 0, 200\n");

	disable_irq();
	return true;
}

// TODO: Maybe remove PRINT_BITMASK statements and use KT_ASSERT_M instead
//       somehow.
bool test_bitmasks(void)
{
#define masksize 67
	DECL_BITMASK(mask, masksize);
	CLR_BITMASK(mask, masksize);
//	PRINT_BITMASK(mask, masksize);
	SET_BITMASK_BIT(mask, 0);
	SET_BITMASK_BIT(mask, 11);
	SET_BITMASK_BIT(mask, 17);
	SET_BITMASK_BIT(mask, masksize-1);
//	PRINT_BITMASK(mask, masksize);
	DECL_BITMASK(mask2, masksize);
	COPY_BITMASK(mask2, mask, masksize);
//	printk("copy of original mask, should be the same as the prev\n");
//	PRINT_BITMASK(mask2, masksize);
	CLR_BITMASK_BIT(mask, 11);
//	PRINT_BITMASK(mask, masksize);
	KT_ASSERT_M("Bit 17 should be 1", 1 == GET_BITMASK_BIT(mask, 17));
	KT_ASSERT_M("Bit 11 should be 0", 0 == GET_BITMASK_BIT(mask, 11));
	FILL_BITMASK(mask, masksize);
//	PRINT_BITMASK(mask, masksize);
	KT_ASSERT_M("Bitmask should not be clear after calling FILL_BITMASK",
	            0 == BITMASK_IS_CLEAR(mask,masksize));
	CLR_BITMASK(mask, masksize);
//	PRINT_BITMASK(mask, masksize);
	KT_ASSERT_M("Bitmask should be clear after calling CLR_BITMASK",
	            1 == BITMASK_IS_CLEAR(mask,masksize));
	return true;
}

checklist_t *the_global_list;

static void test_checklist_handler(struct hw_trapframe *hw_tf, void *data)
{
	udelay(1000000);
	cprintf("down_checklist(%x,%d)\n", the_global_list, core_id());
	down_checklist(the_global_list);
}

// TODO: Add assertions
bool test_checklists(void)
{
	INIT_CHECKLIST(a_list, MAX_NUM_CORES);
	the_global_list = &a_list;
	printk("Checklist Build, mask size: %d\n", sizeof(a_list.mask.bits));
	printk("mask\n");
	PRINT_BITMASK(a_list.mask.bits, a_list.mask.size);
	SET_BITMASK_BIT(a_list.mask.bits, 11);
	printk("Set bit 11\n");
	PRINT_BITMASK(a_list.mask.bits, a_list.mask.size);

	CLR_BITMASK(a_list.mask.bits, a_list.mask.size);
	INIT_CHECKLIST_MASK(a_mask, MAX_NUM_CORES);
	FILL_BITMASK(a_mask.bits, num_cores);
	//CLR_BITMASK_BIT(a_mask.bits, core_id());
	//SET_BITMASK_BIT(a_mask.bits, 1);
	//printk("New mask (1, 17, 25):\n");
	printk("Created new mask, filled up to num_cores\n");
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

	return true;
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

// TODO: Add assertions.
bool test_smp_call_functions(void)
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
	for(i = 1; i < num_cores; i++)
		smp_call_function_single(i, test_hello_world_handler, NULL, 0);
	printk("\nCore %d: SMP Call Self (wait):\n", me);
	printk("---------------------\n");
	smp_call_function_self(test_hello_world_handler, NULL, &waiter0);
	smp_call_wait(waiter0);
	printk("\nCore %d: SMP Call All-Else Individually, in order (wait):\n", me);
	printk("---------------------\n");
	for(i = 1; i < num_cores; i++)
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
	smp_call_function_single(1 % num_cores, test_incrementer_handler, &a, 0);
	smp_call_function_single(2 % num_cores, test_incrementer_handler, &b, 0);
	smp_call_function_single(3 % num_cores, test_incrementer_handler, &c, 0);
	smp_call_function_single(4 % num_cores, test_incrementer_handler, &a, 0);
	smp_call_function_single(5 % num_cores, test_incrementer_handler, &b, 0);
	smp_call_function_single(6 % num_cores, test_incrementer_handler, &c, 0);
	smp_call_function_all(test_incrementer_handler, &a, 0);
	smp_call_function_single(3 % num_cores, test_incrementer_handler, &c, 0);
	smp_call_function_all(test_incrementer_handler, &b, 0);
	smp_call_function_single(1 % num_cores, test_incrementer_handler, &a, 0);
	smp_call_function_all(test_incrementer_handler, &c, 0);
	smp_call_function_single(2 % num_cores, test_incrementer_handler, &b, 0);
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

	return true;
}

#ifdef CONFIG_X86
// TODO: Fix the KT_ASSERTs
bool test_lapic_status_bit(void)
{
	register_irq(I_TESTING, test_incrementer_handler, &a,
	             MKBUS(BusIPI, 0, 0, 0));
	#define NUM_IPI 100000
	atomic_set(&a,0);
	printk("IPIs received (should be 0): %d\n", a);
	// KT_ASSERT_M("IPIs received should be 0", (0 == a));
	for(int i = 0; i < NUM_IPI; i++) {
		send_ipi(7, I_TESTING);
	}
	// need to wait a bit to let those IPIs get there
	udelay(5000000);
	printk("IPIs received (should be %d): %d\n", a, NUM_IPI);
	// KT_ASSERT_M("IPIs received should be 100000", (NUM_IPI == a));
	// hopefully that handler never fires again.  leaving it registered for now.

	return true;
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
// TODO: Add assertions.
bool test_pit(void)
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

	return true;
}

// TODO: Add assertions.
bool test_circ_buffer(void)
{
	int arr[5] = {0, 1, 2, 3, 4};

	for (int i = 0; i < 5; i++) {
		FOR_CIRC_BUFFER(i, 5, j)
			printk("Starting with current = %d, each value = %d\n", i, j);
	}

	return true;
}

static void test_km_handler(uint32_t srcid, long a0, long a1, long a2)
{
	printk("Received KM on core %d from core %d: arg0= %p, arg1 = %p, "
	       "arg2 = %p\n", core_id(), srcid, a0, a1, a2);
	return;
}

// TODO: Add assertions. Try to do everything inside this function.
bool test_kernel_messages(void)
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

	return true;
}
#endif // CONFIG_X86

static size_t test_hash_fn_col(void *k)
{
	return (size_t)k % 2; // collisions in slots 0 and 1
}

bool test_hashtable(void)
{
	struct test {int x; int y;};
	struct test tstruct[10];

	struct hashtable *h;
	uintptr_t k = 5;
	struct test *v = &tstruct[0];

	h = create_hashtable(32, __generic_hash, __generic_eq);

	// test inserting one item, then finding it again
	KT_ASSERT_M("It should be possible to insert items to a hashtable",
	            hashtable_insert(h, (void*)k, v));
	v = NULL;
	KT_ASSERT_M("It should be possible to find inserted stuff in a hashtable",
	            (v = hashtable_search(h, (void*)k)));

	KT_ASSERT_M("The extracted element should be the same we inserted",
	            (v == &tstruct[0]));

	v = NULL;

	KT_ASSERT_M("It should be possible to remove an existing element",
	            (v = hashtable_remove(h, (void*)k)));

	KT_ASSERT_M("An element should not remain in a hashtable after deletion",
	            !(v = hashtable_search(h, (void*)k)));

	/* Testing a bunch of items, insert, search, and removal */
	for (int i = 0; i < 10; i++) {
		k = i; // vary the key, we don't do KEY collisions
		KT_ASSERT_M("It should be possible to insert elements to a hashtable",
		            (hashtable_insert(h, (void*)k, &tstruct[i])));
	}
	// read out the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		KT_ASSERT_M("It should be possible to find inserted stuff in a hashtable",
		            (v = hashtable_search(h, (void*)k)));
		KT_ASSERT_M("The extracted element should be the same we inserted",
		            (v == &tstruct[i]));
	}

	KT_ASSERT_M("The total count of number of elements should be 10",
	            (10 == hashtable_count(h)));

	// remove the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		KT_ASSERT_M("It should be possible to remove an existing element",
		            (v = hashtable_remove(h, (void*)k)));

	}
	// make sure they are all gone
	for (int i = 0; i < 10; i++) {
		k = i;
		KT_ASSERT_M("An element should not remain in a hashtable after deletion",
		            !(v = hashtable_search(h, (void*)k)));
	}

	KT_ASSERT_M("The hashtable should be empty",
	            (0 == hashtable_count(h)));

	hashtable_destroy(h);

	// same test of a bunch of items, but with collisions.
	/* Testing a bunch of items with collisions, etc. */
	h = create_hashtable(32, test_hash_fn_col, __generic_eq);
	// insert 10 items
	for (int i = 0; i < 10; i++) {
		k = i; // vary the key, we don't do KEY collisions

		KT_ASSERT_M("It should be possible to insert elements to a hashtable",
		            (hashtable_insert(h, (void*)k, &tstruct[i])));
	}
	// read out the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		KT_ASSERT_M("It should be possible to find inserted stuff in a hashtable",
		            (v = hashtable_search(h, (void*)k)));
		KT_ASSERT_M("The extracted element should be the same we inserted",
		            (v == &tstruct[i]));
	}

	KT_ASSERT_M("The total count of number of elements should be 10",
	            (10 == hashtable_count(h)));

	// remove the 10 items
	for (int i = 0; i < 10; i++) {
		k = i;
		KT_ASSERT_M("It should be possible to remove an existing element",
		            (v = hashtable_remove(h, (void*)k)));
	}
	// make sure they are all gone
	for (int i = 0; i < 10; i++) {
		k = i;

		KT_ASSERT_M("An element should not remain in a hashtable after deletion",
		            !(v = hashtable_search(h, (void*)k)));
	}

	KT_ASSERT_M("The hashtable should be empty",
	            (0 == hashtable_count(h)));

	hashtable_destroy(h);

	return true;
}

bool test_circular_buffer(void)
{
	static const size_t cbsize = 4096;
	struct circular_buffer cb;
	char *bigbuf;
	size_t csize, off, cnum, mxsize;
	char buf[256];

	KT_ASSERT_M("Failed to build the circular buffer",
				circular_buffer_init(&cb, cbsize, NULL));

	for (size_t i = 0; i < 8 * cbsize; i++) {
		size_t len = snprintf(buf, sizeof(buf), "%lu\n", i);

		KT_ASSERT_M("Circular buffer write failed",
					circular_buffer_write(&cb, buf, len) == len);
	}
	cnum = off = 0;
	while ((csize = circular_buffer_read(&cb, buf, sizeof(buf), off)) != 0) {
		char *top = buf + csize;
		char *ptr = buf;
		char *pnl;

		while ((pnl = memchr(ptr, '\n', top - ptr)) != NULL) {
			size_t num;

			*pnl = 0;
			num = strtoul(ptr, NULL, 10);
			KT_ASSERT_M("Numbers should be ascending", num >= cnum);
			cnum = num;
			ptr = pnl + 1;
		}

		off += ptr - buf;
	}

	for (size_t i = 0; i < (cbsize / sizeof(buf) + 1); i++) {
		memset(buf, (int) i, sizeof(buf));

		KT_ASSERT_M("Circular buffer write failed",
					circular_buffer_write(&cb, buf,
										  sizeof(buf)) == sizeof(buf));
	}
	cnum = off = 0;
	while ((csize = circular_buffer_read(&cb, buf, sizeof(buf), off)) != 0) {
		size_t num = buf[0];

		KT_ASSERT_M("Invalid record read size", csize == sizeof(buf));

		if (off != 0)
			KT_ASSERT_M("Invalid record sequence number",
						num == ((cnum + 1) % 256));
		cnum = num;
		off += csize;
	}

	bigbuf = kzmalloc(cbsize, MEM_WAIT);
	KT_ASSERT(bigbuf != NULL);

	mxsize = circular_buffer_max_write_size(&cb);
	KT_ASSERT_M("Circular buffer max write failed",
				circular_buffer_write(&cb, bigbuf, mxsize) == mxsize);

	memset(bigbuf, 17, cbsize);
	csize = circular_buffer_read(&cb, bigbuf, mxsize, 0);
	KT_ASSERT_M("Invalid max record read size", csize == mxsize);

	for (size_t i = 0; i < csize; i++)
		KT_ASSERT_M("Invalid max record value", bigbuf[i] == 0);

	kfree(bigbuf);

	circular_buffer_destroy(&cb);

	return TRUE;
}

/* Ghetto test, only tests one prod or consumer at a time */
// TODO: Un-guetto test, add assertions.
bool test_bcq(void)
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

	return true;
}

/* Test a simple concurrent send and receive (one prod, one cons).  We spawn a
 * process that will go into _M mode on another core, and we'll do the test from
 * an alarm handler run on our core.  When we start up the process, we won't
 * return so we need to defer the work with an alarm. */
// TODO: Check if we can add more assertions.
bool test_ucq(void)
{
	struct timer_chain *tchain = &per_cpu_info[core_id()].tchain;
	struct alarm_waiter *waiter = kmalloc(sizeof(struct alarm_waiter), 0);

	/* Alarm handler: what we want to do after the process is up */
	void send_msgs(struct alarm_waiter *waiter)
	{
		struct timer_chain *tchain;
		struct proc *p = waiter->data;
		uintptr_t old_proc;
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
	struct file_or_chan *program;

	program = foc_open("/bin/ucq", O_READ, 0);

	KT_ASSERT_M("We should be able to find /bin/ucq",
	            program);

	struct proc *p = proc_create(program, NULL, NULL);
	proc_wakeup(p);
	/* instead of getting rid of the reference created in proc_create, we'll put
	 * it in the awaiter */
	waiter->data = p;
	foc_decref(program);
	/* Should never return from schedule (env_pop in there) also note you may
	 * not get the process you created, in the event there are others floating
	 * around that are runnable */
	run_scheduler();
	smp_idle();

	KT_ASSERT_M("We should never return from schedule",
	            false);

	return true;
}

bool test_radix_tree(void)
{
	struct radix_tree real_tree = RADIX_INITIALIZER;
	struct radix_tree *tree = &real_tree;
	void *retval;

	KT_ASSERT_M("It should be possible to insert at 0",
	            !radix_insert(tree, 0, (void*)0xdeadbeef, 0));
	radix_delete(tree, 0);
	KT_ASSERT_M("It should be possible to re-insert at 0",
	            !radix_insert(tree, 0, (void*)0xdeadbeef, 0));

	KT_ASSERT_M("It should be possible to insert first",
	            !radix_insert(tree, 3, (void*)0xdeadbeef, 0));
	radix_insert(tree, 4, (void*)0x04040404, 0);
	KT_ASSERT((void*)0xdeadbeef == radix_lookup(tree, 3));
	for (int i = 5; i < 100; i++)
		if ((retval = radix_lookup(tree, i))) {
			printk("Extra item %p at slot %d in tree %p\n", retval, i,
			       tree);
			print_radix_tree(tree);
			monitor(0);
		}
	KT_ASSERT_M("It should be possible to insert a two-tier",
	            !radix_insert(tree, 65, (void*)0xcafebabe, 0));
	KT_ASSERT_M("It should not be possible to reinsert",
	            radix_insert(tree, 4, (void*)0x03030303, 0));
	KT_ASSERT_M("It should be possible to insert a two-tier boundary",
	            !radix_insert(tree, 4095, (void*)0x4095, 0));
	KT_ASSERT_M("It should be possible to insert a three-tier",
	            !radix_insert(tree, 4096, (void*)0x4096, 0));
	//print_radix_tree(tree);
	radix_delete(tree, 65);
	radix_delete(tree, 3);
	radix_delete(tree, 4);
	radix_delete(tree, 4095);
	radix_delete(tree, 4096);
	//print_radix_tree(tree);

	return true;
}

/* Assorted FS tests, which were hanging around in init.c */
// TODO: remove all the print statements and try to convert most into assertions
bool test_random_fs(void)
{
	int retval = do_symlink("/dir1/sym", "/bin/hello", S_IRWXU);
	KT_ASSERT_M("symlink1 should be created successfully",
	            (!retval));
	retval = do_symlink("/symdir", "/dir1/dir1-1", S_IRWXU);
	KT_ASSERT_M("symlink1 should be created successfully",
	            (!retval));
	retval = do_symlink("/dir1/test.txt", "/dir2/test2.txt", S_IRWXU);
	KT_ASSERT_M("symlink2 should be created successfully",
	            (!retval));
	retval = do_symlink("/dir1/dir1-1/up", "../../", S_IRWXU);
	KT_ASSERT_M("symlink3 should be created successfully",
	            (!retval));
	retval = do_symlink("/bin/hello-sym", "hello", S_IRWXU);
	KT_ASSERT_M("symlink4 should be created successfully",
	            (!retval));

	struct dentry *dentry;
	struct nameidata nd_r = {0}, *nd = &nd_r;
	retval = path_lookup("/dir1/sym", 0, nd);
	KT_ASSERT_M("symlink lookup should work for an existing symlink",
	            (!retval));
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

	KT_ASSERT_M("symlink lookup should work for an existing symlink",
	            (!retval));
	printk("Pathlookup got %s (hello)\n", nd->dentry->d_name.name);
	path_release(nd);

	/* try with a directory */
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/f1-1.txt", 0, nd);
	KT_ASSERT_M("symlink lookup should work for an existing symlink",
	            (!retval));
	printk("Pathlookup got %s (f1-1.txt)\n", nd->dentry->d_name.name);
	path_release(nd);

	/* try with a rel path */
	printk("Try with a rel path\n");
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/up/hello.txt", 0, nd);
	KT_ASSERT_M("symlink lookup should work for an existing symlink",
	            (!retval));
	printk("Pathlookup got %s (hello.txt)\n", nd->dentry->d_name.name);
	path_release(nd);

	printk("Try for an ELOOP\n");
	memset(nd, 0, sizeof(struct nameidata));
	retval = path_lookup("/symdir/up/symdir/up/symdir/up/symdir/up/hello.txt", 0, nd);
	KT_ASSERT_M("symlink lookup should fail for a non existing symlink",
	            (retval));
	path_release(nd);

	return true;
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
// TODO: Add assertions.
bool test_kthreads(void)
{
	struct semaphore sem = SEMAPHORE_INITIALIZER(sem, 1);
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

	return true;
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
// TODO: I believe we need more assertions.
bool test_kref(void)
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
	KT_ASSERT(kref_refcnt(&local_kref) == 1);
	printk("[TEST-KREF] Simple 2-core getting/putting passed.\n");

	return true;
}

// TODO: Add more descriptive assertion messages.
bool test_atomics(void)
{
	/* subtract_and_test */
	atomic_t num;
	/* Test subing to 0 */
	atomic_init(&num, 1);
	KT_ASSERT(atomic_sub_and_test(&num, 1) == 1);
	atomic_init(&num, 2);
	KT_ASSERT(atomic_sub_and_test(&num, 2) == 1);
	/* Test not getting to 0 */
	atomic_init(&num, 1);
	KT_ASSERT(atomic_sub_and_test(&num, 0) == 0);
	atomic_init(&num, 2);
	KT_ASSERT(atomic_sub_and_test(&num, 1) == 0);
	/* Test negatives */
	atomic_init(&num, -1);
	KT_ASSERT(atomic_sub_and_test(&num, 1) == 0);
	atomic_init(&num, -1);
	KT_ASSERT(atomic_sub_and_test(&num, -1) == 1);
	/* Test larger nums */
	atomic_init(&num, 265);
	KT_ASSERT(atomic_sub_and_test(&num, 265) == 1);
	atomic_init(&num, 265);
	KT_ASSERT(atomic_sub_and_test(&num, 2) == 0);

	/* CAS */
	/* Simple test, make sure the bool retval of CAS handles failure */
	bool test_cas_val(long init_val)
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
		if (atomic_read(&actual_num) != init_val + 10) {
			return false;
		} else {
			return true;
		}
	}
	KT_ASSERT_M("CAS test for 257 should be successful.",
	            test_cas_val(257));
	KT_ASSERT_M("CAS test for 1 should be successful.",
	            test_cas_val(1));
	return true;
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
// TODO: Add assertions.
bool test_abort_halt(void)
{
#ifdef CONFIG_X86
	send_kernel_message(1, __test_try_halt, 0, 0, 0, KMSG_ROUTINE);
	/* wait 1 sec, enough time to for core 1 to be in its KMSG */
	udelay(1000000);
	/* Send an IPI */
	send_ipi(0x01, I_TESTING);
	printk("Core 0 sent the IPI\n");
#endif /* CONFIG_X86 */
	return true;
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

// TODO: Add more assertions.
bool test_cv(void)
{
	int nr_msgs;

	cv_init(cv);
	/* Test 0: signal without waiting */
	cv_broadcast(cv);
	cv_signal(cv);
	kthread_yield();
	printk("test_cv: signal without waiting complete\n");

	/* Test 1: single / minimal shit */
	nr_msgs = num_cores - 1; /* not using cpu 0 */
	atomic_init(&counter, nr_msgs);
	for (int i = 1; i < num_cores; i++)
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
		int cpu = (i % (num_cores - 1)) + 1;
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
	KT_ASSERT(!cv->nr_waiters);
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
			KT_ASSERT(!cv->nr_waiters);
		}
	}
	printk("test_cv: single sender/receiver complete\n");

	return true;
}

/* Based on a bug I noticed.  TODO: actual memset test... */
bool test_memset(void)
{
	#define ARR_SZ 256

	void print_array(char *c, size_t len)
	{
		for (int i = 0; i < len; i++)
			printk("%04d: %02x\n", i, *c++);
	}

	bool check_array(char *c, char x, size_t len)
	{
		for (int i = 0; i < len; i++) {
			#define ASSRT_SIZE 64
			char *assrt_msg = (char*) kmalloc(ASSRT_SIZE, 0);
			snprintf(assrt_msg, ASSRT_SIZE,
				     "Char %d is %c (%02x), should be %c (%02x)", i, *c, *c,
				     x, x);
			KT_ASSERT_M(assrt_msg, (*c == x));
			c++;
		}
		return true;
	}

	bool run_check(char *arr, int ch, size_t len)
	{
		char *c = arr;
		for (int i = 0; i < ARR_SZ; i++)
			*c++ = 0x0;
		memset(arr, ch, len - 4);
		if (check_array(arr, ch, len - 4) &&
		    check_array(arr + len - 4, 0x0, 4)) {
			return true;
		} else {
			return false;
		}
	}

	char bytes[ARR_SZ];

	if (!run_check(bytes, 0xfe, 20) || !run_check(bytes, 0xc0fe, 20)) {
		return false;
	}

	return true;
}

void __attribute__((noinline)) __longjmp_wrapper(struct jmpbuf* jb)
{
	asm ("");
	printk("Starting: %s\n", __FUNCTION__);
	longjmp(jb, 1);
	// Should never get here
	printk("Exiting: %s\n", __FUNCTION__);
}

// TODO: Add assertions.
bool test_setjmp()
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

	return true;
}

// TODO: add assertions.
bool test_apipe(void)
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
	KT_ASSERT(pipe_buf);
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

	return true;
}

static struct rwlock rwlock, *rwl = &rwlock;
static atomic_t rwlock_counter;
// TODO: Add assertions.
bool test_rwlock(void)
{
	bool ret;
	rwinit(rwl);
	/* Basic: can i lock twice, recursively? */
	rlock(rwl);
	ret = canrlock(rwl);
	KT_ASSERT(ret);
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
	atomic_init(&rwlock_counter, (num_cores - 1) * 4);
	for (int i = 1; i < num_cores; i++)
		for (int j = 0; j < 4; j++)
			send_kernel_message(i, __test_rwlock, 0, 0, 0, KMSG_ROUTINE);
	while (atomic_read(&rwlock_counter))
		cpu_relax();
	printk("rwlock test complete\n");

	return true;
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

// TODO: Add more assertions.
bool test_rv(void)
{
	int nr_msgs;

	rendez_init(rv);
	/* Test 0: signal without waiting */
	rendez_wakeup(rv);
	kthread_yield();
	printk("test_rv: wakeup without sleeping complete\n");

	/* Test 1: a few sleepers */
	nr_msgs = num_cores - 1; /* not using cpu 0 */
	atomic_init(&counter, nr_msgs);
	state = FALSE;
	for (int i = 1; i < num_cores; i++)
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
		int cpu = (i % (num_cores - 1)) + 1;
		/* timeouts from 0ms ..5000ms (enough that they should wake via cond */
		if (atomic_read(&counter) % 5)
			send_kernel_message(cpu, __test_rv_sleeper_timeout, i * 4000, 0, 0,
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
	KT_ASSERT(!rv->cv.nr_waiters);
	printk("test_rv: lots of sleepers/timeouts complete\n");

	return true;
}

/* Cheap test for the alarm internal management */
// TODO: Add assertions.
bool test_alarm(void)
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

	return true;
}

bool test_kmalloc_incref(void)
{
	/* this test is a bit invasive of the kmalloc internals */
	void *__get_unaligned_orig_buf(void *buf)
	{
		int *tag_flags = (int*)(buf - sizeof(int));
		if ((*tag_flags & KMALLOC_FLAG_MASK) == KMALLOC_TAG_UNALIGN)
			return (buf - (*tag_flags >> KMALLOC_ALIGN_SHIFT));
		else
			return 0;
	}

	bool test_buftag(void *b, struct kmalloc_tag *btag, char *str)
	{
		KT_ASSERT_M(str, kref_refcnt(&btag->kref) == 1);
		kmalloc_incref(b);
		KT_ASSERT_M(str, kref_refcnt(&btag->kref) == 2);
		kfree(b);
		KT_ASSERT_M(str, kref_refcnt(&btag->kref) == 1);
		kfree(b);
		/* dangerous read, it's been freed */
		KT_ASSERT_M(str, kref_refcnt(&btag->kref) == 0);
		return TRUE;
	}

	void *b1, *b2, *b2o;
	struct kmalloc_tag *b1tag, *b2tag;

	/* no realigned case */
	b1 = kmalloc(55, 0);
	KT_ASSERT(!__get_unaligned_orig_buf(b1));
	b1tag = (struct kmalloc_tag*)(b1 - sizeof(struct kmalloc_tag));

	/* realigned case.  alloc'd before b1's test, so we know we get different
	 * buffers. */
	b2 = kmalloc_align(55, 0, 64);
	b2o = __get_unaligned_orig_buf(b2);
	KT_ASSERT(b2o);
	b2tag = (struct kmalloc_tag*)(b2o - sizeof(struct kmalloc_tag));

	test_buftag(b1, b1tag, "b1, no realign");
	test_buftag(b2, b2tag, "b2, realigned");

	return TRUE;
}

/* Some ghetto things:
 * - ASSERT_M only lets you have a string, not a format string.
 * - put doesn't return, so we have a "loud" test for that.  alternatively, we
 *   could have put panic, but then we couldn't test it at all.  and i don't
 *   particularly want it to have a return value.
 * - ASSERT_M just blindly returns.  we're leaking memory.
 */
bool test_u16pool(void)
{
	#define AMT 4096
	int *t;
	struct u16_pool *id = create_u16_pool(AMT);
	int i, x, y;
	int numalloc;
	KT_ASSERT(id);

	t = kzmalloc(sizeof(int) * (AMT + 1), MEM_WAIT);
	for (x = 0; x < 1024; x++) {
		KT_ASSERT_M("Should be empty", id->tos == 0);
		for (i = 0; i < id->size; i++) {
			int p = get_u16(id);
			if (p < 0)
				KT_ASSERT_M("Couldn't get enough", 0);
			t[i] = p;
		}
		numalloc = i;
		// free them at random. With luck, we don't get too many duplicate
		// hits.
		for (y = i = 0; i < numalloc; y++) {
			/* could read genrand, but that could be offline */
			int f = (uint16_t)read_tsc() % numalloc;
			if (!t[f])
				continue;
			put_u16(id, t[f]);
			t[f] = 0;
			i++;
			/* that's long enough... */
			if (y > 2 * id->size)
				break;
		}
		/* grab the leftovers */
		for (i = 0; i < id->size; i++) {
			if (!t[i])
				continue;
			put_u16(id, t[i]);
			t[i] = 0;
		}
		/* all of our previous checks failed to give back 0 */
		put_u16(id, 0);
	}

	// pop too many.
	bool we_broke = FALSE;
	for (i = 0; i < id->size * 2; i++) {
		x = get_u16(id);
		if (x == -1) {
			we_broke = TRUE;
			break;
		}
		t[i] = x;
	}
	KT_ASSERT_M("Should have failed to get too many", we_broke);

	numalloc = i;

	printd("Allocated %d items\n", numalloc);
	for (i = 0; i < numalloc; i++) {
		put_u16(id, t[i]);
		t[i] = 0;
	}
	KT_ASSERT_M("Should be empty", id->tos == 0);

	printk("Ignore next BAD, testing bad alloc\n");
	put_u16(id, 25);	// should get an error.
	for (i = 0; i < id->size; i++) {
		int v = get_u16(id);
		if (t[v])
			printd("BAD: %d pops twice!\n", v);
		KT_ASSERT_M("Popped twice!", t[v] == 0);
		t[v] = 1;
		//printk("%d,", v);
	}

	for (i = 1; i < id->size; i++) {
		if (!t[i])
			printd("BAD: %d was not set\n", i);
		KT_ASSERT_M("Wasn't set!", t[i]);
	}

	kfree(t);
	return FALSE;
}

static bool uaccess_mapped(void *addr, char *buf, char *buf2)
{
	KT_ASSERT_M(
		"Copy to user (u8) to mapped address should not fail",
		copy_to_user(addr, buf, 1) == 0);
	KT_ASSERT_M(
		"Copy to user (u16) to mapped address should not fail",
		copy_to_user(addr, buf, 2) == 0);
	KT_ASSERT_M(
		"Copy to user (u32) to mapped address should not fail",
		copy_to_user(addr, buf, 4) == 0);
	KT_ASSERT_M(
		"Copy to user (u64) to mapped address should not fail",
		copy_to_user(addr, buf, 8) == 0);
	KT_ASSERT_M(
		"Copy to user (mem) to mapped address should not fail",
		copy_to_user(addr, buf, sizeof(buf)) == 0);

	KT_ASSERT_M(
		"Copy from user (u8) to mapped address should not fail",
		copy_from_user(buf, addr, 1) == 0);
	KT_ASSERT_M(
		"Copy from user (u16) to mapped address should not fail",
		copy_from_user(buf, addr, 2) == 0);
	KT_ASSERT_M(
		"Copy from user (u32) to mapped address should not fail",
		copy_from_user(buf, addr, 4) == 0);
	KT_ASSERT_M(
		"Copy from user (u64) to mapped address should not fail",
		copy_from_user(buf, addr, 8) == 0);
	KT_ASSERT_M(
		"Copy from user (mem) to mapped address should not fail",
		copy_from_user(buf, addr, sizeof(buf)) == 0);

	KT_ASSERT_M(
		"String copy to user to mapped address should not fail",
		strcpy_to_user(current, addr, "Akaros") == 0);
	KT_ASSERT_M(
		"String copy from user to mapped address should not fail",
		strcpy_from_user(current, buf, addr) == 0);
	KT_ASSERT_M("The copied string content should be matching",
				memcmp(buf, "Akaros", 7) == 0);

	return TRUE;
}

static bool uaccess_unmapped(void *addr, char *buf, char *buf2)
{
	KT_ASSERT_M("Copy to user (u8) to not mapped address should fail",
				copy_to_user(addr, buf, 1) == -EFAULT);
	KT_ASSERT_M("Copy to user (u16) to not mapped address should fail",
				copy_to_user(addr, buf, 2) == -EFAULT);
	KT_ASSERT_M("Copy to user (u32) to not mapped address should fail",
				copy_to_user(addr, buf, 4) == -EFAULT);
	KT_ASSERT_M("Copy to user (u64) to not mapped address should fail",
				copy_to_user(addr, buf, 8) == -EFAULT);
	KT_ASSERT_M("Copy to user (mem) to not mapped address should fail",
				copy_to_user(addr, buf, sizeof(buf)) == -EFAULT);

	KT_ASSERT_M("Copy from user (u8) to not mapped address should fail",
				copy_from_user(buf, addr, 1) == -EFAULT);
	KT_ASSERT_M("Copy from user (u16) to not mapped address should fail",
				copy_from_user(buf, addr, 2) == -EFAULT);
	KT_ASSERT_M("Copy from user (u32) to not mapped address should fail",
				copy_from_user(buf, addr, 4) == -EFAULT);
	KT_ASSERT_M("Copy from user (u64) to not mapped address should fail",
				copy_from_user(buf, addr, 8) == -EFAULT);
	KT_ASSERT_M("Copy from user (mem) to not mapped address should fail",
				copy_from_user(buf, addr, sizeof(buf)) == -EFAULT);

	KT_ASSERT_M(
		"String copy to user to not mapped address should fail",
		strcpy_to_user(NULL, addr, "Akaros") == -EFAULT);
	KT_ASSERT_M(
		"String copy from user to not mapped address should fail",
		strcpy_from_user(NULL, buf, addr) == -EFAULT);

	KT_ASSERT_M("Copy from user with kernel side source pointer should fail",
				copy_from_user(buf, buf2, sizeof(buf)) == -EFAULT);
	KT_ASSERT_M("Copy to user with kernel side source pointer should fail",
				copy_to_user(buf, buf2, sizeof(buf)) == -EFAULT);

	return TRUE;
}

bool test_uaccess(void)
{
	char buf[128] = { 0 };
	char buf2[128] = { 0 };
	struct proc *tmp;
	uintptr_t switch_tmp;
	int err;
	static const size_t mmap_size = 4096;
	void *addr;
	bool passed = FALSE;

	err = proc_alloc(&tmp, 0, 0);
	KT_ASSERT_M("Failed to alloc a temp proc", err == 0);
	/* Tell everyone we're ready in case some ops don't work on PROC_CREATED */
	__proc_set_state(tmp, PROC_RUNNABLE_S);
	switch_tmp = switch_to(tmp);
	addr = mmap(tmp, 0, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED)
		goto out;
	passed = uaccess_mapped(addr, buf, buf2);
	munmap(tmp, (uintptr_t) addr, mmap_size);
	if (!passed)
		goto out;
	passed = uaccess_unmapped(addr, buf, buf2);
out:
	switch_back(tmp, switch_tmp);
	proc_decref(tmp);
	return passed;
}

bool test_sort(void)
{
	int cmp_longs_asc(const void *p1, const void *p2)
	{
		const long v1 = *(const long *) p1;
		const long v2 = *(const long *) p2;

		return v1 < v2 ? -1 : (v1 > v2 ? 1 : 0);
	}

	int cmp_longs_desc(const void *p1, const void *p2)
	{
		const long v1 = *(const long *) p1;
		const long v2 = *(const long *) p2;

		return v1 < v2 ? 1 : (v1 > v2 ? -1 : 0);
	}

	size_t i;
	long long_set_1[] = {
		-9, 11, 0, 23, 123, -99, 3, 11, 23, -999, 872, 17, 21
	};
	long long_set_2[] = {
		31, 77, -1, 2, 0, 64, 11, 19, 69, 111, -89, 17, 21, 44, 77
	};

	sort(long_set_1, ARRAY_SIZE(long_set_1), sizeof(long), cmp_longs_asc);
	for (i = 1; i < ARRAY_SIZE(long_set_1); i++)
		KT_ASSERT(long_set_1[i - 1] <= long_set_1[i]);

	sort(long_set_2, ARRAY_SIZE(long_set_2), sizeof(long), cmp_longs_desc);
	for (i = 1; i < ARRAY_SIZE(long_set_2); i++)
		KT_ASSERT(long_set_2[i - 1] >= long_set_2[i]);

	return TRUE;
}

bool test_cmdline_parse(void)
{
	static const char *fake_cmdline =
		"kernel -root=/foo -simple -num=123 -quoted='abc \\'' -dup=311 "
		"-dup='akaros' -empty='' -inner=-outer -outer=-inner=xyz";
	const char *opt;
	char param[128];

	/* Note that the get_boot_option() API should be passed NULL the first time
	 * it is called, in normal cases, and should be passed the value returned by
	 * previous call to get_boot_option(), in case multiple options with same
	 * name have to be fetched.
	 */
	opt = get_boot_option(fake_cmdline, "-root", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -root option", opt);
	KT_ASSERT_M("Invalid -root option value", strcmp(param, "/foo") == 0);

	opt = get_boot_option(fake_cmdline, "-root", NULL, 0);
	KT_ASSERT_M("Unable to parse -root option when param not provided", opt);

	opt = get_boot_option(fake_cmdline, "-simple", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -simple option", opt);
	KT_ASSERT_M("Invalid -simple option value", strcmp(param, "") == 0);

	opt = get_boot_option(fake_cmdline, "-num", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -num option", opt);
	KT_ASSERT_M("Invalid -num option value", strcmp(param, "123") == 0);

	opt = get_boot_option(fake_cmdline, "-quoted", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -quoted option", opt);
	KT_ASSERT_M("Invalid -quoted option value", strcmp(param, "abc '") == 0);

	opt = get_boot_option(fake_cmdline, "-dup", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -dup option", opt);
	KT_ASSERT_M("Invalid -dup option first value", strcmp(param, "311") == 0);

	opt = get_boot_option(opt, "-dup", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -dup option", opt);
	KT_ASSERT_M("Invalid -dup option second value",
				strcmp(param, "akaros") == 0);

	opt = get_boot_option(fake_cmdline, "-inner", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -inner option", opt);
	KT_ASSERT_M("Invalid -inner option value", strcmp(param, "-outer") == 0);

	opt = get_boot_option(opt, "-inner", param, sizeof(param));
	KT_ASSERT_M("Should not be parsing -inner as value", !opt);

	opt = get_boot_option(fake_cmdline, "-outer", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -outer option", opt);
	KT_ASSERT_M("Invalid -outer option value",
				strcmp(param, "-inner=xyz") == 0);

	opt = get_boot_option(fake_cmdline, "-missing", param, sizeof(param));
	KT_ASSERT_M("Should not be parsing -missing option", !opt);

	opt = get_boot_option(fake_cmdline, "-inne", NULL, 0);
	KT_ASSERT_M("Should not be parsing -inne option", !opt);

	opt = get_boot_option(fake_cmdline, "-outera", NULL, 0);
	KT_ASSERT_M("Should not be parsing -outera option", !opt);

	opt = get_boot_option(fake_cmdline, "-empty", param, sizeof(param));
	KT_ASSERT_M("Unable to parse -empty option", opt);
	KT_ASSERT_M("Invalid -empty option value", strcmp(param, "") == 0);

	return TRUE;
}

static struct ktest ktests[] = {
#ifdef CONFIG_X86
	KTEST_REG(ipi_sending,        CONFIG_TEST_ipi_sending),
	KTEST_REG(pic_reception,      CONFIG_TEST_pic_reception),
	KTEST_REG(lapic_status_bit,   CONFIG_TEST_lapic_status_bit),
	KTEST_REG(pit,                CONFIG_TEST_pit),
	KTEST_REG(circ_buffer,        CONFIG_TEST_circ_buffer),
	KTEST_REG(kernel_messages,    CONFIG_TEST_kernel_messages),
#endif // CONFIG_X86
	KTEST_REG(barrier,            CONFIG_TEST_barrier),
	KTEST_REG(interrupts_irqsave, CONFIG_TEST_interrupts_irqsave),
	KTEST_REG(bitmasks,           CONFIG_TEST_bitmasks),
	KTEST_REG(checklists,         CONFIG_TEST_checklists),
	KTEST_REG(smp_call_functions, CONFIG_TEST_smp_call_functions),
	KTEST_REG(hashtable,          CONFIG_TEST_hashtable),
	KTEST_REG(circular_buffer,    CONFIG_TEST_circular_buffer),
	KTEST_REG(bcq,                CONFIG_TEST_bcq),
	KTEST_REG(ucq,                CONFIG_TEST_ucq),
	KTEST_REG(radix_tree,         CONFIG_TEST_radix_tree),
	KTEST_REG(random_fs,          CONFIG_TEST_random_fs),
	KTEST_REG(kthreads,           CONFIG_TEST_kthreads),
	KTEST_REG(kref,               CONFIG_TEST_kref),
	KTEST_REG(atomics,            CONFIG_TEST_atomics),
	KTEST_REG(abort_halt,         CONFIG_TEST_abort_halt),
	KTEST_REG(cv,                 CONFIG_TEST_cv),
	KTEST_REG(memset,             CONFIG_TEST_memset),
	KTEST_REG(setjmp,             CONFIG_TEST_setjmp),
	KTEST_REG(apipe,              CONFIG_TEST_apipe),
	KTEST_REG(rwlock,             CONFIG_TEST_rwlock),
	KTEST_REG(rv,                 CONFIG_TEST_rv),
	KTEST_REG(alarm,              CONFIG_TEST_alarm),
	KTEST_REG(kmalloc_incref,     CONFIG_TEST_kmalloc_incref),
	KTEST_REG(u16pool,            CONFIG_TEST_u16pool),
	KTEST_REG(uaccess,            CONFIG_TEST_uaccess),
	KTEST_REG(sort,               CONFIG_TEST_sort),
	KTEST_REG(cmdline_parse,      CONFIG_TEST_cmdline_parse),
};
static int num_ktests = sizeof(ktests) / sizeof(struct ktest);
linker_func_1(register_pb_ktests)
{
	REGISTER_KTESTS(ktests, num_ktests);
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
