/*
 * Copyright (c) 2009 The Regents of the University of California
 * See LICENSE for details.
 */

#ifndef ROS_KERN_MANAGER_H
#define ROS_KERN_MANAGER_H

/*
 * The manager is the "asymmetric control unit", that runs on core 0 for now
 * and controls the actions of the whole system.
 */
void manager(void);

/* Returns if the calling core is a management core or not. */
static inline bool management_core(void);

static inline bool management_core(void)
{
	// currently returns true for only core 0
	return !core_id();
}

#endif /* ROS_KERN_MANAGER_H */
