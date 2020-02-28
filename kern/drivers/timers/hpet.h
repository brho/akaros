/* Copyright (c) 2020 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * HPET nonsense */

#pragma once

#include <acpi.h>
#include <atomic.h>

struct hpet_timer {
	uintptr_t base;
	uint64_t enable_cmd;
	bool bit64;
	bool fsb;
	bool in_use;
	struct hpet_block *hpb;
};

struct hpet_block {
	spinlock_t lock;
	uintptr_t base;
	uint64_t cap_id;
	uint32_t period;
	uint32_t nsec_per_tick;
	uint32_t reach32;
	unsigned int nr_timers;
	struct hpet_timer timers[32];
};

struct hpet_timer *hpet_get_magic_timer(void);
void hpet_put_timer(struct hpet_timer *ht);

void hpet_timer_enable(struct hpet_timer *ht);
void hpet_timer_disable(struct hpet_timer *ht);
bool hpet_check_spurious_64(struct hpet_timer *ht);
void hpet_magic_timer_setup(struct hpet_timer *ht, uint8_t vno, uint8_t dmode);
void hpet_timer_increment_comparator(struct hpet_timer *ht, uint64_t nsec);

struct Atable *parsehpet(struct Atable *parent,
                         char *name, uint8_t *p, size_t rawsize);
