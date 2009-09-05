/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef ROS_KERNEL
# error "This is a ROS kernel header; user programs should not #include it"
#endif

#include <ros/common.h>

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

void cons_init(void);
void cons_putc(int c);
int cons_getc(void);

#endif /* _CONSOLE_H_ */
