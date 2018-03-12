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

static void pktbind(struct Ipifc *i, int unused_int, char **unused_char_pp_t);
static void pktunbind(struct Ipifc *i);
static void pktbwrite(struct Ipifc *i, struct block *, int unused_int,
					  uint8_t * unused_uint8_p_t);
static void pktin(struct Fs *f, struct Ipifc *i, struct block *b);

struct medium pktmedium = {
	.name = "pkt",
	.hsize = 14,
	.mintu = 40,
	.maxtu = 4 * 1024,
	.maclen = 6,
	.bind = pktbind,
	.unbind = pktunbind,
	.bwrite = pktbwrite,
	.pktin = pktin,
	.unbindonclose = 1,
};

/*
 *  called to bind an IP ifc to an ethernet device
 *  called with ifc wlock'd
 */
static void pktbind(struct Ipifc *i, int unused_int, char **unused_char_pp_t)
{
}

/*
 *  called with ifc wlock'd
 */
static void pktunbind(struct Ipifc *i)
{
}

/*
 *  called by ipoput with a single packet to write
 */
static void
pktbwrite(struct Ipifc *ifc, struct block *bp, int unused_int,
		  uint8_t * unused_uint8_p_t)
{
	/* enqueue onto the conversation's rq */
	bp = concatblock(bp);
	ptclcsum_finalize(bp, 0);
	ipifc_trace_block(ifc, bp);
	qpass(ifc->conv->rq, bp);
}

/*
 *  called with ifc rlocked when someone write's to 'data'
 */
static void pktin(struct Fs *f, struct Ipifc *ifc, struct block *bp)
{
	if (ifc->lifc == NULL) {
		freeb(bp);
	} else {
		ipifc_trace_block(ifc, bp);
		ipiput4(f, ifc, bp);
	}
}

void pktmediumlink(void)
{
	addipmedium(&pktmedium);
}
