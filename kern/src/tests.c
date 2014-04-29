/*
 * Declaration of all the tests to be ran.
 */

#include <stdbool.h>
#include <test_infrastructure.h>
#include "tests_pb_kernel.c"


/* Postboot kernel tests declarations. */

struct pb_kernel_test pb_kernel_tests[] = {
#ifdef CONFIG_X86
	PB_K_TEST_REG(ipi_sending,        CONFIG_TEST_ipi_sending),
	PB_K_TEST_REG(pic_reception,      CONFIG_TEST_pic_reception),
	PB_K_TEST_REG(ioapic_pit_reroute, CONFIG_TEST_ioapic_status_bit),
	PB_K_TEST_REG(lapic_status_bit,   CONFIG_TEST_lapic_status_bit),
	PB_K_TEST_REG(pit,                CONFIG_TEST_pit),
	PB_K_TEST_REG(circ_buffer,        CONFIG_TEST_circ_buffer),
	PB_K_TEST_REG(kernel_messages,    CONFIG_TEST_kernel_messages),
#endif // CONFIG_X86
#ifdef CONFIG_PAGE_COLORING
	PB_K_TEST_REG(page_coloring,      CONFIG_TEST_page_coloring),
	PB_K_TEST_REG(color_alloc,        CONFIG_TEST_color_alloc),
#endif // CONFIG_PAGE_COLORING
	PB_K_TEST_REG(print_info,         CONFIG_TEST_print_info),
	PB_K_TEST_REG(barrier,            CONFIG_TEST_barrier),
	PB_K_TEST_REG(interrupts_irqsave, CONFIG_TEST_interrupts_irqsave),
	PB_K_TEST_REG(bitmasks,           CONFIG_TEST_bitmasks),
	PB_K_TEST_REG(checklists,         CONFIG_TEST_checklists),
	PB_K_TEST_REG(smp_call_functions, CONFIG_TEST_smp_call_functions),
	PB_K_TEST_REG(slab,               CONFIG_TEST_slab),
	PB_K_TEST_REG(kmalloc,            CONFIG_TEST_kmalloc),
	PB_K_TEST_REG(hashtable,          CONFIG_TEST_hashtable),
	PB_K_TEST_REG(bcq,                CONFIG_TEST_bcq),
	PB_K_TEST_REG(ucq,                CONFIG_TEST_ucq),
	PB_K_TEST_REG(vm_regions,         CONFIG_TEST_vm_regions),
	PB_K_TEST_REG(radix_tree,         CONFIG_TEST_radix_tree),
	PB_K_TEST_REG(random_fs,          CONFIG_TEST_random_fs),
	PB_K_TEST_REG(kthreads,           CONFIG_TEST_kthreads),
	PB_K_TEST_REG(kref,               CONFIG_TEST_kref),
	PB_K_TEST_REG(atomics,            CONFIG_TEST_atomics),
	PB_K_TEST_REG(abort_halt,         CONFIG_TEST_abort_halt),
	PB_K_TEST_REG(cv,                 CONFIG_TEST_cv),
	PB_K_TEST_REG(memset,             CONFIG_TEST_memset),
	PB_K_TEST_REG(setjmp,             CONFIG_TEST_setjmp),
	PB_K_TEST_REG(apipe,              CONFIG_TEST_apipe),
	PB_K_TEST_REG(rwlock,             CONFIG_TEST_rwlock),
	PB_K_TEST_REG(rv,                 CONFIG_TEST_rv),
	PB_K_TEST_REG(alarm,              CONFIG_TEST_alarm)
};

int num_pb_kernel_tests = sizeof(pb_kernel_tests) /
                          sizeof(struct pb_kernel_test);
