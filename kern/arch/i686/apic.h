/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#ifndef ROS_KERN_APIC_H
#define ROS_KERN_APIC_H

/* 
 * Functions and definitions for dealing with the APIC and PIC, specific to
 * Intel.  Does not handle an x2APIC.
 */

#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/ioapic.h>

// PIC
#define PIC1_CMD					0x20
#define PIC1_DATA					0x21
#define PIC2_CMD					0xA0
#define PIC2_DATA					0xA1
// These are also hardcoded into the IRQ_HANDLERs of kern/trapentry.S
#define PIC1_OFFSET					0x20
#define PIC2_OFFSET					0x28
#define PIC_EOI						0x20

// Local APIC
#define LAPIC_BASE					0xfee00000 // this is the default, can be changed
#define LAPIC_EOI					(LAPIC_BASE + 0x0b0)
#define LAPIC_SPURIOUS				(LAPIC_BASE + 0x0f0)
#define LAPIC_VERSION				(LAPIC_BASE + 0x030)
#define LAPIC_ERROR					(LAPIC_BASE + 0x280)
#define LAPIC_ID					(LAPIC_BASE + 0x020)
#define LAPIC_LOGICAL_ID			(LAPIC_BASE + 0x0d0)
// LAPIC Local Vector Table
#define LAPIC_LVT_TIMER				(LAPIC_BASE + 0x320)
#define LAPIC_LVT_LINT0				(LAPIC_BASE + 0x350)
#define LAPIC_LVT_LINT1				(LAPIC_BASE + 0x360)
#define LAPIC_LVT_ERROR				(LAPIC_BASE + 0x370)
#define LAPIC_LVT_PERFMON			(LAPIC_BASE + 0x340)
#define LAPIC_LVT_THERMAL			(LAPIC_BASE + 0x330)
#define LAPIC_LVT_MASK				0x00010000
// LAPIC Timer
#define LAPIC_TIMER_INIT			(LAPIC_BASE + 0x380)
#define LAPIC_TIMER_CURRENT			(LAPIC_BASE + 0x390)
#define LAPIC_TIMER_DIVIDE			(LAPIC_BASE + 0x3e0)
#define LAPIC_TIMER_DEFAULT_VECTOR	0xeb
#define LAPIC_TIMER_DEFAULT_DIVISOR	0xa // This is 128.  Ref SDM 3.a 9.6.4
// IPI Interrupt Command Register
#define LAPIC_IPI_ICR_LOWER			(LAPIC_BASE + 0x300)
#define LAPIC_IPI_ICR_UPPER			(LAPIC_BASE + 0x310)

// PIT (Programmable Interval Timer)
#define	TIMER_REG_CNTR0	0	/* timer 0 counter port */
#define	TIMER_REG_CNTR1	1	/* timer 1 counter port */
#define	TIMER_REG_CNTR2	2	/* timer 2 counter port */
#define	TIMER_REG_MODE	3	/* timer mode port */
#define	TIMER_SEL0	0x00	/* select counter 0 */
#define	TIMER_SEL1	0x40	/* select counter 1 */
#define	TIMER_SEL2	0x80	/* select counter 2 */
#define	TIMER_INTTC	0x00	/* mode 0, intr on terminal cnt */
#define	TIMER_ONESHOT	0x02	/* mode 1, one shot */
#define	TIMER_RATEGEN	0x04	/* mode 2, rate generator */
#define	TIMER_SQWAVE	0x06	/* mode 3, square wave */
#define	TIMER_SWSTROBE	0x08	/* mode 4, s/w triggered strobe */
#define	TIMER_HWSTROBE	0x0a	/* mode 5, h/w triggered strobe */
#define	TIMER_LATCH	0x00	/* latch counter for reading */
#define	TIMER_LSB	0x10	/* r/w counter LSB */
#define	TIMER_MSB	0x20	/* r/w counter MSB */
#define	TIMER_16BIT	0x30	/* r/w counter 16 bits, LSB first */
#define	TIMER_BCD	0x01	/* count in BCD */

#define PIT_FREQ 					1193182

#define IO_TIMER1   0x40        /* 8253 Timer #1 */
#define TIMER_CNTR0 (IO_TIMER1 + TIMER_REG_CNTR0)
#define TIMER_CNTR1 (IO_TIMER1 + TIMER_REG_CNTR1)
#define TIMER_CNTR2 (IO_TIMER1 + TIMER_REG_CNTR2)
#define TIMER_MODE  (IO_TIMER1 + TIMER_REG_MODE)

typedef struct system_timing {
	uint64_t tsc_freq;
	uint64_t bus_freq;
	uint16_t pit_divisor;
	uint8_t pit_mode;
} system_timing_t;

extern system_timing_t system_timing;

void pic_remap(void);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void __lapic_set_timer(uint32_t ticks, uint8_t vec, bool periodic, uint8_t div);
void lapic_set_timer(uint32_t usec, bool periodic);
uint32_t lapic_get_default_id(void);
// PIT related
void pit_set_timer(uint32_t freq, uint32_t mode);
void timer_init(void);
void udelay_pit(uint64_t usec);
// TODO: right now timer defaults to TSC
uint64_t gettimer(void);
uint64_t getfreq(void);

static inline void pic_send_eoi(uint32_t irq);
static inline void lapic_send_eoi(void);
static inline uint32_t lapic_get_version(void);
static inline uint32_t lapic_get_error(void);
static inline uint32_t lapic_get_id(void);
static inline void lapic_set_id(uint8_t id); // Careful, may not actually work
static inline uint8_t lapic_get_logid(void);
static inline void lapic_set_logid(uint8_t id);
static inline void lapic_disable_timer(void);
static inline void lapic_disable(void);
static inline void lapic_enable(void);
static inline void lapic_wait_to_send(void);
static inline void send_init_ipi(void);
static inline void send_startup_ipi(uint8_t vector);
static inline void send_self_ipi(uint8_t vector);
static inline void send_broadcast_ipi(uint8_t vector);
static inline void send_all_others_ipi(uint8_t vector);
static inline void send_ipi(uint8_t hw_coreid, uint8_t vector);
static inline void send_group_ipi(uint8_t hw_groupid, uint8_t vector);

#define mask_lapic_lvt(entry) \
	write_mmreg32(entry, read_mmreg32(entry) | LAPIC_LVT_MASK)
#define unmask_lapic_lvt(entry) \
	write_mmreg32(entry, read_mmreg32(entry) & ~LAPIC_LVT_MASK)

static inline void pic_send_eoi(uint32_t irq)
{
	// all irqs beyond the first seven need to be chained to the slave
	if (irq > 7)
		outb(PIC2_CMD, PIC_EOI);
	outb(PIC1_CMD, PIC_EOI);
}

static inline void lapic_send_eoi(void)
{
	write_mmreg32(LAPIC_EOI, 0);
}

static inline uint32_t lapic_get_version(void)
{
	return read_mmreg32(LAPIC_VERSION);	
}

static inline uint32_t lapic_get_error(void)
{
	write_mmreg32(LAPIC_ERROR, 0xdeadbeef);
	return read_mmreg32(LAPIC_ERROR);
}

static inline uint32_t lapic_get_id(void)
{
	return read_mmreg32(LAPIC_ID) >> 24;
}

static inline void lapic_set_id(uint8_t id)
{
	write_mmreg32(LAPIC_ID, id << 24);
}

static inline uint8_t lapic_get_logid(void)
{
	return read_mmreg32(LAPIC_LOGICAL_ID) >> 24;
}

static inline void lapic_set_logid(uint8_t id)
{
	write_mmreg32(LAPIC_LOGICAL_ID, id << 24);
}

static inline void lapic_disable_timer(void)
{
	write_mmreg32(LAPIC_LVT_TIMER, 0);
}

/* There are a couple ways to do it.  The MSR route doesn't seem to work
 * in KVM.  It's also a somewhat permanent thing
 */
static inline void lapic_disable(void)
{
	write_mmreg32(LAPIC_SPURIOUS, read_mmreg32(LAPIC_SPURIOUS) & 0xffffefff);
	//write_msr(IA32_APIC_BASE, read_msr(IA32_APIC_BASE) & ~MSR_APIC_ENABLE);
}

/* Spins until previous IPIs are delivered.  Not sure if we want it inlined
 * Also not sure when we really need to do this. 
 */
static inline void lapic_wait_to_send(void)
{
	while(read_mmreg32(LAPIC_IPI_ICR_LOWER) & 0x1000)
		__cpu_relax();
}

static inline void lapic_enable(void)
{
	write_mmreg32(LAPIC_SPURIOUS, read_mmreg32(LAPIC_SPURIOUS) | 0x00000100);
}

static inline void send_init_ipi(void)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4500);
}

static inline void send_startup_ipi(uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4600 | vector);
}

static inline void send_self_ipi(uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00044000 | vector);
}

static inline void send_broadcast_ipi(uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00084000 | vector);
}

static inline void send_all_others_ipi(uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4000 | vector);
}

static inline void __send_ipi(uint8_t hw_coreid, uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_UPPER, hw_coreid << 24);
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00004000 | vector);
}

static inline void send_ipi(uint8_t hw_coreid, uint8_t vector)
{
	/* 255 is a broadcast, which should use send_broadcast_ipi, and it is also
	 * what would come in if you tried sending an IPI to an os_coreid that
	 * doesn't exist (since they are initialized to -1). */
	if (hw_coreid == 255)
		return;
	__send_ipi(hw_coreid, vector);
}

static inline void send_group_ipi(uint8_t hw_groupid, uint8_t vector)
{
	lapic_wait_to_send();
	write_mmreg32(LAPIC_IPI_ICR_UPPER, hw_groupid << 24);
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00004800 | vector);
}

/* To change the LAPIC Base (not recommended):
	msr_val = read_msr(IA32_APIC_BASE);
	msr_val = msr_val & ~MSR_APIC_BASE_ADDRESS | 0xfaa00000;
	write_msr(IA32_APIC_BASE, msr_val);
*/
#endif /* ROS_KERN_APIC_H */
