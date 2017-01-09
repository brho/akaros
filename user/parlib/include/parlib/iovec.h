/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Various iovec utility functions. */

#pragma once

#include <sys/uio.h>
#include <stdint.h>
#include <ros/common.h>

void iov_strip_bytes(struct iovec *iov, int iovcnt, size_t amt);
void iov_drop_trailing_bytes(struct iovec *iov, int iovcnt, size_t amt);
void iov_trim_len_to(struct iovec *iov, int iovcnt, size_t new_len);

bool iov_has_bytes(struct iovec *iov, int iovcnt, size_t amt);
size_t iov_get_len(struct iovec *iov, int iovcnt);
void iov_linearize(struct iovec *iov, int iovcnt, uint8_t *buf, size_t len);

void iov_set_byte(struct iovec *iov, int iovcnt, size_t idx, uint8_t val);
uint8_t iov_get_byte(struct iovec *iov, int iovcnt, size_t idx);
void iov_memcpy_from(struct iovec *iov, int iovcnt, size_t idx,
                     void *to, size_t amt);
void iov_memcpy_to(struct iovec *iov, int iovcnt, size_t to,
                   void *from, size_t amt);

uint16_t iov_get_be16(struct iovec *iov, int iovcnt, size_t idx);
uint16_t iov_get_be32(struct iovec *iov, int iovcnt, size_t idx);
void iov_put_be16(struct iovec *iov, int iovcnt, size_t idx, uint16_t val);
void iov_put_be32(struct iovec *iov, int iovcnt, size_t idx, uint32_t val);
