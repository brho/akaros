/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <ros/common.h>
#include <compiler.h>
#include <kmalloc.h>
#include <kthread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <pmap.h>
#include <umem.h>
#include <smp.h>
#include <atomic.h>
#include <core_set.h>
#include <completion.h>
#include <arch/uaccess.h>
#include <arch/msr.h>

struct smp_read_values {
	const struct msr_address *msra;
	struct msr_value *msrv;
	atomic_t err;
};

struct smp_write_values {
	const struct msr_address *msra;
	const struct msr_value *msrv;
	atomic_t err;
};

static void msr_smp_read(void *opaque)
{
	struct smp_read_values *srv = (struct smp_read_values *) opaque;
	int err, coreno = core_id();
	uint32_t addr;
	uint64_t value;

	err = msr_get_core_address(coreno, srv->msra, &addr);
	if (likely(!err)) {
		err = safe_read_msr(addr, &value);
		if (likely(!err))
			err = msr_set_core_value(coreno, value, srv->msrv);
	}
	if (unlikely(err))
		atomic_cas(&srv->err, 0, err);
}

int msr_cores_read(const struct core_set *cset, const struct msr_address *msra,
                   struct msr_value *msrv)
{
	int err;
	struct smp_read_values srv;

	ZERO_DATA(srv);
	srv.msra = msra;
	srv.msrv = msrv;
	atomic_init(&srv.err, 0);
	smp_do_in_cores(cset, msr_smp_read, &srv);

	return (int) atomic_read(&srv.err);
}

int msr_core_read(unsigned int coreno, uint32_t addr, uint64_t *value)
{
	int err;
	struct core_set cset;
	struct msr_address msra;
	struct msr_value msrv;

	core_set_init(&cset);
	core_set_setcpu(&cset, coreno);
	msr_set_address(&msra, addr);
	msr_set_values(&msrv, NULL, 0);
	err = msr_cores_read(&cset, &msra, &msrv);
	*value = msrv.value;

	return err;
}

static void msr_smp_write(void *opaque)
{
	struct smp_write_values *swv = (struct smp_write_values *) opaque;
	int err, coreno = core_id();
	uint32_t addr;
	uint64_t value;

	err = msr_get_core_address(coreno, swv->msra, &addr);
	if (likely(!err)) {
		err = msr_get_core_value(coreno, swv->msrv, &value);
		if (likely(!err))
			err = safe_write_msr(addr, value);
	}
	if (unlikely(err))
		atomic_cas(&swv->err, 0, err);
}

int msr_cores_write(const struct core_set *cset, const struct msr_address *msra,
                    const struct msr_value *msrv)
{
	struct smp_write_values swv;

	ZERO_DATA(swv);
	swv.msra = msra;
	swv.msrv = msrv;
	atomic_init(&swv.err, 0);
	smp_do_in_cores(cset, msr_smp_write, &swv);

	return (int) atomic_read(&swv.err);
}

int msr_core_write(unsigned int coreno, uint32_t addr, uint64_t value)
{
	struct core_set cset;
	struct msr_address msra;
	struct msr_value msrv;

	core_set_init(&cset);
	core_set_setcpu(&cset, coreno);
	msr_set_address(&msra, addr);
	msr_set_value(&msrv, value);

	return msr_cores_write(&cset, &msra, &msrv);
}
