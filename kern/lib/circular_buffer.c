/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <circular_buffer.h>

bool circular_buffer_init(struct circular_buffer *cb, size_t size, char *mem)
{
	cb->mem = mem;
	if (mem)
		cb->base = mem;
	else
		cb->base = kmalloc(size, MEM_WAIT);
	if (cb->base) {
		cb->rdptr = cb->wrptr = cb->base;
		cb->size = 0;
		cb->allocated = size;
	}

	return cb->base != NULL;
}

void circular_buffer_destroy(struct circular_buffer *cb)
{
	if (cb->base) {
		if (cb->mem)
			kfree(cb->mem);
		cb->rdptr = cb->wrptr = cb->base = cb->mem = NULL;
		cb->size = cb->allocated = 0;
	}
}

void circular_buffer_clear(struct circular_buffer *cb)
{
	cb->rdptr = cb->wrptr = cb->base;
	cb->size = 0;
}

static bool circular_buffer_is_overlap(const struct circular_buffer *cb,
                                       const char *rptr, const char *wptr,
                                       size_t size)
{
	/* Check if the current write operation [wptr, wptr+size) is overwriting
	 * the block at which rptr in pointing to.
	 */
	return (cb->size > 0) && (rptr >= wptr) && (rptr < (wptr + size));
}

static void circular_buffer_write_skip(struct circular_buffer *cb, char *wrptr,
                                       size_t size)
{
	/* Move the read pointer forward, so that the incoming write does not
	 * overwrite the block the read pointer is looking at.
	 */
	while (circular_buffer_is_overlap(cb, cb->rdptr, wrptr, size)) {
		char *rdptr = cb->rdptr;
		size_t bsize = *(const cbuf_size_t *) rdptr;

		if (likely(bsize)) {
			cb->rdptr = rdptr + bsize;
			cb->size -= bsize - sizeof(cbuf_size_t);
			if (unlikely(cb->size == 0))
				cb->rdptr = cb->base;
		} else {
			cb->rdptr = cb->base;
		}
	}
}

size_t circular_buffer_write(struct circular_buffer *cb,
                             const char *data, size_t size)
{
	/* Data is written and evetually discarded in atomic blocks, in order to
	 * maintain the consistency of the information stored in the buffer.
	 */
	char *wrptr = cb->wrptr;
	size_t wspace = (cb->base + cb->allocated) - wrptr;
	size_t esize = size + 2 * sizeof(cbuf_size_t);

	if (unlikely(esize > cb->allocated))
		return 0;
	/* If at the end of the buffer, the next block to be written does not
	 * fit, we move the pointer to the beginning of the circular buffer.
	 */
	if (unlikely(esize > wspace)) {
		circular_buffer_write_skip(cb, wrptr, wspace);
		wrptr = cb->base;
	}
	circular_buffer_write_skip(cb, wrptr, esize);

	/* Write the data and the end of sequence marker (0).
	 */
	*(cbuf_size_t *) wrptr = sizeof(cbuf_size_t) + size;
	memcpy(wrptr + sizeof(cbuf_size_t), data, size);
	cb->wrptr = wrptr + sizeof(cbuf_size_t) + size;
	cb->size += size;

	*(cbuf_size_t *) cb->wrptr = 0;

	return size;
}

size_t circular_buffer_read(struct circular_buffer *cb, char *data, size_t size,
                            size_t off)
{
	size_t asize = cb->size, rsize = 0;
	const char *rdptr = cb->rdptr;

	while (asize > 0 && size > 0) {
		size_t bsize = *(const cbuf_size_t *) rdptr;

		if (likely(bsize)) {
			size_t esize = bsize - sizeof(cbuf_size_t);

			if (off >= esize) {
				off -= esize;
			} else {
				size_t csize = MIN(esize - off, size);

				memcpy(data, rdptr + sizeof(cbuf_size_t) + off,
				       csize);
				data += csize;
				size -= csize;
				rsize += csize;
				off = 0;
			}
			rdptr = rdptr + bsize;
			asize -= esize;
		} else {
			rdptr = cb->base;
		}
	}

	return rsize;
}
