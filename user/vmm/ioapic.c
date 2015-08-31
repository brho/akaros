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
#include <ros/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

#define IOAPIC_CONFIG 0x100

int debug_ioapic = 0;
#define DPRINTF(fmt, ...) \
	if (debug_ioapic) { printf("ioapic: " fmt , ## __VA_ARGS__); }


struct ioapicinfo {
	int state; // not used yet. */
	uint64_t ioapicbase;
};

static struct ioapicinfo ioapicinfo;

char *ioapic_names[256] = {
	[0] "IOAPICID",
};

static uint32_t ioapic_read(uint64_t gpa)
{

	unsigned int offset = gpa - ioapicinfo.ioapicbase;
	uint32_t low;
	
	DPRINTF("ioapic_read offset %s 0x%x\n", ioapic_names[offset],(int)offset);

	if (offset >= IOAPIC_CONFIG) {
		fprintf(stderr, "Whoa. %p Reading past ioapic space? What gives?\n", gpa);
		return -1;
	}


    switch (offset) {
    case 0x20: 
	    return 0;
    default:
	    fprintf(stderr, "bad register offset@%p\n", (void *)gpa);
	    return 0;
    }
    return 0;
}

static void ioapic_write(uint64_t gpa, uint32_t value)
{
	uint64_t val64;
	uint32_t low, high;
	unsigned int offset = gpa - ioapicinfo.ioapicbase;
	
	DPRINTF("ioapic_write offset %s 0x%x value 0x%x\n", ioapic_names[offset], (int)offset, value);

    if (offset >= IOAPIC_CONFIG) {
	    fprintf(stderr, "Whoa. %p Writing past ioapic config space? What gives?\n", gpa);
	    return;
    }

    switch (offset) {
    default:
        DPRINTF("bad register offset 0x%x\n", offset);
    }

}

int ioapic(struct vmctl *v, uint64_t gpa, int destreg, uint64_t *regp, int store)
{
	if (store) {
		ioapic_write(gpa, *regp);
		DPRINTF("Write: mov %s to %s @%p val %p\n", regname(destreg), ioapic_names[(uint8_t)gpa], gpa, *regp);
	} else {
		*regp = ioapic_read(gpa);
		DPRINTF("Read: Set %s from %s @%p to %p\n", regname(destreg), ioapic_names[(uint8_t)gpa], gpa, *regp);
	}

}
