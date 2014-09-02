/* See COPYRIGHT for copyright information. */

#ifndef ROS_ARCH_INIT_H
#define ROS_ARCH_INIT_H

void arch_init();
bool check_timing_stability(void);	/* in rdtsc_test.c */

void intel_lpc_init();

#endif // !ROS_ARCH_INIT_H

