/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Devfs: filesystem interfaces to devices.  For now, we just create the
 * needed/discovered devices in KFS in its /dev/ folder. */

#ifndef ROS_KERN_DEVFS_H
#define ROS_KERN_DEVFS_H

#include <vfs.h>

void devfs_init(void);

/* Exporting these for convenience */
extern struct file *dev_stdin, *dev_stdout, *dev_stderr;

#endif /* !ROS_KERN_DEVFS_H */
