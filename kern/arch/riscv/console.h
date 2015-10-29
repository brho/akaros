/* See COPYRIGHT for copyright information. */

#pragma once
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <ros/common.h>

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

void cons_init(void);
/* Returns any available character, or 0 for none (legacy helper) */
int cons_get_any_char(void);
