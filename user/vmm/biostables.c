/* Copyright (c) 2017 Google Inc.
 * See LICENSE for details.
 *
 * ACPI setup. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <ros/arch/mmu.h>
#include <vmm/vmm.h>

#include <vmm/acpi/acpi.h>
#include <vmm/acpi/vmm_simple_dsdt.h>

/* By 1999, you could just scan the hardware
 * and work it out. But 2005, that was no longer possible. How sad.
 * so we have to fake acpi to make it all work.
 * This will be copied to memory at 0xe0000, so the kernel can find it.
 */

/* assume they're all 256 bytes long just to make it easy.
 * Just have pointers that point to aligned things.
 */

struct acpi_table_rsdp rsdp = {
	.signature = ACPI_SIG_RSDP,
	.oem_id = "AKAROS",
	.revision = 2,
	.length = 36,
};

struct acpi_table_xsdt xsdt = {
	.header = {
		.signature = ACPI_SIG_DSDT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
};
struct acpi_table_fadt fadt = {
	.header = {
		.signature = ACPI_SIG_FADT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},
};


/* This has to be dropped into memory, then the other crap just follows it.
 */
struct acpi_table_madt madt = {
	.header = {
		.signature = ACPI_SIG_MADT,
		.revision = 2,
		.oem_id = "AKAROS",
		.oem_table_id = "ALPHABET",
		.oem_revision = 0,
		.asl_compiler_id = "RON ",
		.asl_compiler_revision = 0,
	},

	.address = APIC_GPA,
	.flags = 0,
};

struct acpi_madt_io_apic
	Apic1 = {.header = {.type = ACPI_MADT_TYPE_IO_APIC,
	         .length = sizeof(struct acpi_madt_io_apic)},
	         .id = 0, .address = 0xfec00000, .global_irq_base = 0};

struct acpi_madt_interrupt_override isor[] = {
	/* From the ACPI Specification Version 6.1: For example, if your machine has
	 * the ISA Programmable Interrupt Timer (PIT) connected to ISA IRQ 0, but in
	 * APIC mode, it is connected to I/O APIC interrupt input 2, then you would
	 * need an Interrupt Source Override where the source entry is ‘0’
	 * and the Global System Interrupt is ‘2.’ */
};

void lowmem(void)
{
	asm volatile (".section .lowmem, \"aw\";"
	              "low: ;"
	              ".=0x1000;"
	              ".align 0x100000;"
	              ".previous;");
}

static uint8_t acpi_tb_checksum(uint8_t *buffer, uint32_t length)
{
	uint8_t sum = 0;
	uint8_t *end = buffer + length;

	fprintf(stderr, "tbchecksum %p for %d", buffer, length);
	while (buffer < end) {
		if (end - buffer < 2)
			fprintf(stderr, "%02x\n", sum);
		sum = (uint8_t)(sum + *(buffer++));
	}
	fprintf(stderr, " is %02x\n", sum);
	return sum;
}

static void gencsum(uint8_t *target, void *data, int len)
{
	uint8_t csum;
	// blast target to zero so it does not get counted
	// (it might be in the struct we checksum) And, yes, it is, goodness.
	fprintf(stderr, "gencsum %p target %p source %d bytes\n", target, data,
	        len);
	*target = 0;
	csum = acpi_tb_checksum((uint8_t *)data, len);
	*target = ~csum + 1;
	fprintf(stderr, "Cmoputed is %02x\n", *target);
}

/* Initialize the MADT structs for each local apic. */
static void *init_madt_local_apic(struct virtual_machine *vm, void *start)
{
	struct acpi_madt_local_apic *apic = start;

	for (int i = 0; i < vm->nr_gpcs; i++) {
		apic->header.type = ACPI_MADT_TYPE_LOCAL_APIC;
		apic->header.length = sizeof(struct acpi_madt_local_apic);
		apic->processor_id = i;
		apic->id = i;
		apic->lapic_flags = 1;
		apic = (void *)apic + sizeof(struct acpi_madt_local_apic);
	}
	return apic;
}

/* Initialize the MADT structs for each local x2apic. */
static void *init_madt_local_x2apic(struct virtual_machine *vm, void *start)
{
	struct acpi_madt_local_x2apic *apic = start;

	for (int i = 0; i < vm->nr_gpcs; i++) {
		apic->header.type = ACPI_MADT_TYPE_LOCAL_X2APIC;
		apic->header.length = sizeof(struct acpi_madt_local_x2apic);
		apic->local_apic_id = i;
		apic->uid = i;
		apic->lapic_flags = 1;
		apic = (void *)apic + sizeof(struct acpi_madt_local_x2apic);
	}
	return apic;
}

static int cat(char *file, void *where)
{
	int fd;
	int amt, tot = 0;

	fd = open(file, O_RDONLY);
	if (fd < 0)
		return -1;

	while (amt = read(fd, where, 4096)) {
		if (amt < 0) {
			close(fd);
			return -1;
		}
		tot += amt;
		where += amt;
	}
	close(fd);
	return tot;
}

static int smbios(char *smbiostable, void *esegment)
{
	int amt;

	amt = cat(smbiostable, esegment);
	if (amt < 0) {
		fprintf(stderr, "%s: %r\n", smbiostable);
		exit(1);
	}

	return amt;
}

void *setup_biostables(struct virtual_machine *vm,
                       void *a, void *smbiostable)
{
	struct acpi_table_rsdp *r;
	struct acpi_table_fadt *f;
	struct acpi_table_madt *m;
	struct acpi_table_xsdt *x;
	void *low1m;
	uint8_t csum;


	// The low 1m is so we can fill in bullshit like ACPI.
	// And, sorry, due to the STUPID format of the RSDP for now we need the low
	// 1M.
	low1m = mmap((int*)4096, MiB-4096, PROT_READ | PROT_WRITE,
	             MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	if (low1m != (void *)4096) {
		perror("Unable to mmap low 1m");
		exit(1);
	}

	/* As I understood it, the spec was that SMBIOS
	 * tables live at f0000. We've been finding that
	 * they can have pointers to exxxx. So, for now,
	 * we assume you will take a 128K snapshot of flash
	 * and we'll just splat the whole mess in at
	 * 0xe0000. We can get more sophisticated about
	 * this later if needed. TODO: parse the table,
	 * and make sure that ACPI doesn't trash it.
	 * Although you'll know instantly if that happens
	 * as you'll get dmidecode errors. But it still needs
	 * to be better. */
	if (smbiostable) {
		fprintf(stderr, "Using SMBIOS table %s\n", smbiostable);
		smbios(smbiostable, (void *)0xe0000);
	}

	r = a;
	fprintf(stderr, "install rsdp to %p\n", r);
	*r = rsdp;
	a += sizeof(*r);
	r->xsdt_physical_address = (uint64_t)a;
	gencsum(&r->checksum, r, ACPI_RSDP_CHECKSUM_LENGTH);
	csum = acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_CHECKSUM_LENGTH);
	if (csum != 0) {
		fprintf(stderr, "RSDP has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	/* Check extended checksum if table version >= 2 */
	gencsum(&r->extended_checksum, r, ACPI_RSDP_XCHECKSUM_LENGTH);
	if ((rsdp.revision >= 2) &&
	    (acpi_tb_checksum((uint8_t *) r, ACPI_RSDP_XCHECKSUM_LENGTH) != 0)) {
		fprintf(stderr, "RSDP has bad checksum v2\n");
		exit(1);
	}

	/* just leave a bunch of space for the xsdt. */
	/* we need to zero the area since it has pointers. */
	x = a;
	a += sizeof(*x) + 8*sizeof(void *);
	memset(x, 0, a - (void *)x);
	fprintf(stderr, "install xsdt to %p\n", x);
	*x = xsdt;
	x->table_offset_entry[0] = 0;
	x->table_offset_entry[1] = 0;
	x->header.length = a - (void *)x;

	f = a;
	fprintf(stderr, "install fadt to %p\n", f);
	*f = fadt;
	x->table_offset_entry[0] = (uint64_t)f; // fadt MUST be first in xsdt!
	a += sizeof(*f);
	f->header.length = a - (void *)f;

	f->Xdsdt = (uint64_t) a;
	fprintf(stderr, "install dsdt to %p\n", a);
	memcpy(a, &DSDT_DSDTTBL_Header, 36);
	a += 36;

	gencsum(&f->header.checksum, f, f->header.length);
	if (acpi_tb_checksum((uint8_t *)f, f->header.length) != 0) {
		fprintf(stderr, "fadt has bad checksum v2\n");
		exit(1);
	}

	m = a;
	*m = madt;
	x->table_offset_entry[3] = (uint64_t) m;
	a += sizeof(*m);
	fprintf(stderr, "install madt to %p\n", m);

	a = init_madt_local_apic(vm, a);

	memmove(a, &Apic1, sizeof(Apic1));
	a += sizeof(Apic1);

	a = init_madt_local_x2apic(vm, a);

	memmove(a, &isor, sizeof(isor));
	a += sizeof(isor);
	m->header.length = a - (void *)m;

	gencsum(&m->header.checksum, m, m->header.length);
	if (acpi_tb_checksum((uint8_t *) m, m->header.length) != 0) {
		fprintf(stderr, "madt has bad checksum v2\n");
		exit(1);
	}

	gencsum(&x->header.checksum, x, x->header.length);
	csum = acpi_tb_checksum((uint8_t *) x, x->header.length);
	if (csum != 0) {
		fprintf(stderr, "XSDT has bad checksum; summed to %x\n", csum);
		exit(1);
	}

	fprintf(stderr, "allchecksums ok\n");

	a = (void *)(((unsigned long)a + 0xfff) & ~0xfff);

	return a;
}
