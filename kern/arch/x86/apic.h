/*
 * Copyright (c) 2009 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#pragma once

/*
 * Functions and definitions for dealing with the APIC and PIC, specific to
 * Intel.  Does not handle an x2APIC.
 */
#include <arch/mmu.h>
#include <arch/x86.h>
#include <ros/trapframe.h>
#include <atomic.h>
#include <endian.h>
#include <arch/ros/msr-index.h>

// Local APIC
/* PBASE is the physical address.  It is mapped in at the VADDR LAPIC_BASE.
 * 64 bit note: it looks like this is mapped to the same place in 64 bit address
 * spaces.  We just happen to have a slight 'hole' in addressable physical
 * memory.  We can move the PBASE, but we're limited to 32 bit (physical)
 * addresses. */
#define LAPIC_PBASE		0xfee00000	/* default *physical* address */
#define LAPIC_LVT_MASK		0x00010000

/* Quick note on the divisor.  The LAPIC timer ticks once per divisor-bus ticks
 * (system bus or APIC bus, depending on the model).  Ex: A divisor of 128 means
 * 128 bus ticks results in 1 timer tick.  The divisor increases the time range
 * and decreases the granularity of the timer.  Numbers are appx, based on 4
 * billion ticks, vs 2^32 ticks.
 * Ex:   1GHz bus, div 001:    4sec max,    1ns granularity
 * Ex:   1GHz bus, div 128:  512sec max,  128ns granularity
 * Ex: 100MHz bus, div 001:   40sec max,   10ns granularity
 * Ex: 100MHz bus, div 128: 5120sec max, 1280ns granularity */
#define LAPIC_TIMER_DIVISOR_VAL		32	/* seems reasonable */
#define LAPIC_TIMER_DIVISOR_BITS	0x8	/* Div = 32 */

// IPI Interrupt Command Register
#define LAPIC_IPI_ICR_LOWER	0x30
#define LAPIC_IPI_ICR_UPPER	0x31
/* Interrupts being serviced (in-service) and pending (interrupt request reg).
 * Note these registers are not normal bitmaps, but instead are 8 separate
 * 32-bit registers, spaced/aligned on 16 byte boundaries in the LAPIC address
 * space. */
#define LAPIC_ISR		0x10
#define LAPIC_IRR		0x20
#define LAPIC_DFR		0x0e

struct irq_handler;	/* include loops */

bool lapic_check_spurious(int trap_nr);
bool lapic_get_isr_bit(uint8_t vector);
bool lapic_get_irr_bit(uint8_t vector);
void lapic_print_isr(void);
void lapic_mask_irq(struct irq_handler *unused, int apic_vector);
void lapic_unmask_irq(struct irq_handler *unused, int apic_vector);
bool ipi_is_pending(uint8_t vector);
void __lapic_set_timer(uint32_t ticks, uint8_t vec, bool periodic, uint8_t div);
void lapic_set_timer(uint32_t usec, bool periodic);
uint32_t lapic_get_default_id(void);
int apiconline(void);
void handle_lapic_error(struct hw_trapframe *hw_tf, void *data);
uint32_t apicrget(uint64_t r);
void apicrput(uint64_t r, uint32_t data);
void apicsendipi(uint64_t data);
void apic_isr_dump(void);
void apic_irr_dump(void);

static inline void lapic_send_eoi(int unused);
static inline uint32_t lapic_get_version(void);
static inline uint32_t lapic_get_error(void);
static inline uint32_t lapic_get_id(void);
static inline uint8_t lapic_get_logid(void);
static inline void lapic_disable_timer(void);
static inline void lapic_disable(void);
static inline void lapic_enable(void);
static inline void send_init_ipi(void);
static inline void send_startup_ipi(uint8_t vector);
static inline void send_self_ipi(uint8_t vector);
static inline void send_broadcast_ipi(uint8_t vector);
static inline void send_all_others_ipi(uint8_t vector);
static inline void __send_ipi(uint8_t hw_coreid, uint8_t vector);
static inline void send_group_ipi(uint8_t hw_groupid, uint8_t vector);
static inline void __send_nmi(uint8_t hw_coreid);

/* XXX: remove these */
#define mask_lapic_lvt(entry) \
	apicrput(entry, apicrget(entry) | LAPIC_LVT_MASK)
#define unmask_lapic_lvt(entry) \
	apicrput(entry, apicrget(entry) & ~LAPIC_LVT_MASK)

static inline void lapic_send_eoi(int unused)
{
	apicrput(MSR_LAPIC_EOI, 0);
}

static inline uint32_t lapic_get_version(void)
{
	return apicrget(MSR_LAPIC_VERSION);
}

static inline uint32_t lapic_get_error(void)
{
	apicrput(MSR_LAPIC_ESR, 0xdeadbeef);
	return apicrget(MSR_LAPIC_ESR);
}

static inline uint32_t lapic_get_id(void)
{
	return apicrget(MSR_LAPIC_ID);
}

static inline uint8_t lapic_get_logid(void)
{
	return apicrget(MSR_LAPIC_LDR);
}

static inline void lapic_disable_timer(void)
{
	apicrput(MSR_LAPIC_LVT_TIMER, 0);
}

/* There are a couple ways to do it.  The MSR route doesn't seem to work
 * in KVM.  It's also a somewhat permanent thing
 */
static inline void lapic_disable(void)
{
	apicrput(MSR_LAPIC_SPURIOUS, apicrget(MSR_LAPIC_SPURIOUS) & 0xffffefff);
}

static inline void lapic_enable(void)
{
	apicrput(MSR_LAPIC_SPURIOUS, apicrget(MSR_LAPIC_SPURIOUS) | 0x00000100);
}

static inline void send_init_ipi(void)
{
	apicsendipi(0xFFFFFFFF000c4500);
}

static inline void send_startup_ipi(uint8_t vector)
{
	apicsendipi(0xFFFFFFFF000c4600ULL | vector);
}

static inline void send_self_ipi(uint8_t vector)
{
	apicrput(MSR_LAPIC_SELF_IPI, 0x00000000 | vector);
}

static inline void send_broadcast_ipi(uint8_t vector)
{
	apicsendipi(0xFFFFFFFF00084000ULL | vector);
}

static inline void send_all_others_ipi(uint8_t vector)
{
	apicsendipi(0xFFFFFFFF000c4000ULL | vector);
}

static inline void __send_ipi(uint8_t hw_coreid, uint8_t vector)
{
	apicsendipi(((uint64_t)hw_coreid << 32) | 0x00004000 | vector);
}

static inline void send_group_ipi(uint8_t hw_groupid, uint8_t vector)
{
	apicsendipi(((uint64_t)hw_groupid << 32) | 0x00004800 | vector);
}

static inline void __send_nmi(uint8_t hw_coreid)
{
	if (hw_coreid == 255)
		return;
	apicsendipi(((uint64_t)hw_coreid << 32) | 0x00004400);
}

/* To change the LAPIC Base (not recommended):
	msr_val = read_msr(IA32_APIC_BASE);
	msr_val = msr_val & ~MSR_APIC_BASE_ADDRESS | 0xfaa00000;
	write_msr(IA32_APIC_BASE, msr_val);
*/

/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * There are 2 flavours of APIC, Local APIC and IOAPIC,
 * Each I/O APIC has a unique physical address,
 * Local APICs are all at the same physical address as they can only be
 * accessed by the local CPU.  APIC ids are unique to the
 * APIC type, so an IOAPIC and APIC both with id 0 is ok.
 */

struct ioapic {
	spinlock_t lock;	/* IOAPIC: register access */
	uintptr_t addr;		/* IOAPIC: register base */
	uintptr_t paddr;	/* register base */
	int nrdt;		/* IOAPIC: size of RDT */
	int ibase;		/* global interrupt base */
};

struct lapic {
	int machno;		/* similar to os_coreid, unused */

	uint32_t lvt[8];
	int nlvt;
	int ver;
};

struct apic {
	int useable;				/* en */
	struct ioapic;
	struct lapic;
};

enum {
	Nbus = 256,
	Napic = 256,	// XXX /* xAPIC architectural limit */
	Nrdt = 64,
};

/*
 * Common bits for
 *	IOAPIC Redirection Table Entry (RDT);
 *	APIC Local Vector Table Entry (LVT);
 *	APIC Interrupt Command Register (ICR).
 * [10:8] Message Type
 * [11] Destination Mode (RW)
 * [12] Delivery Status (RO)
 * [13] Interrupt Input Pin Polarity (RW)
 * [14] Remote IRR (RO)
 * [15] Trigger Mode (RW)
 * [16] Interrupt Mask
 */
enum {
	MTf = 0x00000000,	/* Fixed */
	MTlp = 0x00000100,	/* Lowest Priority */
	MTsmi = 0x00000200,	/* SMI */
	MTrr = 0x00000300,	/* Remote Read */
	MTnmi = 0x00000400,	/* NMI */
	MTir = 0x00000500,	/* INIT/RESET */
	MTsipi = 0x00000600,	/* Startup IPI */
	MTei = 0x00000700,	/* ExtINT */

	Pm = 0x00000000,	/* Physical Mode */
	Lm = 0x00000800,	/* Logical Mode */

	Ds = 0x00001000,	/* Delivery Status */
	IPhigh = 0x00000000,	/* IIPP High */
	IPlow = 0x00002000,	/* IIPP Low */
	Rirr = 0x00004000,	/* Remote IRR */
	TMedge = 0x00000000,	/* Trigger Mode Edge */
	TMlevel = 0x00008000,	/* Trigger Mode Level */
	Im = 0x00010000,	/* Interrupt Mask */
};

extern struct apic xlapic[Napic];
extern struct apic xioapic[Napic];

#include <arch/ioapic.h>

char *apicdump(char *, char *);
void apictimerenab(void);
void apicinit(int apicno, uintptr_t pa, int isbp);
