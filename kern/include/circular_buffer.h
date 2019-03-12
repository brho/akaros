/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 *
 * The circular buffer interface allows to allocate once, and store atomic
 * data blocks into it, with automatic drop of older blocks, when the buffer
 * becomes full.
 * Blocks are written atomically in the sense that when data is dropped from
 * the head of the buffer, full blocks are dropped, so the circular buffer
 * never returns partial blocks.
 * Appending is O(1) WRT the number of blocks, while seeks are O(N).
 * Reading data from the circular buffer interface, does not drop  the read
 * data from the head of the buffer (the head is pushed forward only when the
 * write pointer wraps around and need more space for a new incoming write
 * operation).
 * The buffer can either be initialized with caller memory (if the "mem"
 * parameter of circular_buffer_init() is not NULL), or it can allocate
 * memory by itself in circular_buffer_init().
 * Once the initial (eventual) allocation is done, no more allocations will
 * be performed.
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>

typedef uint32_t cbuf_size_t;

struct circular_buffer {
	char *mem;
	char *base;
	char *rdptr;
	char *wrptr;
	size_t size;
	size_t allocated;
};

bool circular_buffer_init(struct circular_buffer *cb, size_t size, char *mem);
void circular_buffer_destroy(struct circular_buffer *cb);
void circular_buffer_clear(struct circular_buffer *cb);
size_t circular_buffer_write(struct circular_buffer *cb, const char *data,
			     size_t size);
size_t circular_buffer_read(struct circular_buffer *cb, char *data, size_t size,
			    size_t off);

static inline size_t circular_buffer_size(const struct circular_buffer *cb)
{
	return cb->size;
}

static inline size_t circular_buffer_max_write_size(
	const struct circular_buffer *cb)
{
	return cb->allocated - 2 * sizeof(cbuf_size_t);
}
