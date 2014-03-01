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
#include <acpi.h>
#include <arch/ioapic.h>
#include <arch/coreid.h>

extern	struct Madt	*apics;

int
mpacpi(int ncleft)
{
	char *already;
	int np, bp;
	struct apic *apic;
	struct Apicst *st;

	if (apics == NULL)
		return ncleft;

	printd("APIC lapic paddr %#.8llux, flags %#.8ux\n",
		apics->lapicpa, apics->pcat);
	np = 0;
	for(st = apics->st; st != NULL; st = st->next){
		already = "";
		switch(st->type){
		case ASlapic:
			/* this table is supposed to have all of them if it exists */
			if(st->lapic.id > MaxAPICNO)
				break;
			apic = xlapic + st->lapic.id;
			bp = (np++ == 0);
			if(apic->useable){
				already = "(mp)";
			}
			else if(ncleft != 0){
				ncleft--;
				printk("apicinit(%d, %p, %d);\n", st->lapic.id, apics->lapicpa, bp);
				apicinit(st->lapic.id, apics->lapicpa, bp);
			} else
				already = "(off)";

			printd("apic proc %d/%d apicid %d %s\n", np-1, apic->machno, st->lapic.id, already);
			break;
		case ASioapic:
			if(st->ioapic.id > Napic)
				break;
			apic = xioapic + st->ioapic.id;
			if(apic->useable){
				apic->ibase = st->ioapic.ibase;	/* gnarly */
				already = "(mp)";
				goto pr1;
			}
			printk("ioapicinit(%d, %p, %p);\n", st->lapic.id, apics->lapicpa, st->ioapic.addr);
			ioapicinit(st->ioapic.id, st->ioapic.ibase, st->ioapic.addr);
		pr1:
			printd("ioapic %d ", st->ioapic.id);
			printd("addr %p base %d %s\n", apic->paddr, apic->ibase, already);
			break;
		}
	}
	return ncleft;
}
