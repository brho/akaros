/*
 * DMA Pool allocator
 *
 * Copyright 2001 David Brownell
 * Copyright 2007 Intel Corporation
 *   Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * This software may be redistributed and/or modified under the terms of
 * the GNU General Public License ("GPL") version 2 as published by the
 * Free Software Foundation.
 *
 * This allocator returns small blocks of a given size which are DMA-able by
 * the given device.  It uses the dma_alloc_coherent page allocator to get
 * new pages, then splits them up into blocks of the required size.
 * Many older drivers still have their own code to do this.
 *
 * The current design of this allocator is fairly simple.  The pool is
 * represented by the 'struct dma_pool' which keeps a doubly-linked list of
 * allocated pages.  Each page in the page_list is split into blocks of at
 * least 'size' bytes.  Free blocks are tracked in an unsorted singly-linked
 * list of free blocks within the page.  Used blocks aren't tracked, but we
 * keep a count of how many are currently allocated from each page.
 */

#include <linux_compat.h>

struct dma_pool {
	struct list_head page_list;
	spinlock_t lock;
	size_t size;
	void *dev;
	size_t allocation;
	size_t boundary;
	char name[32];
	struct list_head pools;
};

struct dma_page {
	struct list_head page_list;
	void *vaddr;
	dma_addr_t dma;
	unsigned int in_use;
	unsigned int offset;
};

/**
 * dma_pool_create - Creates a pool of consistent memory blocks, for dma.
 */
struct dma_pool *dma_pool_create(const char *name, void *dev,
				 size_t size, size_t align, size_t boundary)
{
	struct dma_pool *retval;
	size_t allocation;

	if (align == 0)
		align = 1;
	else if (align & (align - 1))
		return NULL;

	if (size == 0)
		return NULL;
	else if (size < 4)
		size = 4;

	if ((size % align) != 0)
		size = ALIGN(size, align);

	allocation = MAX_T(size_t, size, PAGE_SIZE);

	if (!boundary)
		boundary = allocation;
	else if ((boundary < size) || (boundary & (boundary - 1)))
		return NULL;

	retval = kmalloc(sizeof(*retval), MEM_WAIT);
	if (!retval)
		return retval;

	strlcpy(retval->name, name, sizeof(retval->name));

	retval->dev = dev;	/* FIXME */

	INIT_LIST_HEAD(&retval->page_list);
	spinlock_init(&retval->lock);
	retval->size = size;
	retval->boundary = boundary;
	retval->allocation = allocation;

	INIT_LIST_HEAD(&retval->pools);

	/* TODO device_create_file */

	return retval;
}

void dma_pool_destroy(struct dma_pool *pool)
{
	/* TODO */
}

static void pool_initialise_page(struct dma_pool *pool, struct dma_page *page)
{
	unsigned int offset = 0;
	unsigned int next_boundary = pool->boundary;

	do {
		unsigned int next = offset + pool->size;
		if (unlikely((next + pool->size) >= next_boundary)) {
			next = next_boundary;
			next_boundary += pool->boundary;
		}
		*(int *)(page->vaddr + offset) = next;
		offset = next;
	} while (offset < pool->allocation);
}

static struct dma_page *pool_alloc_page(struct dma_pool *pool, int mem_flags)
{
	struct dma_page *page;

	page = kmalloc(sizeof(*page), mem_flags);
	if (!page)
		return NULL;
	page->vaddr = dma_alloc_coherent(pool->dev, pool->allocation,
					 &page->dma, mem_flags);
	if (page->vaddr) {
		pool_initialise_page(pool, page);
		page->in_use = 0;
		page->offset = 0;
	} else {
		kfree(page);
		page = NULL;
	}
	return page;
}

void *dma_pool_alloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle)
{
	struct dma_page *page;
	size_t offset;
	void *retval;

	/* FIXME take care of locks */

	list_for_each_entry(page, &pool->page_list, page_list) {
		if (page->offset < pool->allocation)
			goto ready;
	}

	page = pool_alloc_page(pool, mem_flags);
	if (!page)
		return NULL;

	list_add(&page->page_list, &pool->page_list);
ready:
	page->in_use++;
	offset = page->offset;
	page->offset = *(int *)(page->vaddr + offset);	/* "next" */
	retval = offset + page->vaddr;
	*handle = offset + page->dma;
	return retval;
}

void *dma_pool_zalloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle)
{
	void *ret = dma_pool_alloc(pool, mem_flags, handle);

	if (!ret)
		return NULL;
	memset(ret, 0, pool->size);
	return ret;
}

void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr)
{
	/* TODO */
}
