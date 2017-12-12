#pragma once

#include <stdint.h>

__BEGIN_DECLS

static inline uint8_t read_mmreg8(uintptr_t reg)
{
	return *((volatile uint8_t*)reg);
}

static inline uint8_t read_mmreg16(uintptr_t reg)
{
	return *((volatile uint16_t*)reg);
}

static inline uint32_t read_mmreg32(uintptr_t reg)
{
	return *((volatile uint32_t*)reg);
}

static inline uint64_t read_mmreg64(uintptr_t reg)
{
	return *((volatile uint64_t*)reg);
}

static inline void write_mmreg8(uintptr_t reg, uint8_t val)
{
	*((volatile uint8_t*)reg) = val;
}

static inline void write_mmreg16(uintptr_t reg, uint16_t val)
{
	*((volatile uint16_t*)reg) = val;
}

static inline void write_mmreg32(uintptr_t reg, uint32_t val)
{
	*((volatile uint32_t*)reg) = val;
}

static inline void write_mmreg64(uintptr_t reg, uint64_t val)
{
	*((volatile uint64_t*)reg) = val;
}

__END_DECLS
