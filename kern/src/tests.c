/*
 * Declaration of all the tests to be ran.
 */

#include <stdbool.h>
#include <test_infrastructure.h>
#include "tests_pb_kernel.c"


/* Postboot kernel tests declarations. */

struct pb_kernel_test pb_kernel_tests[] = {
#ifdef CONFIG_X86
	PB_K_TEST_REG(ipi_sending),
	PB_K_TEST_REG(pic_reception),
	PB_K_TEST_REG(ioapic_pit_reroute),
	PB_K_TEST_REG(lapic_status_bit),
	PB_K_TEST_REG(pit),
	PB_K_TEST_REG(circ_buffer),
	PB_K_TEST_REG(kernel_messages),
#endif // CONFIG_X86
#ifdef CONFIG_PAGE_COLORING
	PB_K_TEST_REG(page_coloring),
	PB_K_TEST_REG(color_alloc),
#endif // CONFIG_PAGE_COLORING
	PB_K_TEST_REG(print_info), 
	PB_K_TEST_REG(barrier),
	PB_K_TEST_REG(interrupts_irqsave),
	PB_K_TEST_REG(bitmasks),
	PB_K_TEST_REG(checklists),
	PB_K_TEST_REG(smp_call_functions),
	PB_K_TEST_REG(slab),
	PB_K_TEST_REG(kmalloc),
	PB_K_TEST_REG(hashtable),
	PB_K_TEST_REG(bcq),
	PB_K_TEST_REG(ucq),
	PB_K_TEST_REG(vm_regions),
	PB_K_TEST_REG(radix_tree),
	PB_K_TEST_REG(random_fs),
	PB_K_TEST_REG(kthreads),
	PB_K_TEST_REG(kref),
	PB_K_TEST_REG(atomics),
	PB_K_TEST_REG(abort_halt),
	PB_K_TEST_REG(cv),
	PB_K_TEST_REG(memset),
	PB_K_TEST_REG(setjmp),
	PB_K_TEST_REG(apipe),
	PB_K_TEST_REG(rwlock),
	PB_K_TEST_REG(rv),
	PB_K_TEST_REG(alarm)
};

int num_pb_kernel_tests = sizeof(pb_kernel_tests) /
                          sizeof(struct pb_kernel_test);
