#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <endian.h>
#include <pmap.h>
#include <acpi.h>

#include "hpet.h"

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

struct Atable *parsehpet(struct Atable *parent,
                         char *name, uint8_t *raw, size_t rawsize)
{
	/* Do we want to keep this table around?  if so, we can use newtable,
	 * which allocs an Atable and puts it on a global stailq.  then we
	 * return that pointer, not as an addr, but as a signal to parse code
	 * about whether or not it is safe to unmap (which we don't do anymore).
	 */
	struct Atable *hpet = mkatable(parent, HPET, "HPET", raw, rawsize, 0);
	unsigned long hp_addr;
	uint32_t evt_blk_id;
	int nr_timers;

	assert(hpet);
	printk("HPET table detected at %p, for %d bytes\n", raw, rawsize);

	evt_blk_id = l32get(raw + 36);
	printd("EV BID 0x%08x\n", evt_blk_id);

	hp_addr = (unsigned long)KADDR_NOCHECK(l64get(raw + 44));

	printd("cap/ip %p\n", hpet_r64(hp_addr + 0x00));
	printd("config %p\n", hpet_r64(hp_addr + 0x10));
	printd("irqsts %p\n", hpet_r64(hp_addr + 0x20));

	nr_timers = ((hpet_r64(hp_addr) >> 8) & 0xf) + 1;
	for (int i = 0; i < nr_timers; i++)
		printd("Timer %d, config reg %p\n", i,
		       hpet_r64(hp_addr + 0x100 + 0x20 * i));
	/* 0x10, general config register.  bottom two bits are legacy mode and
	 * global enable.  turning them both off.  need to do read-modify-writes
	 * to HPET registers with reserved fields.*/
	hpet_w64(hp_addr + 0x10, hpet_r64(hp_addr + 0x10) & ~0x3);
	printk("Disabled the HPET timer\n");

	return finatable_nochildren(hpet);
}

void cmos_dumping_ground()
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
