#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>
#include <arch/io.h>
#include <trap.h>

static char *apicregnames[] = {
	[MSR_LAPIC_ID] "Identification",
	[MSR_LAPIC_VERSION] "Version",
	[MSR_LAPIC_TPR] "Task Priority",
//	[Ap] "Arbitration Priority",
	[MSR_LAPIC_PPR] "Processor Priority",
	[MSR_LAPIC_EOI] "EOI",
	[MSR_LAPIC_LDR] "Logical Destination",
	[MSR_LAPIC_SPURIOUS] "Spurious Interrupt Vector",
	[MSR_LAPIC_ISR_START] "Interrupt Status (8)",
	[MSR_LAPIC_TMR_START] "Trigger Mode (8)",
	[MSR_LAPIC_IRR_START] "Interrupt Request (8)",
	[MSR_LAPIC_ESR] "Error Status",
	[MSR_LAPIC_ICR] "Interrupt Command",
	[MSR_LAPIC_INITIAL_COUNT] "Timer Initial Count",
	[MSR_LAPIC_CURRENT_COUNT] "Timer Current Count",
	[MSR_LAPIC_DIVIDE_CONFIG_REG] "Timer Divide Configuration",

	[MSR_LAPIC_LVT_TIMER] "Timer",
	[MSR_LAPIC_LVT_LINT0] "Local Interrupt 0",
	[MSR_LAPIC_LVT_LINT1] "Local Interrupt 1",
	[MSR_LAPIC_LVT_ERROR_REG] "Error",
	[MSR_LAPIC_LVT_PERFMON] "Performance Counter",
	[MSR_LAPIC_LVT_THERMAL] "Thermal Sensor",
};

enum {							/* Siv */
	Swen = 0x00000100,			/* Software Enable */
	Fdis = 0x00000200,	/* Focus Disable */
};

enum {							/* Iclo */
	Lassert = 0x00004000,		/* Assert level */

	DSnone = 0x00000000,	/* Use Destination Field */
	DSself = 0x00040000,	/* Self is only destination */
	DSallinc = 0x00080000,	/* All including self */
	DSallexc = 0x000c0000,	/* All Excluding self */
};

enum {							/* Tlvt */
	Periodic = 0x00020000,		/* Periodic Timer Mode */
};

enum {							/* Tdc */
	DivX2 = 0x00000000,			/* Divide by 2 */
	DivX4 = 0x00000001,	/* Divide by 4 */
	DivX8 = 0x00000002,	/* Divide by 8 */
	DivX16 = 0x00000003,	/* Divide by 16 */
	DivX32 = 0x00000008,	/* Divide by 32 */
	DivX64 = 0x00000009,	/* Divide by 64 */
	DivX128 = 0x0000000a,	/* Divide by 128 */
	DivX1 = 0x0000000b,	/* Divide by 1 */
};

static uintptr_t apicbase;
static int apmachno = 1;
static uint32_t apicr310;

struct apic xlapic[Napic];

static void __apic_ir_dump(uint64_t r);

static void __apic_ir_dump(uint64_t r)
{
	int i;
	uint32_t val;

	if (r != MSR_LAPIC_ISR_START && r != MSR_LAPIC_IRR_START &&
	    r != MSR_LAPIC_TMR_START)
		panic("Invalid register dump offset!");

	for (i = 7; i >= 0; i--) {
		val = apicrget(r+i);
		if (val) {
			printk("Register at range (%d,%d]: 0x%08x\n", ((i+1)*32),
			       i*32, val);
		}
	}
}
void apic_isr_dump(void)
{
	printk("ISR DUMP\n");
	__apic_ir_dump(MSR_LAPIC_ISR_START);
}
void apic_irr_dump(void)
{
	printk("IRR DUMP\n");
	__apic_ir_dump(MSR_LAPIC_IRR_START);
}

uint32_t apicrget(uint64_t r)
{
	uint32_t val;

	if (r >= MSR_LAPIC_END)
		panic("%s: OUT OF BOUNDS: register 0x%x\n", __func__, r);
	if (r != MSR_LAPIC_SPURIOUS && r != MSR_LAPIC_DIVIDE_CONFIG_REG)
		printd("%s: Reading from register 0x%llx\n",
		       __func__, r);

	val = read_msr(r);
	printd("apicrget: %s returns %p\n", apicregnames[r], val);
	if (r == MSR_LAPIC_ID) {
		printd("APIC ID: 0x%lx\n", val);
		printd("APIC LOGICAL ID: 0x%lx\n",
		       apicrget(MSR_LAPIC_LDR));
	}
	return val;
}

void apicrput(uint64_t r, uint32_t data)
{
	uint64_t temp_data = 0;

	if (r >= MSR_LAPIC_END)
		panic("%s: OUT OF BOUNDS: register 0x%x\n", __func__, r);
	if (r != MSR_LAPIC_INITIAL_COUNT && r != MSR_LAPIC_LVT_TIMER &&
	    r != MSR_LAPIC_DIVIDE_CONFIG_REG && r != MSR_LAPIC_EOI)
		printd("%s: Writing to register 0x%llx, value 0x%lx\n",
		       __func__, r, data);
	if (r == MSR_LAPIC_ID)
		panic("ILLEGAL WRITE TO ID");
	printd("apicrput: %s = %p\n", apicregnames[r], data);

	temp_data |= data;

	write_msr(r, temp_data);
}

void apicsendipi(uint64_t data)
{
	printd("SENDING IPI: 0x%016lx\n", data);
	write_msr(MSR_LAPIC_ICR, data);
}

void apicinit(int apicno, uintptr_t pa, int isbp)
{
	struct apic *apic;
	uint64_t msr_val;
	/*
	 * Mark the APIC useable if it has a good ID
	 * and the registers can be mapped.
	 * The APIC Extended Broadcast and ID bits in the HyperTransport
	 * Transaction Control register determine whether 4 or 8 bits
	 * are used for the APIC ID. There is also xAPIC and x2APIC
	 * to be dealt with sometime.
	 */
	printd("apicinit: apicno %d pa %#p isbp %d\n", apicno, pa, isbp);
	if (apicno >= Napic) {
		printd("apicinit%d: out of range\n", apicno);
		return;
	}
	if ((apic = &xlapic[apicno])->useable) {
		printd("apicinit%d: already initialised\n", apicno);
		return;
	}
	assert(pa == LAPIC_PBASE);
	apic->useable = 1;

	/* plan 9 used to set up a mapping btw apic and pcpui like so:
		pcpui->apicno = apicno; // acpino is the hw_coreid
		apic->machno = apmachno++; // machno is the os_coreid
	 * akaros does its own remapping of hw <-> os coreid during smp_boot */

	//X2APIC INIT
	msr_val = read_msr(IA32_APIC_BASE);
	write_msr(IA32_APIC_BASE, msr_val | (3<<10));
}

static char *apicdump0(char *start, char *end, struct apic *apic, int i)
{
	if (!apic->useable || apic->addr != 0)
		return start;
	start = seprintf(start, end,
	                 "apic%d: oscore %d lint0 %#8.8p lint1 %#8.8p\n", i,
		         get_os_coreid(i), apic->lvt[0], apic->lvt[1]);
	start =	seprintf(start, end, " tslvt %#8.8p pclvt %#8.8p elvt %#8.8p\n",
	                 apicrget(MSR_LAPIC_LVT_THERMAL),
	                 apicrget(MSR_LAPIC_LVT_PERFMON),
	                 apicrget(MSR_LAPIC_LVT_ERROR_REG));
	start =	seprintf(start, end,
	                 " tlvt %#8.8p lint0 %#8.8p lint1 %#8.8p siv %#8.8p\n",
	                 apicrget(MSR_LAPIC_LVT_TIMER),
	                 apicrget(MSR_LAPIC_LVT_LINT0),
	                 apicrget(MSR_LAPIC_LVT_LINT1),
	                 apicrget(MSR_LAPIC_SPURIOUS));
	return start;
}

char *apicdump(char *start, char *end)
{
	int i;

	if (!2)
		return start;

	start =
		seprintf(start, end, "apicbase %#p apmachno %d\n", apicbase, apmachno);
	for (i = 0; i < Napic; i++)
		start = apicdump0(start, end, xlapic + i, i);
	for (i = 0; i < Napic; i++)
		start = apicdump0(start, end, xioapic + i, i);
	return start;
}

void handle_lapic_error(struct hw_trapframe *hw_tf, void *data)
{
	uint32_t err;
	apicrput(MSR_LAPIC_ESR, 0);
	err = apicrget(MSR_LAPIC_ESR);
	/* i get a shitload of these on my nehalem, many with err == 0 */
	printd("LAPIC error vector, got 0x%08x\n", err);
}

int apiconline(void)
{
	struct apic *apic;
	uint64_t tsc;
	uint32_t dfr, ver;
	int apicno, nlvt;
	uint64_t msr_val;

	//X2APIC INIT
	msr_val = read_msr(IA32_APIC_BASE);
	write_msr(IA32_APIC_BASE, msr_val | (3<<10));

	apicno = lapic_get_id();
	if (apicno >= Napic) {
		printk("Bad apicno %d on HW core %d!!\n", apicno, hw_core_id());
		return 0;
	}
	apic = &xlapic[apicno];
	/* The addr check tells us if it is an IOAPIC or not... */
	if (!apic->useable || apic->addr) {
		printk("Unsuitable apicno %d on HW core %d!!\n", apicno, hw_core_id());
		return 0;
	}
	/* Things that can only be done when on the processor owning the APIC,
	 * apicinit above runs on the bootstrap processor. */
	ver = apicrget(MSR_LAPIC_VERSION);
	nlvt = ((ver >> 16) & 0xff) + 1;
	if (nlvt > ARRAY_SIZE(apic->lvt)) {
		printk("apiconline%d: nlvt %d > max (%d)\n",
			   apicno, nlvt, ARRAY_SIZE(apic->lvt));
		nlvt = ARRAY_SIZE(apic->lvt);
	}
	apic->nlvt = nlvt;
	apic->ver = ver & 0xff;

	/* These don't really matter in Physical mode; set the defaults anyway.  If
	 * we have problems with logical IPIs on AMD, check this out: */
	//if (memcmp(m->cpuinfo, "AuthenticAMD", 12) == 0)
	//	dfr = 0xf0000000;
	//else
	//	dfr = 0xffffffff;
	//apicrput(Df, dfr);
	//apicrput(MSR_LAPIC_LDR, 0x00000000);

	/* Disable interrupts until ready by setting the Task Priority register to
	 * 0xff. */
	apicrput(MSR_LAPIC_TPR, 0xff);

	/* Software-enable the APIC in the Spurious Interrupt Vector register and
	 * set the vector number. The vector number must have bits 3-0 0x0f unless
	 * the Extended Spurious Vector Enable bit is set in the HyperTransport
	 * Transaction Control register. */
	apicrput(MSR_LAPIC_SPURIOUS, Swen | IdtLAPIC_SPURIOUS);

	/* Acknowledge any outstanding interrupts. */
	apicrput(MSR_LAPIC_EOI, 0);

	/* Mask interrupts on Performance Counter overflow and Thermal Sensor if
	 * implemented, and on Lintr0 (Legacy INTR), Lintr1 (Legacy NMI), and the
	 * Timer.  Clear any Error Status (write followed by read) and enable the
	 * Error interrupt. */
	switch (apic->nlvt) {
		case 6:
			apicrput(MSR_LAPIC_LVT_THERMAL, Im);
			/* fall-through */
		case 5:
			apicrput(MSR_LAPIC_LVT_PERFMON, Im);
			/* fall-through */
		default:
			break;
	}
	/* lvt[0] and [1] were set to 0 in the BSS */
	apicrput(MSR_LAPIC_LVT_LINT1, apic->lvt[1] | Im | IdtLAPIC_LINT1);
	apicrput(MSR_LAPIC_LVT_LINT0, apic->lvt[0] | Im | IdtLAPIC_LINT0);
	apicrput(MSR_LAPIC_LVT_TIMER, Im);

	apicrput(MSR_LAPIC_ESR, 0);
	apicrget(MSR_LAPIC_ESR);
	apicrput(MSR_LAPIC_LVT_ERROR_REG, IdtLAPIC_ERROR | Im);

	/* Not sure we need this from plan 9, Akaros never did:
	 *
	 * Issue an INIT Level De-Assert to synchronise arbitration ID's.
	 * (Necessary in this implementation? - not if Pentium 4 or Xeon (APIC
	 * Version >= 0x14), or AMD). */
	//apicrput(Ichi, 0);
	//apicrput(Iclo, DSallinc | Lassert | MTir);
	//while (apicrget(Iclo) & Ds)
	//	cpu_relax();

 	/* this is to enable the APIC interrupts.  we did a SW lapic_enable()
	 * earlier.  if we ever have issues where the lapic seems offline, check
	 * here. */
	apicrput(MSR_LAPIC_TPR, 0);
	return 1;
}
