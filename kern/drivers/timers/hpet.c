/* Copyright (c) 2020 Google Inc
 * Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * HPET nonsense */

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <endian.h>
#include <pmap.h>
#include <acpi.h>
#include <kmalloc.h>

#include "hpet.h"

#define HPET_BLOCK_LEN		1024

#define HPET_CAP_ID		0x00
#define HPET_CONFIG		0x10
#define HPET_IRQ_STS		0x20
#define HPET_MAIN_COUNTER	0xf0

#define HPET_CONF_LEG_RT_CNF	(1 << 1)
#define HPET_CONF_ENABLE_CNF	(1 << 0)

#define HPET_TIMER_CONF		0x00
#define HPET_TIMER_COMP		0x08
#define HPET_TIMER_FSB		0x10

#define HPET_TN_INT_TYPE_CNF	(1 << 1)
#define HPET_TN_INT_ENB_CNF	(1 << 2)
#define HPET_TN_TYPE_CNF	(1 << 3)
#define HPET_TN_PER_INT_CAP	(1 << 4)
#define HPET_TN_SIZE_CAP	(1 << 5)
#define HPET_TN_VAL_SET_CNF	(1 << 6)
#define HPET_TN_32MODE_CNF	(1 << 8)
#define HPET_TN_INT_ROUTE_CNF	(0x1f << 9)
#define HPET_TN_FSB_EN_CNF	(1 << 14)
#define HPET_TN_FSB_INT_DEL_CAP	(1 << 15)
#define HPET_TN_INT_ROUTE_CAP	(0xffffffffULL << 32)

static struct hpet_block *gbl_hpet;

/* The HPET likes 64bit mmreg reads and writes.  If the arch doesn't support
 * them, then things are a little trickier.  Probably just replace these with
 * mm64 ops, and quit supporting 32 bit. */
static inline void hpet_w64(uintptr_t reg, uint64_t val)
{
	*((volatile uint64_t*)reg) = val;
}

static inline uint64_t hpet_r64(uintptr_t reg)
{
	return *((volatile uint64_t*)reg);
}

void hpet_timer_enable(struct hpet_timer *ht)
{
	hpet_w64(ht->base + HPET_TIMER_CONF, ht->enable_cmd);
}

void hpet_timer_disable(struct hpet_timer *ht)
{
	hpet_w64(ht->base + HPET_TIMER_CONF, 0);
}

/* This only works on 64 bit counters, where we don't have to deal with
 * wrap-around. */
bool hpet_check_spurious_64(struct hpet_timer *ht)
{
	return hpet_r64(ht->hpb->base + HPET_MAIN_COUNTER) <
		hpet_r64(ht->base + HPET_TIMER_COMP);
}

/* There is no upper addr!  But o/w, this is basically MSI.  To be fair, we zero
 * out the upper addr in msi.c too. */
static uint64_t fsb_make_addr(uint8_t dest)
{
	return 0xfee00000 | (dest << 12);
}

/* dmode: e.g. 000 = fixed, 100 = NMI.  Assuming edge triggered */
static uint64_t fsb_make_data(uint8_t vno, uint8_t dmode)
{
	return (dmode << 8) | vno;
}

/* This is a very limited HPET timer, primarily used by the watchdog.
 *
 * It's a one-shot (non-periodic), FSB, 64-bit, edge-triggered timer for Core 0
 * with vector vno and delivery mode dmode.
 *
 * Why so specific?  Okay, the HPET is a piece of shit, at least on my machine.
 * If you disable the interrupt, and then the time comes where MAIN == COMP, the
 * IRQ will be suppressed, but when you enable later on, the IRQ will fire.  No
 * way around it that I can find.
 *
 * One trick is to set the COMP to some time that won't fire, i.e.  in the past.
 * However, the 32 bit counters are only about a 5 minute reach.  So this trick
 * only works with the 64 bit counter.
 *
 * However, even with that, if the IRQ ever legitimately fires, any time you
 * reenable the timer, it triggers an IRQ.
 *
 * This is all with FSB (which has to be edge triggered) interrupt styles.  I
 * tried various combos of "write to the IRQ_STS register", change certain
 * fields in timer CONF, disable the global counter while making changes, etc.
 * All things that aren't in the book, but that might clear whatever internal
 * bit is set.
 *
 * Ultimately, I opted to handle the 'spurious' interrupt in SW, though with a 5
 * minute reach, you can't tell between an old value and a new one.  Unless you
 * use a 64 bit counter - then wraparound isn't a concern.  If we wanted to do
 * this for a 32 bit counter, we'd need to drastically limit the reach. */
void hpet_magic_timer_setup(struct hpet_timer *ht, uint8_t vno, uint8_t dmode)
{
	/* Unlike the other reserved bits in the hpb's registers, the spec says
	 * that timer (ht) conf reserved bits should be set to 0.
	 *
	 * In lieu of screwing around too much, we just set the entire register
	 * in one shot.  Disabled = 0, enabled = all bits needed.  The
	 * disastrous behavior mentioned above occurs regardless of the
	 * technique used. */
	hpet_timer_disable(ht);

	/* Core 0, fixed, with vno */
	hpet_w64(ht->base + HPET_TIMER_FSB,
		 (fsb_make_addr(0) << 32) | fsb_make_data(vno, dmode));

	ht->enable_cmd = HPET_TN_FSB_EN_CNF | HPET_TN_INT_ENB_CNF;
}

/* Sets the time to fire X ns in the future.  No guarantees or sanity checks.
 * If we get delayed setting this, the main counter may pass by our ticks value
 * before we write it, and you'll never get it. */
void hpet_timer_increment_comparator(struct hpet_timer *ht, uint64_t nsec)
{
	uint64_t ticks;

	ticks = hpet_r64(ht->hpb->base + HPET_MAIN_COUNTER);
	ticks += nsec / ht->hpb->nsec_per_tick;
	hpet_w64(ht->base + HPET_TIMER_COMP, ticks);
}

/* See above.  Need 64 bit and FSB */
struct hpet_timer *hpet_get_magic_timer(void)
{
	struct hpet_block *hpb = gbl_hpet;
	struct hpet_timer *ret = NULL;

	if (!hpb)
		return NULL;
	spin_lock(&hpb->lock);
	for (int i = 0; i < hpb->nr_timers; i++) {
		struct hpet_timer *ht = &hpb->timers[i];

		if (ht->in_use)
			continue;
		if (!ht->fsb)
			continue;
		if (!ht->bit64)
			continue;
		ht->in_use = true;
		ret = ht;
		break;
	}
	spin_unlock(&hpb->lock);
	return ret;
}

void hpet_put_timer(struct hpet_timer *ht)
{
	struct hpet_block *hpb = gbl_hpet;

	if (!hpb)
		return;
	spin_lock(&hpb->lock);
	ht->in_use = false;
	spin_unlock(&hpb->lock);
}

static void print_hpb_stats(struct hpet_block *hpb)
{
	printk("HPET at %p:\n", hpb->base);
	printk("\tVendor: 0x%x\n", (hpb->cap_id >> 16) & 0xffff);
	printk("\tPeriod: 0x%08x\n", hpb->period);
	printk("\t32 bit reach: %d sec\n", hpb->reach32);
	printk("\tTimers: %d\n", hpb->nr_timers);
	printk("\tMain counter size: %d\n", hpb->cap_id & (1 << 13) ? 64 : 32);

	printd("\tcap/id %p\n", hpb->cap_id);
	printd("\tconfig %p\n", hpet_r64(hpb->base + HPET_CONFIG));
	printd("\tirqsts %p\n", hpet_r64(hpb->base + HPET_IRQ_STS));

	for (int i = 0; i < hpb->nr_timers; i++) {
		printk("\t\tTimer %d: conf %p comp %p, %d bit, %sFSB\n", i,
		       hpet_r64(hpb->timers[i].base + HPET_TIMER_CONF),
		       hpet_r64(hpb->timers[i].base + HPET_TIMER_COMP),
		       hpb->timers[i].bit64 ? 64 : 32,
		       hpb->timers[i].fsb ? "" : "no ");
	}
}

struct Atable *parsehpet(struct Atable *parent,
                         char *name, uint8_t *raw, size_t rawsize)
{
	struct hpet_block *hpb;
	struct Atable *hpet;
	uint32_t evt_blk_id;

	/* Only dealing with one block of these. */
	if (gbl_hpet) {
		printk("Found another HPET, skipping!\n");
		return NULL;
	}

	/* Do we want to keep this table around?  if so, we can use newtable,
	 * which allocs an Atable and puts it on a global stailq.  then we
	 * return that pointer, not as an addr, but as a signal to parse code
	 * about whether or not it is safe to unmap (which we don't do anymore).
	 */
	hpet = mkatable(parent, HPET, "HPET", raw, rawsize, 0);
	assert(hpet);
	printk("HPET table detected at %p, for %d bytes\n", raw, rawsize);

	evt_blk_id = l32get(raw + 36);

	hpb = kzmalloc(sizeof(*hpb), MEM_WAIT);
	spinlock_init(&hpb->lock);

	hpb->base = vmap_pmem_nocache(l64get(raw + 44), HPET_BLOCK_LEN);
	if (!hpb->base) {
		printk("HPET failed to get an iomapping, aborting\n");
		kfree(hpb);
		kfree(hpet);
		return NULL;
	}

	hpb->cap_id = hpet_r64(hpb->base + HPET_CAP_ID);
	if (evt_blk_id != (hpb->cap_id & 0xffffffff)) {
		printk("HPET ACPI mismatch: ACPI: %p HPET: %p\n", evt_blk_id,
		       hpb->cap_id);
	}
	hpb->nr_timers = ((hpb->cap_id >> 8) & 0xf) + 1;

	/* femtoseconds (10E-15) per tick.
	 * e.g. 69 ns per tick.
	 * freq is just 10^15 / period: 14.318 MHz
	 * reach of 32 bit counter:
	 * period fs/tick * 1sec/10^15fs * 2^32tick/wrap ~= 299 seconds */
	hpb->period = hpb->cap_id >> 32;
	hpb->nsec_per_tick = hpb->period / 1000000;
	hpb->reach32 = hpb->period * (1ULL << 32) / 1000000000000000ULL;

	for (int i = 0; i < hpb->nr_timers; i++) {
		struct hpet_timer *ht = &hpb->timers[i];
		uint64_t conf;

		ht->base = hpb->base + 0x100 + 0x20 * i;
		ht->hpb = hpb;
		conf = hpet_r64(ht->base + HPET_TIMER_CONF);
		ht->bit64 = conf & HPET_TN_SIZE_CAP;
		ht->fsb = conf & HPET_TN_FSB_INT_DEL_CAP;
		hpet_timer_disable(ht);
	}

	/* No interest in legacy mode. */
	hpet_w64(hpb->base + HPET_CONFIG,
		 hpet_r64(hpb->base + HPET_CONFIG) & HPET_CONF_LEG_RT_CNF);
	/* All timers are off currently */
	hpet_w64(hpb->base + HPET_CONFIG,
		 hpet_r64(hpb->base + HPET_CONFIG) | HPET_CONF_ENABLE_CNF);

	gbl_hpet = hpb;

	return finatable_nochildren(hpet);
}

void cmos_dumping_ground(void)
{
	uint8_t cmos_b;

	/* this stuff tries to turn off various cmos / RTC timer bits.  keeping
	 * around if we need to disable the RTC alarm.  note that the HPET
	 * replaces the RTC periodic function (where available), and in those
	 * cases the RTC alarm function is implemented with SMM. */
	outb(0x70, 0xb);
	cmos_b = inb(0x71);
	printk("cmos b 0x%02x\n", cmos_b);

	cmos_b &= ~((1 << 5) | (1 << 6));
	outb(0x70, 0xb);
	outb(0x71, cmos_b);

	outb(0x70, 0xb);
	cmos_b = inb(0x71);
	printk("cmos b 0x%02x\n", cmos_b);
}
