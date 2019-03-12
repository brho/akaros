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
#include <arch/fixup.h>

#define __m(x) *(x)

struct extable_ip_fixup {
	uint64_t insn;
	uint64_t fixup;
};

#define __read_msr_asm(eax, edx, addr, err, errret)			\
	asm volatile(ASM_STAC "\n"					\
	             "1:rdmsr\n"					\
	             "	mfence\n"					\
	             "2: " ASM_CLAC "\n"				\
	             ".section .fixup,\"ax\"\n"				\
	             "3:mov %4,%0\n"					\
	             "	jmp 2b\n"					\
	             ".previous\n"					\
	             _ASM_EXTABLE(1b, 3b)				\
	             : "=r" (err), "=d" (edx), "=a" (eax)		\
	             : "c" (addr), "i" (errret), "0" (err))

#define __write_msr_asm(val, addr, err, errret)				\
	asm volatile(ASM_STAC "\n"					\
	             "1:wrmsr\n"					\
	             "2: " ASM_CLAC "\n"				\
	             ".section .fixup,\"ax\"\n"				\
	             "3:mov %4,%0\n"					\
	             "	jmp 2b\n"					\
	             ".previous\n"					\
	             _ASM_EXTABLE(1b, 3b)				\
	             : "=r" (err)					\
	             : "d" ((uint32_t) (val >> 32)),			\
	               "a" ((uint32_t) (val & 0xffffffff)), "c" (addr),	\
	               "i" (errret), "0" (err))

#define __put_user_asm(x, addr, err, itype, rtype, ltype, errret)	\
	asm volatile(ASM_STAC "\n"					\
	             "1:mov"itype" %"rtype"1,%2\n"			\
	             "2: " ASM_CLAC "\n"				\
	             ".section .fixup,\"ax\"\n"				\
	             "3:mov %3,%0\n"					\
	             "	jmp 2b\n"					\
	             ".previous\n"					\
	             _ASM_EXTABLE(1b, 3b)				\
	             : "=r"(err)					\
	             : ltype(x), "m" (__m(addr)), "i" (errret), "0" (err))

#define __get_user_asm(x, addr, err, itype, rtype, ltype, errret)	\
	asm volatile(ASM_STAC "\n"					\
	             "1:mov"itype" %2,%"rtype"1\n"			\
	             "2: " ASM_CLAC "\n"				\
	             ".section .fixup,\"ax\"\n"				\
	             "3:mov %3,%0\n"					\
	             "	xor"itype" %"rtype"1,%"rtype"1\n"		\
	             "	jmp 2b\n"					\
	             ".previous\n"					\
	             _ASM_EXTABLE(1b, 3b)				\
	             : "=r" (err), ltype(x)				\
	             : "m" (__m(addr)), "i" (errret), "0" (err))

#define __user_memcpy(dst, src, count, err, errret)			\
	asm volatile(ASM_STAC "\n"					\
	             " 	cld\n"						\
	             "1:rep movsb\n"					\
	             "2: " ASM_CLAC "\n"				\
	             ".section .fixup,\"ax\"\n"				\
	             "3:mov %4,%0\n"					\
	             "	jmp 2b\n"					\
	             ".previous\n"					\
	             _ASM_EXTABLE(1b, 3b)				\
	             : "=r"(err), "+D" (dst), "+S" (src), "+c" (count)	\
	             : "i" (errret), "0" (err)				\
	             : "memory")

static inline int __put_user(void *dst, const void *src, unsigned int count)
{
	int err = 0;

	switch (count) {
	case 1:
		__put_user_asm(*(const uint8_t *) src, (uint8_t *) dst, err,
			       "b", "b", "iq", -EFAULT);
		break;
	case 2:
		__put_user_asm(*(const uint16_t *) src, (uint16_t *) dst, err,
			       "w", "w", "ir", -EFAULT);
		break;
	case 4:
		__put_user_asm(*(const uint32_t *) src, (uint32_t *) dst, err,
			       "l", "k", "ir", -EFAULT);
		break;
	case 8:
		__put_user_asm(*(const uint64_t *) src, (uint64_t *) dst, err,
			       "q", "", "er", -EFAULT);
		break;
	default:
		__user_memcpy(dst, src, count, err, -EFAULT);
	}

	return err;
}

static inline int copy_to_user(void *dst, const void *src, unsigned int count)
{
	int err = 0;

	if (unlikely(!is_user_rwaddr(dst, count))) {
		err = -EFAULT;
	} else if (!__builtin_constant_p(count)) {
		__user_memcpy(dst, src, count, err, -EFAULT);
	} else {
		err = __put_user(dst, src, count);
	}

	return err;
}

static inline int __get_user(void *dst, const void *src, unsigned int count)
{
	int err = 0;

	switch (count) {
	case 1:
		__get_user_asm(*(uint8_t *) dst, (const uint8_t *) src, err,
			       "b", "b", "=q", -EFAULT);
		break;
	case 2:
		__get_user_asm(*(uint16_t *) dst, (const uint16_t *) src, err,
			       "w", "w", "=r", -EFAULT);
		break;
	case 4:
		__get_user_asm(*(uint32_t *) dst, (const uint32_t *) src, err,
			       "l", "k", "=r", -EFAULT);
		break;
	case 8:
		__get_user_asm(*(uint64_t *) dst, (const uint64_t *) src, err,
			       "q", "", "=r", -EFAULT);
		break;
	default:
		__user_memcpy(dst, src, count, err, -EFAULT);
	}

	return err;
}

static inline int copy_from_user(void *dst, const void *src,
                                 unsigned int count)
{
	int err = 0;

	if (unlikely(!is_user_raddr((void *) src, count))) {
		err = -EFAULT;
	} else if (!__builtin_constant_p(count)) {
		__user_memcpy(dst, src, count, err, -EFAULT);
	} else {
		err = __get_user(dst, src, count);
	}

	return err;
}

static inline int read_msr_safe(uint32_t addr, uint64_t *value)
{
	int err = 0;
	uint32_t edx, eax;

	__read_msr_asm(eax, edx, addr, err, -EFAULT);
	if (likely(err == 0))
		*value = ((uint64_t) edx << 32) | eax;

	return err;
}

static inline int write_msr_safe(uint32_t addr, uint64_t value)
{
	int err = 0;

	__write_msr_asm(value, addr, err, -EFAULT);

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
