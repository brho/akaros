/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * System includes for vboot reference library.  With few exceptions, this is
 * the ONLY place in firmware/ where system headers may be included via
 * #include <...>, so that there's only one place that needs to be fixed up for
 * platforms which don't have all the system includes.
 */

#pragma once
/* Note that the old include/ros/env.h is merged into this file */

#include <ros/memlayout.h>
#include <ros/syscall.h>
#include <ros/sysevent.h>
#include <ros/procinfo.h>
#include <error.h>
#include <ros/procdata.h>
#include <ros/procinfo.h>
#include <ros/resource.h>
#include <trap.h>
#include <ros/common.h>
#include <arch/arch.h>
#include <sys/queue.h>
#include <atomic.h>
#include <mm.h>
#include <schedule.h>
#include <devalarm.h>
#include <ns.h>
#include <arch/vmm/vmm.h>
