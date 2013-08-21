/* 
 * Copyright 2013 Google Inc.
 * See LICENSE for details.
 *
 * Copyright (c) 1989-2003 by Lucent Technologies, Bell Laboratories.
 *
 * First cut at Plan 9 namespace system calls
 */

#ifdef __SHARC__
#pragma nosharc
#endif

#ifdef __DEPUTY__
#pragma nodeputy
#endif

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
