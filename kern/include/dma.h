/* Copyright (c) 2019 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * An arena for DMA-able memory and a 'pool' (slab/KC) for smaller objects.
 * See dma.c for details.
 */

#pragma once

#include <arena.h>
#include <arch/pci.h>

typedef physaddr_t dma_addr_t;

struct dma_arena {
	struct arena		arena;
	void *(*to_cpu_addr)(struct dma_arena *da, dma_addr_t dev_addr);
	void 			*data;
};

/* Default arena: basically just physical pages */
extern struct dma_arena dma_phys_pages;
struct dma_arena *dev_to_dma_arena(struct device *d);

void dma_arena_init(void);

void *dma_arena_alloc(struct dma_arena *da, size_t size, dma_addr_t *dma_handle,
		      int mem_flags);
void *dma_arena_zalloc(struct dma_arena *da, size_t size,
		       dma_addr_t *dma_handle, int mem_flags);
void dma_arena_free(struct dma_arena *da, void *cpu_addr, dma_addr_t dma_handle,
		    size_t size);

/* Compatible with Linux's DMA pool */
struct dma_pool *dma_pool_create(const char *name, struct device *dev,
				 size_t size, size_t align, size_t allocation);
void dma_pool_destroy(struct dma_pool *pool);
void *dma_pool_alloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle);
void *dma_pool_zalloc(struct dma_pool *pool, int mem_flags, dma_addr_t *handle);
void dma_pool_free(struct dma_pool *pool, void *vaddr, dma_addr_t addr);
