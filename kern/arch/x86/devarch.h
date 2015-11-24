/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#pragma once

int ioalloc(int port, int size, int align, char *tag);
void iofree(int port);
int iounused(int start, int end);
void ioinit(void);
int ioreserve(int unused_int, int size, int align, char *tag);
void archreset(void);
