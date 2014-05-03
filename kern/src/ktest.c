/*
 * Declaration of all the tests to be ran.
 */

#include <stdbool.h>
#include <ktest.h>
#include "pb_ktests.c"

/* Global string used to report info about the last completed test */
char ktest_msg[1024];

/* Postboot kernel tests declarations. */
struct ktest pb_ktests[] = {
#ifdef CONFIG_X86
	KTEST_REG(ipi_sending,        CONFIG_TEST_ipi_sending),
	KTEST_REG(pic_reception,      CONFIG_TEST_pic_reception),
	KTEST_REG(ioapic_pit_reroute, CONFIG_TEST_ioapic_status_bit),
	KTEST_REG(lapic_status_bit,   CONFIG_TEST_lapic_status_bit),
	KTEST_REG(pit,                CONFIG_TEST_pit),
	KTEST_REG(circ_buffer,        CONFIG_TEST_circ_buffer),
	KTEST_REG(kernel_messages,    CONFIG_TEST_kernel_messages),
#endif // CONFIG_X86
#ifdef CONFIG_PAGE_COLORING
	KTEST_REG(page_coloring,      CONFIG_TEST_page_coloring),
	KTEST_REG(color_alloc,        CONFIG_TEST_color_alloc),
#endif // CONFIG_PAGE_COLORING
	KTEST_REG(print_info,         CONFIG_TEST_print_info),
	KTEST_REG(barrier,            CONFIG_TEST_barrier),
	KTEST_REG(interrupts_irqsave, CONFIG_TEST_interrupts_irqsave),
	KTEST_REG(bitmasks,           CONFIG_TEST_bitmasks),
	KTEST_REG(checklists,         CONFIG_TEST_checklists),
	KTEST_REG(smp_call_functions, CONFIG_TEST_smp_call_functions),
	KTEST_REG(slab,               CONFIG_TEST_slab),
	KTEST_REG(kmalloc,            CONFIG_TEST_kmalloc),
	KTEST_REG(hashtable,          CONFIG_TEST_hashtable),
	KTEST_REG(bcq,                CONFIG_TEST_bcq),
	KTEST_REG(ucq,                CONFIG_TEST_ucq),
	KTEST_REG(vm_regions,         CONFIG_TEST_vm_regions),
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
	KTEST_REG(alarm,              CONFIG_TEST_alarm)
};
int num_pb_ktests = sizeof(pb_ktests) / sizeof(struct ktest);

