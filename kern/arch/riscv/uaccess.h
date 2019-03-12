/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * Part of this code coming from a Linux kernel file:
 *
 * linux/arch/x86/include/asm/uaccess.h
 *
 * Which, even though missing specific copyright, it is supposed to be
 * ruled by the overall Linux copyright.
 */

#pragma once

#include <ros/errno.h>
#include <compiler.h>
#include <stdint.h>
#include <umem.h>

#define ASM_STAC
#define ASM_CLAC
#define __m(x) *(x)

struct extable_ip_fixup {
	uint64_t insn;
	uint64_t fixup;
};

#define _ASM_EXTABLE(from, to)						\
	" .pushsection \"__ex_table\",\"a\"\n"				\
	" .balign 16\n"							\
	" .quad (" #from ") - .\n"					\
	" .quad (" #to ") - .\n"					\
	" .popsection\n"

static inline int __put_user(void *dst, const void *src, unsigned int count)
{
#warning "The __put_user() API is a stub and should be re-implemented"

	memcpy(dst, src, count);

	return 0;
}

static inline int copy_to_user(void *dst, const void *src, unsigned int count)
{
#warning "The copy_to_user() API is a stub and should be re-implemented"

	int err = 0;

	if (unlikely(!is_user_rwaddr(dst, count))) {
		err = -EFAULT;
	} else {
		err = __put_user(dst, src, count);
	}

	return err;
}

static inline int __get_user(void *dst, const void *src, unsigned int count)
{
#warning "The __get_user() API is a stub and should be re-implemented"

	memcpy(dst, src, count);

	return 0;
}

static inline int copy_from_user(void *dst, const void *src,
                                 unsigned int count)
{
#warning "The copy_from_user() API is a stub and should be re-implemented"

	int err = 0;

	if (unlikely(!is_user_raddr((void *) src, count))) {
		err = -EFAULT;
	} else {
		err = __get_user(dst, src, count);
	}

	return err;
}

static inline uintptr_t ex_insn_addr(const struct extable_ip_fixup *x)
{
	return (uintptr_t) &x->insn + x->insn;
}

static inline uintptr_t ex_fixup_addr(const struct extable_ip_fixup *x)
{
	return (uintptr_t) &x->fixup + x->fixup;
}
