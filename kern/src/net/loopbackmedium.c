/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

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
#include <net/ip.h>

enum {
	Maxtu = 16 * 1024,
};

typedef struct LB LB;
struct LB {
	struct proc *readp;
	struct queue *q;
	struct Fs *f;
};

static void loopbackread(void *a);

static void
loopbackbind(struct Ipifc *ifc, int unused_int, char **unused_char_pp_t)
{
	LB *lb;

	lb = kzmalloc(sizeof(*lb), 0);
	lb->f = ifc->conv->p->f;
	/* TO DO: make queue size a function of kernel memory */
	lb->q = qopen(128 * 1024, Qmsg, NULL, NULL);
	ifc->arg = lb;
	ifc->mbps = 1000;

	ktask("loopbackread", loopbackread, ifc);

}

static void loopbackunbind(struct Ipifc *ifc)
{
	LB *lb = ifc->arg;

	printk("%s is messed up, shouldn't track procs\n", __FUNCTION__);

	/*if(lb->readp)
	   postnote(lb->readp, 1, "unbind", 0);
	 */

	/* wait for reader to die */
	while (lb->readp != 0)
		kthread_usleep(300 * 1000);

	/* clean up */
	qfree(lb->q);
	kfree(lb);
}

static void
loopbackbwrite(struct Ipifc *ifc, struct block *bp, int unused_int,
			   uint8_t * unused_uint8_p_t)
{
	LB *lb;

	ptclcsum_finalize(bp, 0);
	lb = ifc->arg;
	if (qpass(lb->q, bp) < 0)
		ifc->outerr++;
	ifc->out++;
}

static void loopbackread(void *a)
{
	ERRSTACK(2);
	struct Ipifc *ifc;
	struct block *bp;
	LB *lb;

	ifc = a;
	lb = ifc->arg;
	lb->readp = current;	/* hide identity under a rock for unbind */
	if (waserror()) {
		lb->readp = 0;
		warn("loopbackread exits unexpectedly");
		return;
		poperror();
	}
	for (;;) {
		bp = qbread(lb->q, Maxtu);
		if (bp == NULL)
			continue;
		ifc->in++;
		if (!canrlock(&ifc->rwlock)) {
			freeb(bp);
			continue;
		}
		if (waserror()) {
			runlock(&ifc->rwlock);
			nexterror();
		}
		if (ifc->lifc == NULL) {
			freeb(bp);
		} else {
			ipifc_trace_block(ifc, bp);
			ipiput4(lb->f, ifc, bp);
		}
		runlock(&ifc->rwlock);
		poperror();
	}
	poperror();
}

struct medium loopbackmedium = {
	.hsize = 0,
	.mintu = 0,
	.maxtu = Maxtu,
	.maclen = 0,
	.name = "loopback",
	.bind = loopbackbind,
	.unbind = loopbackunbind,
	.bwrite = loopbackbwrite,
};

linker_func_4(loopbackmediumlink)
{
	addipmedium(&loopbackmedium);
}
