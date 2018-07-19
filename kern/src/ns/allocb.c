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
#include <process.h>

/* Note that Hdrspc is only available via padblock (to the 'left' of the rp). */
enum {
	Hdrspc = 128,		/* leave room for high-level headers */
	Bdead = 0x51494F42,	/* "QIOB" */
	BLOCKALIGN = 32,	/* was the old BY2V in inferno, which was 8 */
};

/*
 *  allocate blocks (round data base address to 64 bit boundary).
 *  if mallocz gives us more than we asked for, leave room at the front
 *  for header.
 */
struct block *block_alloc(size_t size, int mem_flags)
{
	struct block *b;
	uintptr_t addr;
	int n;

	/* If Hdrspc is not block aligned it will cause issues. */
	static_assert(Hdrspc % BLOCKALIGN == 0);

	b = kmalloc(sizeof(struct block) + size + Hdrspc + (BLOCKALIGN - 1),
				mem_flags);
	if (b == NULL)
		return NULL;

	b->next = NULL;
	b->list = NULL;
	b->free = NULL;
	b->flag = 0;
	b->extra_len = 0;
	b->nr_extra_bufs = 0;
	b->extra_data = 0;
	b->mss = 0;
	b->network_offset = 0;
	b->transport_offset = 0;

	addr = (uintptr_t) b;
	addr = ROUNDUP(addr + sizeof(struct block), BLOCKALIGN);
	b->base = (uint8_t *) addr;
	/* TODO: support this */
	/* interesting. We can ask the allocator, after allocating,
	 * the *real* size of the block we got. Very nice.
	 * Not on akaros yet.
	 b->lim = ((uint8_t*)b) + msize(b);
	 * See use of n in commented code below
	 */
	b->lim =
		((uint8_t *) b) + sizeof(struct block) + size + Hdrspc + (BLOCKALIGN -
																  1);
	b->rp = b->base;
	/* TODO: support this */
	/* n is supposed to be Hdrspc + rear padding + extra reserved memory, but
	 * since we don't currently support checking how much memory was actually
	 * reserved, this is always Hdrspc + rear padding. After rounding that down
	 * to BLOCKALIGN, it's always Hdrpsc since the padding is < BLOCKALIGN.
	 n = b->lim - b->base - size;
	 b->rp += n & ~(BLOCKALIGN - 1);
	 */
	b->rp += Hdrspc;
	b->wp = b->rp;
	/* b->base is aligned, rounded up from b
	 * b->lim is the upper bound on our malloc
	 * b->rp is advanced by some aligned amount, based on how much extra we
	 * received from kmalloc and the Hdrspc. */
	return b;
}

/* Makes sure b has nr_bufs extra_data.  Will grow, but not shrink, an existing
 * extra_data array.  When growing, it'll copy over the old entries.  All new
 * entries will be zeroed.  mem_flags determines if we'll block on kmallocs.
 *
 * Return 0 on success or -1 on error.
 * Caller is responsible for concurrent access to the block's metadata. */
int block_add_extd(struct block *b, unsigned int nr_bufs, int mem_flags)
{
	unsigned int old_nr_bufs = b->nr_extra_bufs;
	size_t old_amt = sizeof(struct extra_bdata) * old_nr_bufs;
	size_t new_amt = sizeof(struct extra_bdata) * nr_bufs;
	void *new_bdata;

	if (old_nr_bufs >= nr_bufs)
		return 0;
	if (b->extra_data) {
		new_bdata = krealloc(b->extra_data, new_amt, mem_flags);
		if (!new_bdata)
			return -1;
		memset(new_bdata + old_amt, 0, new_amt - old_amt);
	} else {
		new_bdata = kzmalloc(new_amt, mem_flags);
		if (!new_bdata)
			return - 1;
	}
	b->extra_data = new_bdata;
	b->nr_extra_bufs = nr_bufs;
	return 0;
}

/* Go backwards from the end of the list, remember the last unused slot, and
 * stop when a used slot is encountered. */
static struct extra_bdata *next_unused_slot(struct block *b)
{
	struct extra_bdata *ebd = NULL;

	for (int i = b->nr_extra_bufs - 1; i >= 0; i--) {
		if (b->extra_data[i].base)
			break;
		ebd = &b->extra_data[i];
	}
	return ebd;
}

/* Append an extra data buffer @base with offset @off of length @len to block
 * @b.  Reuse an unused extra data slot if there's any.
 * Return 0 on success or -1 on error. */
int block_append_extra(struct block *b, uintptr_t base, uint32_t off,
                       uint32_t len, int mem_flags)
{
	unsigned int nr_bufs = b->nr_extra_bufs + 1;
	struct extra_bdata *ebd;

	ebd = next_unused_slot(b);
	if (!ebd) {
		if (block_add_extd(b, nr_bufs, mem_flags) != 0)
			return -1;
		ebd = next_unused_slot(b);
		assert(ebd);
	}
	ebd->base = base;
	ebd->off = off;
	ebd->len = len;
	b->extra_len += ebd->len;
	return 0;
}

/* There's metadata in each block related to the data payload.  For instance,
 * the TSO mss, the offsets to various headers, whether csums are needed, etc.
 * When you create a new block, like in copyblock, this will copy those bits
 * over. */
void block_copy_metadata(struct block *new_b, struct block *old_b)
{
	new_b->flag |= (old_b->flag & BLOCK_META_FLAGS);
	new_b->tx_csum_offset = old_b->tx_csum_offset;
	new_b->mss = old_b->mss;
	new_b->network_offset = old_b->network_offset;
	new_b->transport_offset = old_b->transport_offset;
}

void block_reset_metadata(struct block *b)
{
	b->flag &= ~BLOCK_META_FLAGS;
	b->tx_csum_offset = 0;
	b->mss = 0;
	b->network_offset = 0;
	b->transport_offset = 0;
}

void free_block_extra(struct block *b)
{
	struct extra_bdata *ebd;

	/* assuming our release method is kfree, which will change when we support
	 * user buffers */
	for (int i = 0; i < b->nr_extra_bufs; i++) {
		ebd = &b->extra_data[i];
		if (ebd->base)
			kfree((void*)ebd->base);
	}
	b->extra_len = 0;
	b->nr_extra_bufs = 0;
	kfree(b->extra_data);	/* harmless if it is 0 */
	b->extra_data = 0;		/* in case the block is reused by a free override */
}

/* Frees a block, returning its size (len, not alloc) */
size_t freeb(struct block *b)
{
	void *dead = (void *)Bdead;
	size_t ret;

	if (b == NULL)
		return 0;
	ret = BLEN(b);
	free_block_extra(b);
	/*
	 * drivers which perform non cache coherent DMA manage their own buffer
	 * pool of uncached buffers and provide their own free routine.
	 */
	if (b->free) {
		b->free(b);
		return ret;
	}
	/* poison the block in case someone is still holding onto it */
	b->next = dead;
	b->rp = dead;
	b->wp = dead;
	b->lim = dead;
	b->base = dead;
	kfree(b);
	return ret;
}

/* Free a list of blocks, returning their total size. */
size_t freeblist(struct block *b)
{
	struct block *next;
	size_t ret = 0;

	for (; b != 0; b = next) {
		next = b->next;
		b->next = 0;
		ret += freeb(b);
	}
	return ret;
}

void checkb(struct block *b, char *msg)
{
	void *dead = (void *)Bdead;
	struct extra_bdata *ebd;
	size_t extra_len = 0;

	if (b == dead)
		panic("checkb b %s 0x%lx", msg, b);
	if (b->base == dead || b->lim == dead || b->next == dead
		|| b->rp == dead || b->wp == dead) {
		printd("checkb: base 0x%8.8lx lim 0x%8.8lx next 0x%8.8lx\n",
			   b->base, b->lim, b->next);
		printd("checkb: rp 0x%8.8lx wp 0x%8.8lx\n", b->rp, b->wp);
		panic("checkb dead: %s\n", msg);
	}

	if (b->base > b->lim)
		panic("checkb 0 %s 0x%lx 0x%lx", msg, b->base, b->lim);
	if (b->rp < b->base)
		panic("checkb 1 %s 0x%lx 0x%lx", msg, b->base, b->rp);
	if (b->wp < b->base)
		panic("checkb 2 %s 0x%lx 0x%lx", msg, b->base, b->wp);
	if (b->rp > b->lim)
		panic("checkb 3 %s 0x%lx 0x%lx", msg, b->rp, b->lim);
	if (b->wp > b->lim)
		panic("checkb 4 %s 0x%lx 0x%lx", msg, b->wp, b->lim);
	if (b->nr_extra_bufs && !b->extra_data)
		panic("checkb 5 %s missing extra_data", msg);

	for (int i = 0; i < b->nr_extra_bufs; i++) {
		ebd = &b->extra_data[i];
		if (!ebd->base && (ebd->off || ebd->len))
			panic("checkb %s: ebd %d has no base, but has off %d and len %d",
			      msg, i, ebd->off, ebd->len);
		if (ebd->base) {
			if (!kmalloc_refcnt((void*)ebd->base))
				panic("checkb %s: buf %d, base %p has no refcnt!\n", msg, i,
				      ebd->base);
			extra_len += ebd->len;
		}
	}
	if (extra_len != b->extra_len)
		panic("checkb %s: block extra_len %d differs from sum of ebd len %d",
		      msg, b->extra_len, extra_len);
}

void printblock(struct block *b)
{
	unsigned char *c;
	unsigned int off, elen;
	struct extra_bdata *e;

	if (b == NULL) {
		printk("block is null\n");
		return;
	}

	print_lock();
	printk("block of BLEN = %d, with %d header and %d data in %d extras\n",
	       BLEN(b), BHLEN(b), b->extra_len, b->nr_extra_bufs);

	printk("header:\n");
	printk("%2x:\t", 0);
	off = 0;
	for (c = b->rp; c < b->wp; c++) {
		printk("  %02x", *c & 0xff);
		off++;
		if (off % 8 == 0) {
			printk("\n");
			printk("%2x:\t", off);
		}
	}
	printk("\n");
	elen = b->extra_len;
	for (int i = 0; (i < b->nr_extra_bufs) && elen; i++) {
		e = &b->extra_data[i];
		if (e->len == 0)
			continue;
		elen -= e->len;
		printk("data %d:\n", i);
		printk("%2x:\t", 0);
		for (off = 0; off < e->len; off++) {
			c = (unsigned char *)e->base + e->off + off;
			printk("  %02x", *c & 0xff);
			if ((off + 1) % 8 == 0 && off +1 < e->len) {
				printk("\n");
				printk("%2x:\t", off + 1);
			}
		}
	}
	printk("\n");
	print_unlock();
}
