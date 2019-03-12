/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Part of this code coming from a Linux kernel file:
 *
 * linux/arch/x86/mm/extable.c
 *
 * Which, even though missing specific copyright, it is supposed to be
 * ruled by the overall Linux copyright.
 */

#include <arch/uaccess.h>
#include <compiler.h>
#include <sort.h>
#include <ex_table.h>

extern struct extable_ip_fixup __attribute__((weak)) __start___ex_table[];
extern struct extable_ip_fixup __attribute__((weak)) __stop___ex_table[];

static int fixup_cmp(const void *f1, const void *f2)
{
	return ((const struct extable_ip_fixup *) f1)->insn <
		((const struct extable_ip_fixup *) f2)->insn ? -1 : 1;
}

void exception_table_init(void)
{
	if (__start___ex_table) {
		struct extable_ip_fixup *first = __start___ex_table;
		struct extable_ip_fixup *last = __stop___ex_table;
		uint64_t offset = 0;

		for (struct extable_ip_fixup *fx = first; fx < last; fx++) {
			fx->insn += offset;
			offset += sizeof(fx->insn);
			fx->fixup += offset;
			offset += sizeof(fx->fixup);
		}

		sort(first, last - first, sizeof(*first), fixup_cmp);

		offset = 0;
		for (struct extable_ip_fixup *fx = first; fx < last; fx++) {
			fx->insn -= offset;
			offset += sizeof(fx->insn);
			fx->fixup -= offset;
			offset += sizeof(fx->fixup);
		}
	}
}

uintptr_t get_fixup_ip(uintptr_t xip)
{
	const struct extable_ip_fixup *first = __start___ex_table;

	if (likely(first)) {
		const struct extable_ip_fixup *last = __stop___ex_table;

		while (first <= last) {
			const struct extable_ip_fixup *x =
				first + ((last - first) >> 1);
			uintptr_t insn = ex_insn_addr(x);

			if (insn < xip)
				first = x + 1;
			else if (insn > xip)
				last = x - 1;
			else
				return (uintptr_t) ex_fixup_addr(x);
		}
	}

	return 0;
}
