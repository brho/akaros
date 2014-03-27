#include <vfs.h>
#include <kfs.h>
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
#include <ip.h>
#include <arch/io.h>
#include <trap.h>

enum {							/* Local APIC registers */
	Id = 0x0020,				/* Identification */
	Ver = 0x0030,	/* Version */
	Tp = 0x0080,	/* Task Priority */
	Ap = 0x0090,	/* Arbitration Priority */
	Pp = 0x00a0,	/* Processor Priority */
	Eoi = 0x00b0,	/* EOI */
	Ld = 0x00d0,	/* Logical Destination */
	Df = 0x00e0,	/* Destination Format */
	Siv = 0x00f0,	/* Spurious Interrupt Vector */
	Is = 0x0100,	/* Interrupt Status (8) */
	Tm = 0x0180,	/* Trigger Mode (8) */
	Ir = 0x0200,	/* Interrupt Request (8) */
	Es = 0x0280,	/* Error Status */
	Iclo = 0x0300,	/* Interrupt Command */
	Ichi = 0x0310,	/* Interrupt Command [63:32] */
	Lvt0 = 0x0320,	/* Local Vector Table 0 */
	Lvt5 = 0x0330,	/* Local Vector Table 5 */
	Lvt4 = 0x0340,	/* Local Vector Table 4 */
	Lvt1 = 0x0350,	/* Local Vector Table 1 */
	Lvt2 = 0x0360,	/* Local Vector Table 2 */
	Lvt3 = 0x0370,	/* Local Vector Table 3 */
	Tic = 0x0380,	/* Timer Initial Count */
	Tcc = 0x0390,	/* Timer Current Count */
	Tdc = 0x03e0,	/* Timer Divide Configuration */

	Tlvt = Lvt0,	/* Timer */
	Lint0 = Lvt1,	/* Local Interrupt 0 */
	Lint1 = Lvt2,	/* Local Interrupt 1 */
	Elvt = Lvt3,	/* Error */
	Pclvt = Lvt4,	/* Performance Counter */
	Tslvt = Lvt5,	/* Thermal Sensor */
};

static char *apicregnames[] = {
	[Id] "Identification",
	[Ver] "Version",
	[Tp] "Task Priority",
	[Ap] "Arbitration Priority",
	[Pp] "Processor Priority",
	[Eoi] "EOI",
	[Ld] "Logical Destination",
	[Df] "Destination Format",
	[Siv] "Spurious Interrupt Vector",
	[Is] "Interrupt Status (8)",
	[Tm] "Trigger Mode (8)",
	[Ir] "Interrupt Request (8)",
	[Es] "Error Status",
	[Iclo] "Interrupt Command",
	[Ichi] "Interrupt Command [63:32]",
	[Lvt0] "Local Vector Table 0",
	[Lvt5] "Local Vector Table 5",
	[Lvt4] "Local Vector Table 4",
	[Lvt1] "Local Vector Table 1",
	[Lvt2] "Local Vector Table 2",
	[Lvt3] "Local Vector Table 3",
	[Tic] "Timer Initial Count",
	[Tcc] "Timer Current Count",
	[Tdc] "Timer Divide Configuration",

	[Tlvt] "Timer",
	[Lint0] "Local Interrupt 0",
	[Lint1] "Local Interrupt 1",
	[Elvt] "Error",
	[Pclvt] "Performance Counter",
	[Tslvt] "Thermal Sensor",
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

struct apic xlapic[Napic];

static uint32_t apicrget(int r)
{
	uint32_t val;
	if (!apicbase)
		panic("apicrget: no apic");
	val = read_mmreg32(apicbase + r);
	printd("apicrget: %s returns %p\n", apicregnames[r], val);
	return val;
}

static void apicrput(int r, uint32_t data)
{
	if (!apicbase)
		panic("apicrput: no apic");
	printd("apicrput: %s = %p\n", apicregnames[r], data);
	write_mmreg32(apicbase + r, data);
}

void apicinit(int apicno, uintptr_t pa, int isbp)
{
	struct apic *apic;

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
	apicbase = LAPIC_BASE;	/* was the plan to just clobber the global? */
	apic->useable = 1;

	/* plan 9 used to set up a mapping btw apic and pcpui like so:
		pcpui->apicno = apicno; // acpino is the hw_coreid
		apic->machno = apmachno++; // machno is the os_coreid
	 * akaros does its own remapping of hw <-> os coreid during smp_boot */
}

static char *apicdump0(char *start, char *end, struct apic *apic, int i)
{
	if (!apic->useable || apic->addr != 0)
		return start;
	start =
		seprintf(start, end, "apic%d: oscore %d lint0 %#8.8p lint1 %#8.8p\n", i,
				 get_os_coreid(i), apic->lvt[0], apic->lvt[1]);
	start =
		seprintf(start, end, " tslvt %#8.8p pclvt %#8.8p elvt %#8.8p\n",
				 apicrget(Tslvt), apicrget(Pclvt), apicrget(Elvt));
	start =
		seprintf(start, end,
				 " tlvt %#8.8p lint0 %#8.8p lint1 %#8.8p siv %#8.8p\n",
				 apicrget(Tlvt), apicrget(Lint0), apicrget(Lint1),
				 apicrget(Siv));
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
	apicrput(Es, 0);
	err = apicrget(Es);
	/* i get a shitload of these on my nehalem, many with err == 0 */
	printd("LAPIC error vector, got 0x%08x\n", err);
}

int apiconline(void)
{
	struct apic *apic;
	uint64_t tsc;
	uint32_t dfr, ver;
	int apicno, nlvt;

	if (!apicbase) {
		printk("No apicbase on HW core %d!!\n", hw_core_id());
		return 0;
	}
	if ((apicno = ((apicrget(Id) >> 24) & 0xff)) >= Napic) {
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
	ver = apicrget(Ver);
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
		dfr = 0xffffffff;
	apicrput(Df, dfr);
	apicrput(Ld, 0x00000000);

	/* Disable interrupts until ready by setting the Task Priority register to
	 * 0xff. */
	apicrput(Tp, 0xff);

	/* Software-enable the APIC in the Spurious Interrupt Vector register and
	 * set the vector number. The vector number must have bits 3-0 0x0f unless
	 * the Extended Spurious Vector Enable bit is set in the HyperTransport
	 * Transaction Control register. */
	apicrput(Siv, Swen | IdtLAPIC_SPURIOUS);

	/* Acknowledge any outstanding interrupts. */
	apicrput(Eoi, 0);

	/* Mask interrupts on Performance Counter overflow and Thermal Sensor if
	 * implemented, and on Lintr0 (Legacy INTR), Lintr1 (Legacy NMI), and the
	 * Timer.  Clear any Error Status (write followed by read) and enable the
	 * Error interrupt. */
	switch (apic->nlvt) {
		case 6:
			apicrput(Tslvt, Im);
			/* fall-through */
		case 5:
			apicrput(Pclvt, Im);
			/* fall-through */
		default:
			break;
	}
	/* lvt[0] and [1] were set to 0 in the BSS */
	apicrput(Lint1, apic->lvt[1] | Im | IdtLAPIC_LINT1);
	apicrput(Lint0, apic->lvt[0] | Im | IdtLAPIC_LINT0);
	apicrput(Tlvt, Im);

	apicrput(Es, 0);
	apicrget(Es);
	apicrput(Elvt, IdtLAPIC_ERROR | Im);

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
	apicrput(Tp, 0);
	return 1;
}
