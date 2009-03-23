/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_KERN_APIC_H
#define ROS_KERN_APIC_H

/* 
 * Functions and definitions for dealing with the APIC and PIC, specific to
 * Intel.  Does not handle an x2APIC.
 */

#include <inc/mmu.h>
#include <inc/x86.h>

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
// IPI Interrupt Command Register
#define LAPIC_IPI_ICR_LOWER			(LAPIC_BASE + 0x300)
#define LAPIC_IPI_ICR_UPPER			(LAPIC_BASE + 0x310)

// IOAPIC
#define IOAPIC_BASE					0xfec00000 // this is the default, can be changed

void remap_pic(void);
void lapic_set_timer(uint32_t ticks, uint8_t vector, bool periodic);
uint32_t lapic_get_default_id(void);

static inline void pic_send_eoi(uint32_t irq);
static inline void lapic_send_eoi(void);
static inline uint32_t lapic_get_version(void);
static inline uint32_t lapic_get_error(void);
static inline uint32_t lapic_get_id(void);
static inline void lapic_disable(void);
static inline void lapic_enable(void);
static inline void lapic_wait_to_send(void);
static inline void send_init_ipi(void);
static inline void send_startup_ipi(uint8_t vector);
static inline void send_self_ipi(uint8_t vector);
static inline void send_broadcast_ipi(uint8_t vector);
static inline void send_all_others_ipi(uint8_t vector);
static inline void send_ipi(uint8_t dest, bool logical_mode, uint8_t vector);

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
		asm volatile("pause");
}

static inline void lapic_enable(void)
{
	write_mmreg32(LAPIC_SPURIOUS, read_mmreg32(LAPIC_SPURIOUS) | 0x00001000);
}

static inline void send_init_ipi(void)
{
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4500);
}

static inline void send_startup_ipi(uint8_t vector)
{
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4600 | vector);
}

static inline void send_self_ipi(uint8_t vector)
{
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00044000 | vector);
}

static inline void send_broadcast_ipi(uint8_t vector)
{
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00084000 | vector);
}

static inline void send_all_others_ipi(uint8_t vector)
{
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x000c4000 | vector);
}

static inline void send_ipi(uint8_t dest, bool logical_mode, uint8_t vector)
{
	write_mmreg32(LAPIC_IPI_ICR_UPPER, dest << 24);
	write_mmreg32(LAPIC_IPI_ICR_LOWER, 0x00004000 | (logical_mode << 11) | vector);
}

/* To change the LAPIC Base (not recommended):
	msr_val = read_msr(IA32_APIC_BASE);
	msr_val = msr_val & ~MSR_APIC_BASE_ADDRESS | 0xfaa00000;
	write_msr(IA32_APIC_BASE, msr_val);
*/
#endif /* ROS_KERN_APIC_H */
