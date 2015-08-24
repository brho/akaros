/* Copyright (c) 2015 Google Inc.
 * Ron Minnich <rminnich@google.com>
 * See LICENSE for details.
 *
 * VMM.h */

#pragma once

#include <ros/vmm.h>

char *regname(uint8_t reg);
int decode(struct vmctl *v, uint64_t *gpa, uint8_t *destreg, uint64_t **regp,
           int *store, int *size, int *advance);
int io(struct vmctl *v);
