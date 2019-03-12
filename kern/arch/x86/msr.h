/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <sys/types.h>
#include <ros/errno.h>
#include <stdio.h>
#include <string.h>
#include <core_set.h>

/* The msr_address and msr_value structures allow to specify either a single
 * address/value for all cores, or dedicated ones for every core.
 * This allow a single logical call to msr_cores_read()/msr_cores_write() APIs,
 * to read/write MSRs at different addresses and using different values.
 */
struct msr_address {
	uint32_t addr;
	const uint32_t *addresses;
	size_t num_addresses;
};

struct msr_value {
	uint64_t value;
	uint64_t *values;
	size_t num_values;
};

int msr_cores_read(const struct core_set *cset, const struct msr_address *msra,
		   struct msr_value *msrv);
int msr_core_read(unsigned int core, uint32_t addr, uint64_t *value);
int msr_cores_write(const struct core_set *cset, const struct msr_address *msra,
		    const struct msr_value *msrv);
int msr_core_write(unsigned int core, uint32_t addr, uint64_t value);

static inline void msr_set_address(struct msr_address *msra, uint32_t addr)
{
	ZERO_DATA(*msra);
	msra->addr = addr;
}

static inline void msr_set_addresses(struct msr_address *msra,
				     const uint32_t *addresses,
				     size_t num_addresses)
{
	ZERO_DATA(*msra);
	msra->addresses = addresses;
	msra->num_addresses = num_addresses;
}

static inline int msr_get_core_address(unsigned int coreno,
				       const struct msr_address *msra,
				       uint32_t *paddr)
{
	if (msra->addresses != NULL) {
		if (coreno >= (unsigned int) msra->num_addresses)
			return -ERANGE;

		*paddr = msra->addresses[coreno];
	} else {
		*paddr = msra->addr;
	}

	return 0;
}

static inline void msr_set_value(struct msr_value *msrv, uint64_t value)
{
	ZERO_DATA(*msrv);
	msrv->value = value;
}

static inline void msr_set_values(struct msr_value *msrv,
				  const uint64_t *values, size_t num_values)
{
	ZERO_DATA(*msrv);

	/* Avoid supporting two APIs, one for setting const values, and one for
	 * setting the non const ones.
	 */
	msrv->values = (uint64_t *) values;
	msrv->num_values = num_values;
}

static inline int msr_set_core_value(unsigned int coreno, uint64_t value,
				     struct msr_value *msrv)
{
	if (msrv->values != NULL) {
		if (coreno >= (unsigned int) msrv->num_values)
			return -ERANGE;

		msrv->values[coreno] = value;
	} else {
		msrv->value = value;
	}

	return 0;
}

static inline int msr_get_core_value(unsigned int coreno,
				     const struct msr_value *msrv,
				     uint64_t *pvalue)
{
	if (msrv->values != NULL) {
		if (coreno >= (unsigned int) msrv->num_values)
			return -ERANGE;

		*pvalue = msrv->values[coreno];
	} else {
		*pvalue = msrv->value;
	}

	return 0;
}
