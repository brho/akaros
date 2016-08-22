/* Generic I/O port emulation, based on MN10300 code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 *
 * Modified for Akaros (mostly just trimmed down and change b->8, w->16, etc).
 * Arches can't override these either.  I'm undecided about void* vs uintptr_t
 * for the addr parameter.
 */

#pragma once

#include <sys/types.h>
#include <endian.h>

#define __iomem
#define __force

/*
 * __raw_{read,write}{8,16,32,64}() access memory in native endianness.
 *
 * On some architectures memory mapped IO needs to be accessed differently.
 * On the simple architectures, we just read/write the memory location
 * directly.
 */
static inline uint8_t __raw_read8(const volatile uint8_t __iomem *addr)
{
	return *(const volatile uint8_t __force *)addr;
}

static inline uint16_t __raw_read16(const volatile uint16_t __iomem *addr)
{
	return *(const volatile uint16_t __force *)addr;
}

static inline uint32_t __raw_read32(const volatile uint32_t __iomem *addr)
{
	return *(const volatile uint32_t __force *)addr;
}

static inline uint64_t __raw_read64(const volatile uint64_t __iomem *addr)
{
	return *(const volatile uint64_t __force *)addr;
}

static inline void __raw_write8(uint8_t value, volatile uint8_t __iomem *addr)
{
	*(volatile uint8_t __force *)addr = value;
}

static inline void __raw_write16(uint16_t value,
                                 volatile uint16_t __iomem *addr)
{
	*(volatile uint16_t __force *)addr = value;
}

static inline void __raw_write32(uint32_t value,
                                 volatile uint32_t __iomem *addr)
{
	*(volatile uint32_t __force *)addr = value;
}

static inline void __raw_write64(uint64_t value,
                                 volatile uint64_t __iomem *addr)
{
	*(volatile uint64_t __force *)addr = value;
}

/*
 * {read,write}{8,16,32,64}() access little endian memory and return result in
 * native endianness.
 */
static inline uint8_t read8(const volatile uint8_t __iomem *addr)
{
	return __raw_read8(addr);
}

static inline uint16_t read16(const volatile uint16_t __iomem *addr)
{
	return le16_to_cpu(__raw_read16(addr));
}

static inline uint32_t read32(const volatile uint32_t __iomem *addr)
{
	return le32_to_cpu(__raw_read32(addr));
}

static inline uint64_t read64(const volatile uint64_t __iomem *addr)
{
	return le64_to_cpu(__raw_read64(addr));
}

static inline void write8(uint8_t value, volatile uint8_t __iomem *addr)
{
	__raw_write8(value, addr);
}

static inline void write16(uint16_t value, volatile uint16_t __iomem *addr)
{
	__raw_write16(cpu_to_le16(value), addr);
}

static inline void write32(uint32_t value, volatile uint32_t __iomem *addr)
{
	__raw_write32(cpu_to_le32(value), addr);
}

static inline void write64(uint64_t value, volatile uint64_t __iomem *addr)
{
	__raw_write64(cpu_to_le64(value), addr);
}
