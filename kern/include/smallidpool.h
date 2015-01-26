/* Copyright (c) 2015 Google Inc.
 *
 * Trivial ID pool for small sets of things (< 64K)
 * implemented as a stack.
 */

#define MAXAMT 65535

// Element 0 is top of stack pointer. It  initially points to 1.
// You therefore can not get ID 0. The max id you can get is 65534,
// due to the use of uint16_t for the stack elements. That covers just
// about everything we've ever needed.
// The check array is used instead of a bitfield because these architectures
// suck at those.
struct idpool {
	unit16_t *ids;
	unit8_t *check;
	int size;
};
