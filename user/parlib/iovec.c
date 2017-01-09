/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Various utility functions. */

#include <parlib/iovec.h>
#include <assert.h>
#include <sys/param.h>

/* Strips bytes from the front of the iovec.  This modifies the base and
 * length of the iovecs in the array. */
void iov_strip_bytes(struct iovec *iov, int iovcnt, size_t amt)
{
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len <= amt) {
			amt -= iov[i].iov_len;
			iov[i].iov_len = 0;
			continue;
		}
		iov[i].iov_len -= amt;
		iov[i].iov_base += amt;
		amt = 0;
	}
}

/* Drops bytes from the end of the iovec.  This modified the length of the
 * iovecs in the array. */
void iov_drop_trailing_bytes(struct iovec *iov, int iovcnt, size_t amt)
{
	for (int i = iovcnt - 1; i >= 0; i--) {
		if (iov[i].iov_len <= amt) {
			amt -= iov[i].iov_len;
			iov[i].iov_len = 0;
			continue;
		}
		iov[i].iov_len -= amt;
		break;
	}
}

/* Trims the iov's length to new_len, modifying the iov_lens.  If the iov is
 * shorter than new_len, it will not grow. */
void iov_trim_len_to(struct iovec *iov, int iovcnt, size_t new_len)
{
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len <= new_len) {
			new_len -= iov[i].iov_len;
			continue;
		}
		iov[i].iov_len = new_len;
		break;
	}
}

/* Checks if iov has amt bytes in it. */
bool iov_has_bytes(struct iovec *iov, int iovcnt, size_t amt)
{
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len >= amt)
			return TRUE;
		amt -= iov[i].iov_len;
	}
	return FALSE;
}

size_t iov_get_len(struct iovec *iov, int iovcnt)
{
	size_t ret = 0;

	for (int i = 0; i < iovcnt; i++)
		ret += iov[i].iov_len;
	return ret;
}

/* Copies up to len bytes of the contents of IOV into buf. */
void iov_linearize(struct iovec *iov, int iovcnt, uint8_t *buf, size_t len)
{
	size_t copy_amt;
	size_t sofar = 0;

	for (int i = 0; i < iovcnt; i++) {
		if (!len)
			break;
		copy_amt = MIN(iov[i].iov_len, len);
		memcpy(buf + sofar, iov[i].iov_base, copy_amt);
		sofar += copy_amt;
		len -= copy_amt;
	}
}

/* Sets a byte in an iov, essentially iov_mem[idx] = val. */
void iov_set_byte(struct iovec *iov, int iovcnt, size_t idx, uint8_t val)
{
	uint8_t *p = NULL;

	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len <= idx) {
			idx -= iov[i].iov_len;
			continue;
		}
		p = iov[i].iov_base + idx;
		break;
	}
	assert(p);
	*p = val;
}

/* Sets a byte in an iov, essentially *val = iov_mem[idx]. */
uint8_t iov_get_byte(struct iovec *iov, int iovcnt, size_t idx)
{
	uint8_t *p = NULL;

	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len <= idx) {
			idx -= iov[i].iov_len;
			continue;
		}
		p = iov[i].iov_base + idx;
		break;
	}
	assert(p);
	return *p;
}

void iov_memcpy_from(struct iovec *iov, int iovcnt, size_t from,
                     void *to, size_t amt)
{
	size_t copy_amt;
	void *copy_start;
	size_t sofar = 0;

	for (int i = 0; i < iovcnt; i++) {
		if (!amt)
			break;
		if (iov[i].iov_len <= from) {
			from -= iov[i].iov_len;
			continue;
		}
		copy_start = iov[i].iov_base + from;
		copy_amt = iov[i].iov_len - from;
		from = 0;
		copy_amt = MIN(copy_amt, amt);
		memcpy(to + sofar, copy_start, copy_amt);
		sofar += copy_amt;
		amt -= copy_amt;
	}
	assert(!amt);
}

void iov_memcpy_to(struct iovec *iov, int iovcnt, size_t to,
                   void *from, size_t amt)
{
	size_t copy_amt;
	void *copy_start;
	size_t sofar = 0;

	for (int i = 0; i < iovcnt; i++) {
		if (!amt)
			break;
		if (iov[i].iov_len <= to) {
			to -= iov[i].iov_len;
			continue;
		}
		copy_start = iov[i].iov_base + to;
		copy_amt = iov[i].iov_len - to;
		to = 0;
		copy_amt = MIN(copy_amt, amt);
		memcpy(copy_start, from + sofar, copy_amt);
		sofar += copy_amt;
		amt -= copy_amt;
	}
	assert(!amt);
}

uint16_t iov_get_be16(struct iovec *iov, int iovcnt, size_t idx)
{
	return ((uint16_t)iov_get_byte(iov, iovcnt, idx + 0) << 8)
	     | ((uint16_t)iov_get_byte(iov, iovcnt, idx + 1) << 0);
}

uint16_t iov_get_be32(struct iovec *iov, int iovcnt, size_t idx)
{
	return ((uint32_t)iov_get_byte(iov, iovcnt, idx + 0) << 24)
	     | ((uint32_t)iov_get_byte(iov, iovcnt, idx + 1) << 16)
	     | ((uint32_t)iov_get_byte(iov, iovcnt, idx + 2) << 8)
	     | ((uint32_t)iov_get_byte(iov, iovcnt, idx + 3) << 0);
}

void iov_put_be16(struct iovec *iov, int iovcnt, size_t idx, uint16_t val)
{
	iov_set_byte(iov, iovcnt, idx + 0, (val >> 8) & 0xff);
	iov_set_byte(iov, iovcnt, idx + 1, (val >> 0) & 0xff);
}

void iov_put_be32(struct iovec *iov, int iovcnt, size_t idx, uint32_t val)
{
	iov_set_byte(iov, iovcnt, idx + 0, (val >> 24) & 0xff);
	iov_set_byte(iov, iovcnt, idx + 1, (val >> 16) & 0xff);
	iov_set_byte(iov, iovcnt, idx + 2, (val >>  8) & 0xff);
	iov_set_byte(iov, iovcnt, idx + 3, (val >>  0) & 0xff);
}
