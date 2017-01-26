/*
 * APIC emulation
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
#include <vmm/sched.h>


#define APIC_CONFIG 0x100

int debug_apic = 1;
#define DPRINTF(fmt, ...) \
	if (debug_apic) { fprintf(stderr, "apic: " fmt , ## __VA_ARGS__); }


static struct apicinfo apicinfo;

enum {
	reserved,
	readonly = 1,
	readwrite = 3,
	writeonly = 2
};

struct {
	char *name;
	int mode;
	uint32_t value;
} apicregs[256] = {
[0x00] {.name = "Reserved", .mode =  reserved},
[0x01] {.name = "Reserved", .mode =  reserved},
[0x02] {.name = "Local APIC ID Register Read/Write.", .mode = readwrite},
[0x03] {.name = "Local APIC Version Register Read Only.", .mode = readonly},
[0x04] {.name = "Reserved", .mode =  reserved},
[0x05] {.name = "Reserved", .mode =  reserved},
[0x06] {.name = "Reserved", .mode =  reserved},
[0x07] {.name = "Reserved", .mode =  reserved},
[0x08] {.name = "Task Priority Register (TPR) Read/Write.", .mode = readwrite},
[0x09] {.name = "Arbitration Priority Register1 (APR) Read Only.", .mode = readonly},
[0x0A] {.name = "Processor Priority Register (PPR) Read Only.", .mode = readonly},
[0x0B] {.name = "EOI Register Write Only.", .mode = writeonly},
[0x0C] {.name = "Remote Read Register1 (RRD) Read Only", .mode = readonly},
[0x0D] {.name = "Logical Destination Register Read/Write.", .mode = readwrite},
[0x0E] {.name = "Destination Format Register Read/Write (see Section", .mode = readwrite},
[0x0F] {.name = "Spurious Interrupt Vector Register Read/Write (see Section 10.9.", .mode = readwrite},
[0x10] {.name = "In-Service Register (ISR); bits 31:0 Read Only.", .mode = readonly},
[0x11] {.name = "In-Service Register (ISR); bits 63:32 Read Only.", .mode = readonly},
[0x12] {.name = "In-Service Register (ISR); bits 95:64 Read Only.", .mode = readonly},
[0x13] {.name = "In-Service Register (ISR); bits 127:96 Read Only.", .mode = readonly},
[0x14] {.name = "In-Service Register (ISR); bits 159:128 Read Only.", .mode = readonly},
[0x15] {.name = "In-Service Register (ISR); bits 191:160 Read Only.", .mode = readonly},
[0x16] {.name = "In-Service Register (ISR); bits 223:192 Read Only.", .mode = readonly},
[0x17] {.name = "In-Service Register (ISR); bits 255:224 Read Only.", .mode = readonly},
[0x18] {.name = "Trigger Mode Register (TMR); bits 31:0 Read Only.", .mode = readonly},
[0x19] {.name = "Trigger Mode Register (TMR); bits 63:32 Read Only.", .mode = readonly},
[0x1A] {.name = "Trigger Mode Register (TMR); bits 95:64 Read Only.", .mode = readonly},
[0x1B] {.name = "Trigger Mode Register (TMR); bits 127:96 Read Only.", .mode = readonly},
[0x1C] {.name = "Trigger Mode Register (TMR); bits 159:128 Read Only.", .mode = readonly},
[0x1D] {.name = "Trigger Mode Register (TMR); bits 191:160 Read Only.", .mode = readonly},
[0x1E] {.name = "Trigger Mode Register (TMR); bits 223:192 Read Only.", .mode = readonly},
[0x1F] {.name = "Trigger Mode Register (TMR); bits 255:224 Read Only.", .mode = readonly},
[0x20] {.name = "Interrupt Request Register (IRR); bits 31:0 Read Only.", .mode = readonly},
[0x21] {.name = "Interrupt Request Register (IRR); bits 63:32 Read Only.", .mode = readonly},
[0x22] {.name = "Interrupt Request Register (IRR); bits 95:64 Read Only.", .mode = readonly},
[0x23] {.name = "Interrupt Request Register (IRR); bits 127:96 Read Only.", .mode = readonly},
[0x24] {.name = "Interrupt Request Register (IRR); bits 159:128 Read Only.", .mode = readonly},
[0x25] {.name = "Interrupt Request Register (IRR); bits 191:160 Read Only.", .mode = readonly},
[0x26] {.name = "Interrupt Request Register (IRR); bits 223:192 Read Only.", .mode = readonly},
[0x27] {.name = "Interrupt Request Register (IRR); bits 255:224 Read Only.", .mode = readonly},
[0x28] {.name = "Error Status Register Read Only.", .mode = readonly},
[0x29 ] {.name = "Reserved", .mode =  reserved},
[0x2a] {.name = "Reserved", .mode =  reserved},
[0x2b] {.name = "Reserved", .mode =  reserved},
[0x2c] {.name = "Reserved", .mode =  reserved},
[0x2d] {.name = "Reserved", .mode =  reserved},
[0x2E] {.name = "Reserved", .mode =  reserved},
[0x2F] {.name = "LVT CMCI Register Read/Write.", .mode = readwrite},
[0x30] {.name = "Interrupt Command Register (ICR); bits 0-31 Read/Write.", .mode = readwrite},
[0x31] {.name = "Interrupt Command Register (ICR); bits 32-63 Read/Write.", .mode = readwrite},
[0x32] {.name = "LVT Timer Register Read/Write.", .mode = readwrite},
[0x33] {.name = "LVT Thermal Sensor Register2 Read/Write.", .mode = readwrite},
[0x34] {.name = "LVT Performance Monitoring Counters Register3 Read/Write.", .mode = readwrite},
[0x35] {.name = "LVT LINT0 Register Read/Write.", .mode = readwrite},
[0x36] {.name = "LVT LINT1 Register Read/Write.", .mode = readwrite},
[0x37] {.name = "LVT Error Register Read/Write.", .mode = readwrite},
[0x38] {.name = "Initial Count Register (for Timer) Read/Write.", .mode = readwrite},
[0x39] {.name = "Current Count Register (for Timer) Read Only.", .mode = readonly},
[0x3A] {.name = "Reserved", .mode =  reserved},
[0x3a]{.name = "Reserved", .mode =  reserved},
[0x3b]{.name = "Reserved", .mode =  reserved},
[0x3c]{.name = "Reserved", .mode =  reserved},
[0x3D]{.name = "Reserved", .mode =  reserved},
[0x3E] {.name = "Divide Configuration Register (for Timer) Read/Write.", .mode = readwrite},
[0x3F] {.name = "Reserved", .mode =  reserved},
};

static uint32_t apic_read(uint64_t offset)
{

	uint32_t low;

	DPRINTF("apic_read offset %s 0x%x\n", apicregs[offset].name, (int)offset);

	if (! apicregs[offset].mode & 1) {
		fprintf(stderr, "Attempt to read %s, which is %s\n", apicregs[offset].name,
			apicregs[offset].mode == 0 ?  "reserved" : "writeonly");
		// panic? what to do?
		return (uint32_t) -1;
	}

	// no special cases yet.
	switch (offset) {
	default:
		DPRINTF("%s: return %08x\n", apicregs[offset].name, apicregs[offset].value);
		return apicregs[offset].value;
		break;
	}
	return 0;
}

static void apic_write(uint64_t offset, uint32_t value)
{
	uint64_t val64;
	uint32_t low, high;

	DPRINTF("apic_write offset %s 0x%x value 0x%x\n", apicregs[offset].name, (int)offset, value);

	if (! apicregs[offset].mode & 2) {
		fprintf(stderr, "Attempt to write %s, which is %s\n", apicregs[offset].name,
			apicregs[offset].mode == 0 ?  "reserved" : "readonly");
		// panic? what to do?
		return;
	}

	switch (offset) {
	default:
		DPRINTF("%s: Set to %08x\n", apicregs[offset].name, value);
		apicregs[offset].value = value;
		break;
	}

}

int __apic_access(struct guest_thread *vm_thread, uint64_t gpa, int destreg,
                  uint64_t *regp, int store)
{
	uint32_t offset = gpa & 0xfffff;
	/* basic sanity tests. */
	// TODO: Should be minus the base but FIXME

	//fprintf(stderr, "WE SHOULD NEVER BE HERE: user/vmm/apic.c");
	//exit(1);

	offset = gpa & 0xfffff;
	if (offset & 0xf) {
		DPRINTF("bad register offset; low nibl is non-zero\n");
		return -1;
	}
	offset >>= 4;
	if (offset > APIC_CONFIG) {
		DPRINTF("Bad register offset: 0x%x and max is 0x%x\n", gpa, gpa + APIC_CONFIG);
		return -1;
	}

	if (store) {
		apic_write(offset, *regp);
		DPRINTF("Write: mov %s to %s @%p val %p\n", regname(destreg), apicregs[offset].name, gpa, *regp);
	} else {
		*regp = apic_read(offset);
		DPRINTF("Read: Set %s from %s @%p to %p\n", regname(destreg), apicregs[offset].name, gpa, *regp);
	}
	return 0;
}

void vapic_status_dump(FILE *f, void *vapic)
{
	uint32_t *p = (uint32_t *)vapic;
	int i;
	fprintf(f, "-- BEGIN APIC STATUS DUMP --\n");
	for (i = 0x100/sizeof(*p); i < 0x180/sizeof(*p); i+=4) {
		fprintf(f, "VISR : 0x%x: 0x%08x\n", i, p[i]);
	}
	for (i = 0x200/sizeof(*p); i < 0x280/sizeof(*p); i+=4) {
		fprintf(f, "VIRR : 0x%x: 0x%08x\n", i, p[i]);
	}
	i = 0x0B0/sizeof(*p);
	fprintf(f, "EOI FIELD : 0x%x, 0x%08x\n", i, p[i]);

	fprintf(f, "-- END APIC STATUS DUMP --\n");
}
