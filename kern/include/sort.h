/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

#include <stdio.h>

void sort(void *base, size_t count, size_t size,
          int (*cmp)(const void *, const void *));
