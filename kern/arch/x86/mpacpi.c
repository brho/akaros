/* This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file. */

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

extern struct Madt *apics;

int mpacpi(int ncleft)
{
	char *already;
	int np, bp;
	struct apic *apic;
	struct Apicst *st;

	/* If we don't have an mpisabusno yet, it's because the MP tables failed to
	 * parse.  So we'll just take the last one available.  I think we're
	 * supposed to parse the ACPI shit with the AML to figure out the buses and
	 * find a clear one, but fuck that.  Note this busno is just for our own
	 * RDT/Rbus bookkeeping. */
	if (mpisabusno == -1)
		mpisabusno = Nbus - 1;

	if (apics == NULL)
		return ncleft;

	printd("APIC lapic paddr %#.8llux, flags %#.8ux\n",
		   apics->lapicpa, apics->pcat);
	np = 0;
	printd("apics->st %p\n", apics->st);
	for (st = apics->st; st != NULL; st = st->next) {
		already = "";
		switch (st->type) {
			case ASlapic:
				printd("ASlapic %d\n", st->lapic.id);
				/* this table is supposed to have all of them if it exists */
				if (st->lapic.id > MaxAPICNO)
					break;
				apic = xlapic + st->lapic.id;
				bp = (np++ == 0);
				if (apic->useable) {
					already = "(mp)";
				} else if (ncleft != 0) {
					ncleft--;
					apicinit(st->lapic.id, apics->lapicpa, bp);
				} else
					already = "(off)";

				printd("apic proc %d/%d apicid %d %s\n", np - 1, apic->machno,
					   st->lapic.id, already);
				break;
			case ASioapic:
				printd("ASioapic %d\n", st->ioapic.id);
				if (st->ioapic.id > Napic)
					break;
				apic = xioapic + st->ioapic.id;
				if (apic->useable) {
					apic->ibase = st->ioapic.ibase;	/* gnarly */
					already = "(mp)";
					goto pr1;
				}
				ioapicinit(st->ioapic.id, st->ioapic.ibase, st->ioapic.addr);
pr1:
				printd("ioapic %d ", st->ioapic.id);
				printd("addr %p base %d %s\n", apic->paddr, apic->ibase,
					   already);
				break;
		}
	}
	return ncleft;
}
