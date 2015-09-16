/*
 * IOAPIC emulation
 *
 * Copyright 2015 Google Inc.
 *
 * See LICENSE for details.
 */

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdint.h>
#include <err.h>
#include <sys/mman.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

#define IOAPIC_CONFIG 0x100
#define IOAPIC_NUM_PINS 24

int debug_ioapic = 1;
int apic_id_mask = 0xf0;

#define DPRINTF(fmt, ...) \
	if (debug_ioapic) { fprintf(stderr, "ioapic: " fmt , ## __VA_ARGS__); }


struct ioapic {
	int id;
	int reg;
	uint32_t arbid;
	uint32_t value[256];
};

static struct ioapic ioapic[1];

static uint32_t ioapic_read(int ix, uint64_t offset)
{
	uint32_t ret = (uint32_t)-1;
	uint32_t reg = ioapic[ix].reg;


	if (offset == 0) {
		DPRINTF("ioapic_read ix %x return 0x%x\n", ix, reg);
		return reg;
	}

	DPRINTF("ioapic_read %x 0x%x\n", ix, (int)reg);
	switch (reg) {
	case 0:
		return ioapic[ix].id;
		break;
	case 1:
		return 0x170011;
		break;
	case 2:
		return ioapic[ix].arbid;
		break;
	default:
		if (reg >= 0 && reg < (IOAPIC_NUM_PINS*2 + 0x10)) {
			//bx_io_redirect_entry_t *entry = ioredtbl + index;
			//data = (ioregsel&1) ? entry->get_hi_part() : entry->get_lo_part();
			ret = ioapic[ix].value[reg];
			DPRINTF("IOAPIC_READ %x: %x return %08x\n", ix, reg, ret);
			return ret;
		} else {
			DPRINTF("IOAPIC READ: %x BAD INDEX 0x%x\n", ix, reg);
		}
		return ret;
		break;
	}
	return 0;
}

static void ioapic_write(int ix, uint64_t offset, uint32_t value)
{
	uint32_t ret;
	uint32_t reg = ioapic[ix].reg;

	if (offset == 0) {
		DPRINTF("ioapic_write ix %x set reg 0x%x\n", ix, value);
		ioapic[ix].reg = value;
		return;
	}

	switch (reg) {
	case 0:
		DPRINTF("IOAPIC_WRITE: Set %d ID to %d\n", ix, value);
		ioapic[ix].id = value;
		break;
	case 1:
	case 2:
		DPRINTF("IOAPIC_WRITE: Can't write %d\n", reg);
	default:
		if (reg >= 0 && reg < (IOAPIC_NUM_PINS*2 + 0x10)) {
			ioapic[ix].value[reg] = value;
			DPRINTF("IOAPIC %x: set %08x to %016x\n", ix, reg, value);
		} else {
			DPRINTF("IOAPIC WRITE: %x BAD INDEX 0x%x\n", ix, reg);
		}
		break;
	}

}

int do_ioapic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store)
{
	// TODO: compute an index for the ioapic array. 
	int ix = 0;
	uint32_t offset = gpa & 0xfffff;
	/* basic sanity tests. */
	DPRINTF("%s: %p 0x%x %p %s\n", __func__, (void *)gpa, destreg, regp, store ? "write" : "read");

	if ((offset != 0) && (offset != 0x10)) {
		DPRINTF("Bad register offset: 0x%x and has to be 0x0 or 0x10\n", offset);
		return -1;
	}

	if (store) {
		ioapic_write(ix, offset, *regp);
	} else {
		*regp = ioapic_read(ix, offset);
	}

}
